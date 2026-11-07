#include "content_server.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "library.h"
#include "m3u.h"
#include "paths.h"
#include "sha256.h"

namespace vj::ecs {
namespace fs = std::filesystem;
using neuro::Json;

namespace {
constexpr uint64_t kGameLimit = uint64_t(8) * 1024 * 1024 * 1024;
constexpr size_t kFileLimit = 512;

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string text(const Json& value) {
    require(value.isString() && !value.asString().empty() && value.asString().size() <= 4096,
            "The server returned an invalid text field.");
    return value.asString();
}

uint64_t sizeOf(const Json& value) {
    require(value.isNumber() && value.asNumber() >= 0 && value.asNumber() <= double(kGameLimit) &&
                std::floor(value.asNumber()) == value.asNumber(), "The server returned an invalid file size.");
    return static_cast<uint64_t>(value.asNumber());
}

void checkCancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) throw std::runtime_error("Cancelled.");
}

void checkResponse(bool received, const http::Response& response) {
    if (!received) throw std::runtime_error(response.error.empty() ? "Could not reach the server." : response.error);
    if (response.status == 401) throw std::runtime_error("Access token required or expired. Check the server token.");
    if (response.status == 403) throw std::runtime_error("This token cannot read this library or game.");
    if (response.status == 404) throw std::runtime_error("This library or game is no longer available.");
    if (response.status != 200)
        throw std::runtime_error("The server returned HTTP " + std::to_string(response.status) + ". Refresh and try again.");
}

struct File {
    std::string id, path, endpoint, hash;
    uint64_t size = 0;
    bool game = true;
};

bool hexHash(const std::string& value) {
    return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::string contentPath(const char* kind, const std::string& id) {
    return std::string("/api/v1/") + kind + "/" + lib::urlEncode(id) + "/content";
}

std::string fileHash(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read a downloaded file.");
    Sha256 hash;
    char buffer[65536];
    while (stream.read(buffer, sizeof buffer) || stream.gcount()) hash.update(buffer, size_t(stream.gcount()));
    require(stream.eof(), "Could not read a downloaded file.");
    return hash.hexDigest();
}


bool cachedFilesMatch(const fs::path& folder, const fs::path& base, const std::vector<File>& files) {
    if (fs::is_symlink(folder) || !fs::is_directory(folder)) return false;
    for (const File& file : files) {
        const fs::path existing = folder / fs::u8path(file.path);
        fs::path parent = existing;
        while (parent != base && parent != parent.parent_path()) {
            if (fs::is_symlink(parent)) return false;
            parent = parent.parent_path();
        }
        if (!fs::is_regular_file(existing) || fs::file_size(existing) != file.size || fileHash(existing) != file.hash)
            return false;
    }
    return true;
}

// A descriptor can only name companions downloaded in this same set, including contained ../ references.
void checkReference(const File& descriptor, const std::string& reference, const std::set<std::string>& files) {
    require(!reference.empty() && reference.front() != '/' && reference.find('\\') == std::string::npos &&
                reference.find(':') == std::string::npos, "A disc descriptor names an unsafe companion path.");
    const std::string resolved = (fs::u8path(descriptor.path).parent_path() / fs::u8path(reference)).lexically_normal().generic_u8string();
    require(safeRelativePath(resolved) && files.count(lower(resolved)), "A disc companion is missing from the downloaded set.");
}

void checkDescriptors(const std::vector<File>& files, const fs::path& folder) {
    std::set<std::string> names;
    for (const File& file : files) names.insert(lower(file.path));
    for (const File& file : files) {
        const std::string ext = pathExt(file.path);
        if (ext != "cue" && ext != "m3u") continue;
        require(file.size <= 1024 * 1024, "A disc descriptor is too large.");
        std::ifstream input(folder / fs::u8path(file.path), std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        require(input.good() || input.eof(), "Could not read a disc descriptor.");
        size_t count = 0;
        if (ext == "m3u") {
            for (const auto& reference : parseM3u(body, "")) {
                require(pathExt(reference) != "m3u", "Nested disc playlists are unsupported.");
                checkReference(file, reference, names);
                ++count;
            }
        } else {
            std::istringstream lines(body);
            std::string line;
            while (std::getline(lines, line)) {
                const size_t start = line.find_first_not_of(" \t\r");
                if (start == std::string::npos || lower(line.substr(start, 4)) != "file") continue;
                const size_t first = line.find('"', start + 4);
                const size_t last = first == std::string::npos ? first : line.find('"', first + 1);
                require(first != std::string::npos && last != std::string::npos,
                        "Disc FILE entries must use quoted companion names.");
                checkReference(file, line.substr(first + 1, last - first - 1), names);
                ++count;
            }
        }
        require(count > 0, "The disc descriptor has no companion files.");
    }
}

std::string launchFile(const std::vector<File>& files) {
    for (const std::string extension : {"m3u", "cue", "chd", "cdi", "cart"}) {
        std::vector<std::string> choices;
        for (const File& file : files) {
            if (!file.game) continue;
            const std::string ext = pathExt(file.path);
            if (ext == extension || (extension == "cart" && lib::isCartExt(ext))) choices.push_back(file.path);
        }
        require(choices.size() <= 1, "This item has several games. Choose a separate item or a disc playlist.");
        if (!choices.empty()) return choices.front();
    }
    throw std::runtime_error("This game has no supported cartridge or disc image.");
}

struct Staging {
    fs::path path;
    ~Staging() { std::error_code ignored; if (!path.empty()) fs::remove_all(path, ignored); }
};

} // namespace

std::string normalizeOrigin(const std::string& address) {
    const size_t scheme = address.find("://");
    require(scheme != std::string::npos, "Enter a server URL such as http://192.168.1.10:8473.");
    const std::string protocol = lower(address.substr(0, scheme));
    require(protocol == "http" || protocol == "https", "The server URL must use HTTP or HTTPS.");
    std::string authority = address.substr(scheme + 3);
    while (!authority.empty() && authority.back() == '/') authority.pop_back();
    require(!authority.empty() && authority.size() <= 480, "The server address is invalid.");
    for (unsigned char c : authority)
        require(c > 32 && c < 127 && std::string("/@?#\\%\"").find(char(c)) == std::string::npos,
                "Use the server origin, with no credentials, path, query or fragment.");
    size_t port = std::string::npos;
    if (authority.front() == '[') {
        const size_t end = authority.find(']');
        require(end > 1 && end != std::string::npos, "Invalid IPv6 server address.");
        require(authority.substr(1, end - 1).find_first_not_of("0123456789abcdefABCDEF:.") == std::string::npos,
                "Invalid IPv6 server address.");
        if (end + 1 < authority.size()) {
            require(authority[end + 1] == ':', "Invalid server port.");
            port = end + 1;
        }
    } else {
        port = authority.find(':');
        const std::string host = authority.substr(0, port);
        require(!host.empty() && host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_") == std::string::npos,
                "Invalid server host name.");
    }
    if (port != std::string::npos) {
        const std::string value = authority.substr(port + 1);
        require(!value.empty() && value.size() <= 5 && value.find_first_not_of("0123456789") == std::string::npos,
                "Invalid server port.");
        const int number = std::stoi(value);
        require(number > 0 && number <= 65535, "Invalid server port.");
        authority = authority.substr(0, port) + ((protocol == "http" && number == 80) || (protocol == "https" && number == 443)
                    ? "" : ":" + std::to_string(number));
    }
    return protocol + "://" + lower(authority);
}

bool safeRelativePath(const std::string& path) {
    if (path.empty() || path.size() > 1024 || path.front() == '/' || path.find('\\') != std::string::npos) return false;
    size_t start = 0;
    while (start < path.size()) {
        const size_t end = path.find('/', start);
        const std::string component = path.substr(start, end == std::string::npos ? end : end - start);
        if (component.empty() || component == "." || component == ".." || component.back() == '.' || component.back() == ' ') return false;
        for (unsigned char c : component)
            if (c < 32 || c == 127 || std::string("<>:\"|?*").find(char(c)) != std::string::npos) return false;
        const std::string stem = lower(component.substr(0, component.find('.')));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '1' && stem[3] <= '9')) return false;
        if (end == std::string::npos) return true;
        start = end + 1;
    }
    return false;
}

Client::Client(std::string origin, std::string token, Transport transport)
    : origin_(normalizeOrigin(origin)), token_(std::move(token)), transport_(std::move(transport)) {}

Json Client::json(const std::string& path, const std::atomic<bool>* cancel) const {
    checkCancel(cancel);
    http::Options options;
    options.bearerToken = token_;
    options.followRedirects = false;
    http::Response response;
    checkResponse(transport_(origin_ + path, response, 8 * 1024 * 1024, cancel, options), response);
    Json result;
    std::string error;
    require(Json::parse(std::string(response.body.begin(), response.body.end()), result, error) && result.isObject(),
            "The server returned invalid JSON.");
    return result;
}

std::vector<Library> Client::libraries(const std::atomic<bool>* cancel) const {
    const Json capabilities = json("/api/v1/capabilities", cancel);
    require(capabilities["protocol"]["major"].isNumber() && capabilities["protocol"]["major"].asNumber() == 1,
            "This server uses an unsupported content protocol.");
    const Json result = json("/api/v1/browse-libraries", cancel);
    require(result["items"].isArray() && result["items"].items().size() <= 4096, "Invalid library list.");
    std::vector<Library> libraries;
    for (const Json& item : result["items"].items()) libraries.push_back({text(item["id"]), text(item["name"])});
    return libraries;
}

Page Client::games(const std::string& library, const std::string& search, const std::string& system,
                   const std::string& cursor, const std::atomic<bool>* cancel) const {
    require(system == "com.provenance.jaguar" || system == "com.provenance.jaguarcd", "Choose a Jaguar system.");
    std::string path = "/api/v1/browse?category=games&limit=100&libraryId=" + lib::urlEncode(library) +
                       "&systemId=" + lib::urlEncode(system) + "&search=" + lib::urlEncode(search);
    if (!cursor.empty()) path += "&cursor=" + lib::urlEncode(cursor);
    const Json result = json(path, cancel);
    require(result["items"].isArray() && result["items"].items().size() <= 100, "Invalid game page.");
    require(result.has("nextCursor") && (result["nextCursor"].isNull() || result["nextCursor"].isString()), "Invalid page cursor.");
    Page page;
    page.nextCursor = result["nextCursor"].asString();
    for (const Json& item : result["items"].items()) {
        const std::string type = text(item["entityType"]);
        if (type != "item" && type != "source" && type != "asset") continue;
        if (item["systemId"].asString() != system) continue;
        require(item[type].isObject(), "A game is missing its download details.");
        page.games.push_back({text(item["id"]), text(item["name"]), type, system, item[type]});
    }
    return page;
}

std::string Client::download(const Game& game, const std::string& cacheRoot, const std::atomic<bool>* cancel,
                             const std::function<void(uint64_t, uint64_t)>& progress) const {
    std::vector<File> files;
    std::string selected;
    if (game.entityType == "item") {
        const Json manifest = json("/api/v1/items/" + lib::urlEncode(game.id) + "/manifest", cancel);
        require(manifest["files"].isArray(), "The game manifest has no files.");
        for (const Json& file : manifest["files"].items()) {
            if (!file["required"].asBool()) continue;
            const std::string id = text(file["id"]);
            const std::string hash = text(file["sha256"]);
            require(hexHash(hash), "The manifest has an invalid SHA-256.");
            files.push_back({id, text(file["relativePath"]), contentPath("files", id), hash, sizeOf(file["size"]),
                             file["kind"].asString() == "rom"});
        }
    } else if (game.entityType == "source" && (pathExt(game.payload["name"].asString()) == "cue" ||
                                               game.payload["groupFileCount"].asNumber() > 0)) {
        const Json group = json("/api/v1/sources/" + lib::urlEncode(game.id) + "/group", cancel);
        require(group.has("error") && group["error"].isNull() && group["files"].isArray(), "This disc group is incomplete. Rescan its server folder.");
        for (const Json& file : group["files"].items()) {
            const std::string id = text(file["id"]);
            const std::string path = text(file.has("relativePath") ? file["relativePath"] : file["name"]);
            require(file["availability"].asString() == "local", "A disc companion is offline or evicted.");
            files.push_back({id, path, contentPath("sources", id), "", sizeOf(file["size"])});
            if (id == game.id) selected = path;
        }
        require(!selected.empty(), "The disc group does not include its descriptor.");
    } else {
        require(game.entityType == "source" || game.entityType == "asset", "Unsupported game entry.");
        const Json& file = game.payload;
        if (game.entityType == "source") require(file["availability"].asString() == "local", "This game is offline or evicted.");
        const std::string path = text(file[game.entityType == "source" ? "name" : "relativePath"]);
        const std::string hash = game.entityType == "asset" ? text(file["sha256"]) : "";
        require(hash.empty() || hexHash(hash), "The file has an invalid SHA-256.");
        files.push_back({game.id, path, contentPath(game.entityType == "source" ? "sources" : "files", game.id), hash, sizeOf(file["size"])});
    }
    require(!files.empty() && files.size() <= kFileLimit, "A game must contain between 1 and 512 files.");
    uint64_t total = 0;
    std::set<std::string> names;
    for (const File& file : files) {
        require(safeRelativePath(file.path) && names.insert(lower(file.path)).second, "The game contains unsafe or duplicate file paths.");
        require(file.size <= kGameLimit - total, "The game exceeds the 8 GiB download limit.");
        total += file.size;
    }
    if (selected.empty()) selected = launchFile(files);
    require(lib::isCartExt(pathExt(selected)) || lib::isDiscExt(pathExt(selected)), "Unsupported game image.");
    checkCancel(cancel);
    const std::string identity = origin_ + "\n" + game.entityType + "\n" + game.id;
    const fs::path base = fs::u8path(cacheRoot) / sha256Hex(identity.data(), identity.size());
    fs::create_directories(base);
    Staging staging;
    static std::atomic<uint64_t> sequence{0};
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path candidate = base / (".download-" + std::to_string(stamp) + "-" + std::to_string(++sequence));
        if (fs::create_directory(candidate)) { staging.path = candidate; break; }
    }
    require(!staging.path.empty(), "Could not create a download staging folder.");
    uint64_t completed = 0;
    std::string fingerprint;
    for (File& file : files) {
        checkCancel(cancel);
        const fs::path target = staging.path / fs::u8path(file.path);
        fs::create_directories(target.parent_path());
        require(!fs::exists(target), "The game contains duplicate file paths on this filesystem.");
        std::ofstream output(target, std::ios::binary);
        require(bool(output), "Could not create a downloaded file.");
        Sha256 hash;
        http::Options options;
        options.bearerToken = token_;
        options.followRedirects = false;
        options.write = [&](const void* data, size_t size) {
            if (cancel && cancel->load()) return false;
            output.write(static_cast<const char*>(data), std::streamsize(size));
            if (!output) return false;
            hash.update(data, size);
            completed += size;
            if (progress) progress(completed, total);
            return true;
        };
        http::Response response;
        checkResponse(transport_(origin_ + file.endpoint, response, file.size, cancel, options), response);
        output.close();
        require(bool(output) && response.bytesReceived == file.size, "The download was incomplete. Refresh and try again.");
        const std::string digest = hash.hexDigest();
        require(file.hash.empty() || file.hash == digest, "The downloaded file failed SHA-256 verification.");
        file.hash = digest;
    }
    checkDescriptors(files, staging.path);
    // The existing loader names saves by file stem. Keep distinct server games from sharing saves.
    std::string stem = fs::u8path(selected).stem().u8string();
    if (stem.size() > 120) {
        size_t end = 120;
        while (end && (static_cast<unsigned char>(stem[end]) & 0xc0) == 0x80) --end;
        stem.resize(end);
    }
    const std::string launchName = stem + "-ecs-" + sha256Hex(identity.data(), identity.size()).substr(0, 16) + "." + pathExt(selected);
    const std::string launchPath = (fs::u8path(selected).parent_path() / fs::u8path(launchName)).generic_u8string();
    require(!fs::exists(staging.path / fs::u8path(launchPath)), "The game contains a conflicting launch filename.");
    fs::rename(staging.path / fs::u8path(selected), staging.path / fs::u8path(launchPath));
    for (File& file : files) {
        if (file.path == selected) file.path = launchPath;
        fingerprint += file.path + "\n" + file.hash + "\n";
    }
    selected = launchPath;
    checkCancel(cancel);
    fs::path final = base / sha256Hex(fingerprint.data(), fingerprint.size());
    if (fs::exists(final)) {
        if (cachedFilesMatch(final, base, files)) return (final / fs::u8path(selected)).u8string();
        final += "-" + staging.path.filename().string().substr(1);
    }
    std::error_code error;
    fs::rename(staging.path, final, error);
    // Another instance can publish the same immutable version while this transfer finishes.
    if (error) {
        if (fs::exists(final) && cachedFilesMatch(final, base, files)) return (final / fs::u8path(selected)).u8string();
        throw std::runtime_error("Could not publish the downloaded game. Try again.");
    }
    staging.path.clear();
    return (final / fs::u8path(selected)).u8string();
}

} // namespace vj::ecs
