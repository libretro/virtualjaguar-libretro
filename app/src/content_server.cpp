#include "content_server.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
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

std::string rommId(const Json& value) {
    const double id = value.asNumber();
    require(value.isNumber() && id >= 1 && id <= 9007199254740991.0 && std::floor(id) == id,
            "RomM returned an invalid identifier.");
    return std::to_string(static_cast<uint64_t>(id));
}

void checkCancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load()) throw std::runtime_error("Cancelled.");
}

void checkResponse(bool received, const http::Response& response) {
    if (response.status == 401) throw std::runtime_error("Access token required or expired. Check the server token.");
    if (response.status == 403) throw std::runtime_error("This token cannot read this library or game.");
    if (response.status == 404) throw std::runtime_error("This library or game is no longer available.");
    if (!received) throw std::runtime_error(response.error.empty() ? "Could not reach the server." : response.error);
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

// Saves use the opened file's stem, while states use each mounted disc's stem.
// Namespace the playlist and its discs, preserving CUE track paths within each folder.
std::string namespaceGameFiles(std::vector<File>& files, const std::string& selected,
                               const fs::path& folder, const std::string& identity) {
    const bool playlist = pathExt(selected) == "m3u";
    std::map<std::string, std::string> renamed;
    for (const File& file : files) {
        const std::string ext = pathExt(file.path);
        const bool disc = ext == "cue" || ext == "chd" || ext == "cdi" || ext == "iso";
        if (file.path != selected && !(playlist && disc)) continue;
        std::string stem = fs::u8path(file.path).stem().u8string();
        if (stem.size() > 120) {
            size_t end = 120;
            while (end && (static_cast<unsigned char>(stem[end]) & 0xc0) == 0x80) --end;
            stem.resize(end);
        }
        // Include a disc's original relative path so same-named discs in separate
        // subfolders also have distinct state keys. The key stays stable on refresh.
        const std::string key = file.path == selected ? identity : identity + "\n" + file.path;
        const std::string name = stem + "-ecs-" + sha256Hex(key.data(), key.size()).substr(0, 16) + "." + ext;
        renamed.emplace(lower(file.path), (fs::u8path(file.path).parent_path() / fs::u8path(name)).generic_u8string());
    }
    if (playlist) for (File& file : files) {
        if (pathExt(file.path) != "m3u") continue;
        std::ifstream input(folder / fs::u8path(file.path), std::ios::binary);
        const std::string original((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        require(input.good() || input.eof(), "Could not read a disc playlist.");
        input.close();
        std::string body;
        const fs::path parent = fs::u8path(file.path).parent_path();
        for (const auto& reference : parseM3u(original, "")) {
            const fs::path resolved = (parent / fs::u8path(reference)).lexically_normal();
            const auto found = renamed.find(lower(resolved.generic_u8string()));
            body += found == renamed.end() ? reference : fs::u8path(found->second).lexically_relative(parent).generic_u8string();
            body += '\n';
        }
        std::ofstream output(folder / fs::u8path(file.path), std::ios::binary | std::ios::trunc);
        output.write(body.data(), std::streamsize(body.size()));
        output.close();
        require(bool(output), "Could not update the downloaded disc playlist.");
        file.size = body.size();
        file.hash = sha256Hex(body.data(), body.size());
    }
    for (File& file : files) {
        const auto found = renamed.find(lower(file.path));
        if (found == renamed.end()) continue;
        require(!fs::exists(folder / fs::u8path(found->second)), "The game contains a conflicting launch filename.");
        fs::rename(folder / fs::u8path(file.path), folder / fs::u8path(found->second));
        file.path = found->second;
    }
    return renamed.at(lower(selected));
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

Client::Client(std::string origin, std::string token, Transport transport, Protocol protocol)
    : origin_(normalizeOrigin(origin)), token_(std::move(token)), transport_(std::move(transport)),
      protocol_(protocol) {}

Json Client::json(const std::string& path, const std::atomic<bool>* cancel, bool optional) const {
    checkCancel(cancel);
    http::Options options;
    options.bearerToken = token_;
    options.followRedirects = false;
    http::Response response;
    const bool received = transport_(origin_ + path, response, 8 * 1024 * 1024, cancel, options);
    if (optional && received && response.status == 404) return {};
    checkResponse(received, response);
    Json result;
    std::string error;
    require(Json::parse(std::string(response.body.begin(), response.body.end()), result, error) &&
                (result.isObject() || (protocol_ == Protocol::RomM && result.isArray())),
            "The server returned invalid JSON.");
    return result;
}

std::vector<Library> Client::libraries(const std::atomic<bool>* cancel) const {
    if (protocol_ == Protocol::RomM) {
        const Json result = json("/api/platforms", cancel);
        require(result.isArray(), "Invalid RomM platform list.");
        Library library{"", "RomM"};
        // Each side of the separator holds the platform IDs for one Jaguar system.
        std::string carts, discs;
        bool hasSlugs = false, gateway = false;
        for (const Json& item : result.items()) {
            gateway |= item.has("ecs_system_id");
            std::string slug = lower(item["slug"].asString());
            hasSlugs |= !slug.empty();
            slug.erase(std::remove(slug.begin(), slug.end(), '-'), slug.end());
            std::string* ids = nullptr;
            if (slug == "jaguar" || slug == "atarijaguar") ids = &carts;
            if (slug == "jaguarcd" || slug == "atarijaguarcd") ids = &discs;
            if (ids) *ids += rommId(item["id"]) + ",";
        }
        require(result.items().empty() || hasSlugs,
                "This endpoint does not expose RomM platform slugs. For an ECS gateway, use the native ECS origin.");
        library.id = (gateway ? "gateway:" : "") + carts + "|" + discs;
        return {std::move(library)};
    }
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
    if (protocol_ == Protocol::RomM) {
        const bool gateway = library.rfind("gateway:", 0) == 0;
        const size_t start = gateway ? 8 : 0;
        const auto split = library.find('|', start);
        require(split != std::string::npos, "Refresh the RomM platforms.");
        const std::string ids = system == "com.provenance.jaguar" ? library.substr(start, split - start) : library.substr(split + 1);
        if (ids.empty()) return {};
        require(ids.back() == ',' && ids.find_first_not_of("0123456789,") == std::string::npos,
                "Invalid RomM platform selection.");
        std::string filter;
        std::istringstream platforms(ids);
        for (std::string id; std::getline(platforms, id, ',');) {
            require(!id.empty(), "Invalid RomM platform selection.");
            filter += "&platform_ids=" + id;
        }
        require(cursor.size() <= 5 && cursor.find_first_not_of("0123456789") == std::string::npos,
                "Invalid RomM page offset.");
        const unsigned offset = cursor.empty() ? 0 : unsigned(std::stoul(cursor));
        const Json result = json("/api/roms?limit=100&offset=" + std::to_string(offset) + filter +
            "&with_char_index=false&with_rom_id_index=false&with_filter_values=false&with_files=false"
            "&with_total=true&search_term=" +
            lib::urlEncode(search), cancel);
        require(result["items"].isArray() && result["items"].items().size() <= 100 &&
                    result["total"].isNumber() && result["total"].asNumber() >= 0, "Invalid RomM page.");
        Page page;
        const auto& items = result["items"].items();
        const unsigned next = offset + unsigned(items.size());
        if (!items.empty() && result["total"].asNumber() > next) page.nextCursor = std::to_string(next);
        for (const Json& item : items) {
            const std::string platform = "," + rommId(item["platform_id"]) + ",";
            if (("," + ids).find(platform) == std::string::npos) continue;
            if (item["has_file_on_disk"].isBool() && !item["has_file_on_disk"].asBool()) continue;
            const std::string& name = item["name"].asString();
            page.games.push_back({rommId(item["id"]), name.empty() ? text(item["fs_name"]) : name,
                                  gateway ? "gateway" : "romm", system, Json(), item["path_cover_small"].asString()});
        }
        return page;
    }
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
        const std::string id = text(item["id"]);
        std::string artwork;
        const auto& preview = item["previewAsset"]["id"].asString();
        if (!preview.empty()) artwork = contentPath("files", preview);
        if (type == "source" && item[type]["artworkAvailable"].asBool())
            artwork = "/api/v1/sources/" + lib::urlEncode(id) + "/matches/selections/artwork/content";
        page.games.push_back({id, text(item["name"]), type, system, item[type], std::move(artwork)});
    }
    return page;
}

bool Addon::canBoot(const Game& game) const {
    return kind == "patch" && format == "ips" && game.systemId == "com.provenance.jaguar";
}

std::vector<Addon> Client::addons(const Game& game, const std::atomic<bool>* cancel) const {
    std::vector<Addon> addons;
    std::set<std::string> ids;
    const std::string id = lib::urlEncode(game.id);
    if (game.entityType == "romm") {
        const std::string path = "/api/roms/" + id;
        const Json details = json(path + "/simple", cancel);
        require(rommId(details["id"]) == game.id && details["files"].isArray() &&
                details["files"].items().size() <= kFileLimit, "Invalid RomM file list.");
        for (const auto& file : details["files"].items()) {
            const std::string category = lower(file["category"].asString());
            const std::string name = text(file["file_name"]);
            const std::string format = pathExt(name);
            Addon addon;
            if (category == "mod" || category == "hack") addon.kind = "mod";
            else if (category == "texture_pack") addon.kind = category;
            else if (category == "patch" || format == "ips" || format == "bps" || format == "ups")
                addon.kind = "patch";
            else continue;
            addon.id = rommId(file["id"]);
            addon.filename = name;
            addon.format = format;
            addon.size = sizeOf(file["file_size_bytes"]);
            addon.hash = lower(file["sha256"].asString());
            const std::string sha1 = lower(file["sha1_hash"].asString());
            if (sha1.size() == 40 && sha1.find_first_not_of("0123456789abcdef") == std::string::npos)
                addon.rommSha1 = sha1;
            require(addon.hash.empty() || hexHash(addon.hash), "Invalid add-on SHA-256.");
            addon.endpoint = path + "/content/" + lib::urlEncode(name) + "?file_ids=" + addon.id + "&purpose=download";
            if (ids.insert(addon.id).second) addons.push_back(std::move(addon));
        }
    } else {
        std::string path;
        if (game.entityType == "source") path = "/api/v1/sources/" + id + "/companions";
        else if (game.entityType == "item") path = "/api/v1/items/" + id + "/companions";
        else if (game.entityType == "gateway") path = "/api/roms/" + id + "/ecs-companions";
        else return addons;
        const Json list = json(path, cancel, true);
        if (list.isNull()) return addons;
        require(list["items"].isArray() && list["items"].items().size() <= kFileLimit, "Invalid add-on list.");
        for (const auto& item : list["items"].items()) {
            Addon addon;
            addon.id = text(item["id"]);
            addon.kind = text(item["kind"]);
            addon.filename = text(item["filename"]);
            addon.format = lower(item["format"].asString());
            if (addon.format.empty()) addon.format = pathExt(addon.filename);
            addon.size = sizeOf(item["sizeBytes"]);
            addon.hash = text(item["sha256"]);
            require(hexHash(addon.hash), "Invalid add-on SHA-256.");
            addon.endpoint = path + "/" + lib::urlEncode(addon.id) + "/content";
            require(item["contentPath"].asString() == addon.endpoint, "Invalid add-on content path.");
            const Json& base = item["base"];
            if (base.has("sizeBytes")) addon.baseSize = sizeOf(base["sizeBytes"]);
            if (base.has("sha256")) {
                addon.baseHash = lower(text(base["sha256"]));
                require(hexHash(addon.baseHash), "Invalid base SHA-256.");
            }
            if (base.has("crc32")) {
                addon.baseCrc = lower(text(base["crc32"]));
                require(addon.baseCrc.size() == 8 &&
                        addon.baseCrc.find_first_not_of("0123456789abcdef") == std::string::npos,
                        "Invalid base CRC32.");
            }
            if (ids.insert(addon.id).second) addons.push_back(std::move(addon));
        }
    }
    return addons;
}

std::vector<unsigned char> Client::artwork(const Game& game, const std::atomic<bool>* cancel) const {
    std::string path = game.artwork;
    if (path.size() > 4096) return {};
    if (protocol_ == Protocol::RomM) {
        // RomM appends a timestamp for cache invalidation, which can contain spaces.
        // Keep it in the UI cache key and send only the resource path.
        path = path.substr(0, path.find('?'));
        if (path.rfind("/assets/romm/resources/", 0) != 0) return {};
        // RomM generates PNG small covers and can advertise a derived WebP copy.
        if (pathExt(path) == "webp") path.replace(path.size() - 4, 4, "png");
        const std::string relative = path.substr(1);
        if (!safeRelativePath(relative) || relative.find('%') != std::string::npos) return {};
        path.clear();
        std::istringstream segments(relative);
        for (std::string segment; std::getline(segments, segment, '/');)
            path += "/" + lib::urlEncode(segment);
    } else if (path.rfind("/api/v1/", 0) != 0) return {};
    http::Options options;
    options.bearerToken = token_;
    options.followRedirects = false;
    http::Response response;
    if (!transport_(origin_ + path, response, 2 * 1024 * 1024, cancel, options) || response.status != 200) return {};
    return std::move(response.body);
}

namespace {
std::string downloadFiles(const std::string& origin, const std::string& token, const Transport& transport,
                          std::vector<File>& files, std::string selected, uint64_t total,
                          const std::string& cacheRoot, const std::string& identity, bool gameFiles,
                          const std::atomic<bool>* cancel,
                          const std::function<void(uint64_t, uint64_t)>& progress) {
    checkCancel(cancel);
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
        options.bearerToken = token;
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
        checkResponse(transport(origin + file.endpoint, response, file.size, cancel, options), response);
        output.close();
        require(bool(output) && response.bytesReceived == file.size, "The download was incomplete. Refresh and try again.");
        const std::string digest = hash.hexDigest();
        require(file.hash.empty() || file.hash == digest, "The downloaded file failed SHA-256 verification.");
        file.hash = digest;
    }
    if (gameFiles) {
        checkDescriptors(files, staging.path);
        selected = namespaceGameFiles(files, selected, staging.path, identity);
        checkDescriptors(files, staging.path);
    }
    for (const File& file : files) fingerprint += file.path + "\n" + file.hash + "\n";
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
} // namespace

std::string Client::download(const Game& game, const std::string& cacheRoot, const std::atomic<bool>* cancel,
                             const std::function<void(uint64_t, uint64_t)>& progress) const {
    std::vector<File> files;
    std::string selected;
    if (game.entityType == "gateway") {
        const std::string path = "/api/roms/" + lib::urlEncode(game.id);
        const Json details = json(path, cancel);
        require(rommId(details["id"]) == game.id, "Invalid ECS gateway game.");
        const std::string name = text(details["fs_name"]);
        require(details["fs_size_bytes"].isNumber(),
                "For grouped gateway downloads, add the native Emulation Server.");
        files.push_back({game.id, name, path + "/content/" + lib::urlEncode(name),
                         "", sizeOf(details["fs_size_bytes"])});
    } else if (protocol_ == Protocol::RomM) {
        require(game.entityType == "romm" && !game.id.empty() &&
                    game.id.find_first_not_of("0123456789") == std::string::npos, "Invalid RomM game.");
        const std::string path = "/api/roms/" + game.id;
        const Json details = json(path + "/simple", cancel);
        require(rommId(details["id"]) == game.id && details["files"].isArray() &&
                    details["files"].items().size() <= kFileLimit, "Invalid RomM file list.");
        const std::string romPath = text(details["fs_path"]) + "/" + text(details["fs_name"]);
        for (const Json& file : details["files"].items()) {
            const std::string category = file["category"].asString();
            if (!category.empty() && category != "game") continue;
            const std::string name = text(file["file_name"]);
            const std::string format = pathExt(name);
            if (format == "ips" || format == "bps" || format == "ups") continue;
            const std::string full = text(file["full_path"]);
            require(safeRelativePath(name) && name.find('/') == std::string::npos,
                    "The game contains unsafe file names.");
            require(full == romPath || full.rfind(romPath + "/", 0) == 0,
                    "A RomM file is outside its game folder.");
            const std::string id = rommId(file["id"]);
            files.push_back({id, full == romPath ? name : full.substr(romPath.size() + 1),
                             path + "/content/" + lib::urlEncode(name) + "?file_ids=" + id + "&purpose=play",
                             "", sizeOf(file["file_size_bytes"])});
        }
    } else if (game.entityType == "item") {
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
    const std::string identity = origin_ + "\n" + game.entityType + "\n" + game.id;
    return downloadFiles(origin_, token_, transport_, files, selected, total, cacheRoot,
                         identity, true, cancel, progress);
}

std::string Client::downloadAddon(const Addon& addon, const std::string& cacheRoot,
                                  const std::atomic<bool>* cancel,
                                  const std::function<void(uint64_t, uint64_t)>& progress) const {
    require(safeRelativePath(addon.filename) && addon.filename.find('/') == std::string::npos &&
            addon.filename.size() <= 255, "The add-on has an unsafe filename.");
    const std::string identity = origin_ + "\naddon\n" + addon.endpoint;
    std::vector<File> files{{addon.id, addon.filename, addon.endpoint, addon.hash, addon.size, false}};
    return downloadFiles(origin_, token_, transport_, files, addon.filename, addon.size, cacheRoot,
                         identity, false, cancel, progress);
}

} // namespace vj::ecs
