#include "content_server_browser.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "secret_store.h"

namespace vj {
namespace {
const char* kService = "io.github.libretro.VirtualJaguar.ContentServer";
const char* kSystems[] = {"com.provenance.jaguar", "com.provenance.jaguarcd"};
}

ContentServerTab::ContentServerTab(std::string label, std::string server, ecs::Protocol kind, std::string root)
    : name(std::move(label)), origin(std::move(server)), protocol(kind), root_(std::move(root)) {}

std::string ContentServerTab::credentialKey() const {
    return protocol == ecs::Protocol::Ecs ? origin : "romm:" + origin;
}

void ContentServerTab::stop() {
    cancel_.store(true);
    worker_.stop();
    clearCovers();
    std::lock_guard<std::mutex> lock(mutex_);
    result_.reset();
    busy = false;
}

void ContentServerTab::start(std::function<Result()> task) {
    if (busy) return;
    busy = true;
    message.clear();
    cancel_.store(false);
    worker_.start();
    worker_.post([this, task = std::move(task)] {
        Result result;
        try { result = task(); }
        catch (const std::exception& error) { result.error = error.what(); }
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = std::move(result);
    });
}

void ContentServerTab::pump() {
    std::optional<Result> result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result.swap(result_);
    }
    if (!result) return;
    busy = downloading = false;
    if (cancel_.load()) { message = "Cancelled."; return; }
    if (!result->error.empty()) { message = result->error; return; }
    switch (result->kind) {
    case Kind::Libraries:
        libraries = std::move(result->libraries);
        if (!libraries.empty()) {
            const auto current = std::find_if(libraries.begin(), libraries.end(), [&](const ecs::Library& library) {
                return library.id == libraryId;
            });
            browse(current == libraries.end() ? 0 : int(current - libraries.begin()));
        } else {
            games.clear();
            nextCursor.clear();
            selectedLibrary = selectedGame = -1;
            message = "Connected. This token has no accessible libraries.";
        }
        break;
    case Kind::Games:
        if (!result->append) { games.clear(); selectedGame = -1; }
        for (auto& game : result->page.games) {
            const auto duplicate = std::find_if(games.begin(), games.end(), [&](const ecs::Game& other) {
                return game.id == other.id && game.entityType == other.entityType;
            });
            if (duplicate == games.end() && games.size() < 10000) games.push_back(std::move(game));
        }
        nextCursor = games.size() < 10000 ? result->page.nextCursor : "";
        message = games.empty() ? "No matching games in this library." : std::to_string(games.size()) + " games";
        break;
    case Kind::Download:
        readyPath = std::move(result->path);
        launchReady = true;
        message = "Download complete.";
        break;
    case Kind::Error:
        message = "The server request failed.";
        break;
    }
}

void ContentServerTab::connect() {
    if (busy) return;
    opened = true;
    if (!http::available()) { message = "This build has no HTTP support."; return; }
    clearCovers();
    if (activeToken_ != token) {
        libraries.clear();
        games.clear();
        nextCursor.clear();
        readyPath.clear();
        selectedLibrary = selectedGame = -1;
        launchReady = false;
        activeToken_ = token;
    }
    const ecs::Client client(origin, activeToken_, http::get, protocol);
    start([this, client] {
        Result result;
        result.kind = Kind::Libraries;
        result.libraries = client.libraries(&cancel_);
        return result;
    });
}

void ContentServerTab::browse(int library, bool append) {
    if (busy || library < 0 || size_t(library) >= libraries.size() || system < 0 || system > 1) return;
    if (append && nextCursor.empty()) return;
    if (append && (selectedLibrary != library || activeSearch_ != search || activeSystem_ != kSystems[system])) append = false;
    selectedLibrary = library;
    libraryId = libraries[size_t(library)].id;
    activeSearch_ = search;
    activeSystem_ = kSystems[system];
    const std::string cursor = append ? nextCursor : "";
    if (!append) { games.clear(); nextCursor.clear(); selectedGame = -1; }
    const std::string id = libraries[size_t(library)].id, query = activeSearch_, systemId = activeSystem_;
    const ecs::Client client(origin, activeToken_, http::get, protocol);
    start([this, client, id, query, systemId, cursor, append] {
        Result result;
        result.kind = Kind::Games;
        result.append = append;
        result.page = client.games(id, query, systemId, cursor, &cancel_);
        return result;
    });
}

void ContentServerTab::download(int index) {
    if (busy || index < 0 || size_t(index) >= games.size()) return;
    received_ = total_ = 0;
    readyPath.clear();
    launchReady = false;
    downloading = true;
    const ecs::Client client(origin, activeToken_, http::get, protocol);
    const ecs::Game game = games[size_t(index)];
    start([this, client, game] {
        Result result;
        result.kind = Kind::Download;
        result.path = client.download(game, root_, &cancel_, [this](uint64_t received, uint64_t total) {
            received_.store(received);
            total_.store(total);
        });
        return result;
    });
}

void ContentServerTab::clearCovers() {
    artCancel_.store(true);
    artWorker_.stop();
    for (const auto& cover : covers_)
        if (cover.second.view.tex && destroyTexture_) destroyTexture_(cover.second.view.tex);
    covers_.clear();
    pendingCover_.clear();
    coverResult_.reset();
}

ArtView ContentServerTab::cover(const ecs::Game& game, LibraryService& library) {
    if (game.artwork.empty() || !library.createTexture || !library.destroyTexture) return {};
    if (!pendingCover_.empty()) {
        std::optional<CoverResult> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result.swap(coverResult_);
        }
        if (result) {
            destroyTexture_ = library.destroyTexture;
            if (covers_.size() >= 64) {
                const auto oldest = std::min_element(covers_.begin(), covers_.end(), [](const auto& a, const auto& b) {
                    return a.second.used < b.second.used;
                });
                if (oldest->second.view.tex) destroyTexture_(oldest->second.view.tex);
                covers_.erase(oldest);
            }
            auto& cover = covers_[pendingCover_];
            cover.used = ++coverSequence_;
            if (!result->rgba.empty()) {
                const unsigned texture = library.createTexture(result->rgba.data(), result->w, result->h);
                cover.view = {ArtView::Ready, texture, result->w, result->h};
            }
            pendingCover_.clear();
        }
    }
    const auto found = covers_.find(game.artwork);
    if (found != covers_.end()) {
        found->second.used = ++coverSequence_;
        return found->second.view;
    }
    if (busy || !pendingCover_.empty()) return {};
    pendingCover_ = game.artwork;
    artCancel_.store(false);
    artWorker_.start();
    const ecs::Client client(origin, activeToken_, http::get, protocol);
    artWorker_.post([this, client, game] {
        CoverResult result;
        try {
            const auto bytes = client.artwork(game, &artCancel_);
            // Read PNG dimensions before decoding to bound allocations from remote covers.
            const auto dimension = [&](size_t i) {
                return (uint32_t(bytes[i]) << 24) | (uint32_t(bytes[i + 1]) << 16) |
                       (uint32_t(bytes[i + 2]) << 8) | bytes[i + 3];
            };
            if (bytes.size() >= 24 && std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16) == 0 &&
                dimension(16) > 0 && dimension(16) <= 2048 && dimension(20) > 0 && dimension(20) <= 2048) {
                SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
                using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
                Surface source(io ? SDL_LoadPNG_IO(io, true) : nullptr, SDL_DestroySurface);
                if (source) {
                    const int side = std::max({128, source->w, source->h});
                    Surface thumb(SDL_ScaleSurface(source.get(), std::max(1, source->w * 128 / side),
                        std::max(1, source->h * 128 / side), SDL_SCALEMODE_LINEAR), SDL_DestroySurface);
                    Surface rgba(thumb ? SDL_ConvertSurface(thumb.get(), SDL_PIXELFORMAT_RGBA32) : nullptr, SDL_DestroySurface);
                    if (rgba) {
                        result.w = rgba->w;
                        result.h = rgba->h;
                        result.rgba.resize(size_t(rgba->w) * rgba->h * 4);
                        for (int y = 0; y < rgba->h; ++y)
                            std::memcpy(result.rgba.data() + size_t(y) * rgba->w * 4,
                                        static_cast<const unsigned char*>(rgba->pixels) + size_t(y) * rgba->pitch,
                                        size_t(rgba->w) * 4);
                    }
                }
            }
        } catch (const std::exception&) {}
        std::lock_guard<std::mutex> lock(mutex_);
        coverResult_ = std::move(result);
    });
    return {};
}

void ContentServerBrowser::init(Config& config, const std::string& dataRoot, bool persistSettings) {
    config_ = &config;
    root_ = dataRoot + "content-server/";
    persist_ = persistSettings;
    const auto restore = [&](const std::string& label, const std::string& address, ecs::Protocol protocol) -> ContentServerTab* {
        try {
            const std::string origin = ecs::normalizeOrigin(address);
            if (tabs.size() >= 16 || label.empty() || label.size() >= sizeof name) return nullptr;
            for (const auto& tab : tabs)
                if (tab->origin == origin && tab->protocol == protocol) return nullptr;
            auto tab = std::make_unique<ContentServerTab>(label, origin, protocol, root_);
            std::string saved;
            if (secret::active().get(kService, tab->credentialKey(), saved) == secret::Status::Ok)
                std::snprintf(tab->token, sizeof tab->token, "%s", saved.c_str());
            tabs.push_back(std::move(tab));
            return tabs.back().get();
        } catch (const std::exception&) { return nullptr; }
    };
    neuro::Json settings;
    std::string error;
    if (neuro::Json::parse(config.get("content_servers", ""), settings, error) && settings.isArray()) {
        for (const auto& item : settings.items()) {
            const auto& kind = item["protocol"].asString();
            if (kind != "ecs" && kind != "romm") continue;
            auto* tab = restore(item["name"].asString(), item["origin"].asString(),
                                kind == "romm" ? ecs::Protocol::RomM : ecs::Protocol::Ecs);
            if (!tab) continue;
            tab->system = item["system"].asNumber() == 1 ? 1 : 0;
            tab->libraryId = item["library"].asString();
            std::snprintf(tab->search, sizeof tab->search, "%s", item["search"].asString().c_str());
            tab->activeSearch_ = tab->search;
        }
    } else {
        for (const auto& entry : config.withPrefix("ecs.server."))
            restore(entry.second.substr(0, sizeof name - 1), entry.second, ecs::Protocol::Ecs);
    }
}

void ContentServerBrowser::stop() {
    for (auto& tab : tabs) tab->stop();
}

bool ContentServerBrowser::add() {
    std::string origin;
    try { origin = ecs::normalizeOrigin(address); }
    catch (const std::exception& error) { message = error.what(); return false; }
    if (!name[0]) { message = "Enter a server name."; return false; }
    const auto kind = protocol == 1 ? ecs::Protocol::RomM : ecs::Protocol::Ecs;
    for (const auto& tab : tabs) {
        if (tab->origin != origin || tab->protocol != kind) continue;
        message = "This server already has a tab.";
        return false;
    }
    if (tabs.size() >= 16) { message = "Remove a server before adding another."; return false; }
    tabs.push_back(std::make_unique<ContentServerTab>(name, origin, kind, root_));
    // Allow the user to enter a token before sending the first request.
    tabs.back()->opened = true;
    name[0] = address[0] = 0;
    message.clear();
    save();
    return true;
}

bool ContentServerBrowser::forget(size_t index) {
    auto& tab = tabs.at(index);
    if (persist_ && secret::active().erase(kService, tab->credentialKey()) == secret::Status::Unavailable) {
        tab->message = "Credential storage unavailable. Try removing this server again when it is unlocked.";
        return false;
    }
    tabs.erase(tabs.begin() + index);
    save();
    return true;
}

void ContentServerBrowser::save() {
    if (!persist_ || !config_) return;
    using neuro::Json;
    Json settings = Json::array();
    for (const auto& tab : tabs) {
        Json item = Json::object();
        item.set("name", Json::string(tab->name)).set("origin", Json::string(tab->origin))
            .set("protocol", Json::string(tab->protocol == ecs::Protocol::Ecs ? "ecs" : "romm"))
            .set("system", Json::number(tab->system)).set("search", Json::string(tab->activeSearch_));
        if (tab->protocol == ecs::Protocol::Ecs) item.set("library", Json::string(tab->libraryId));
        settings.push(std::move(item));
    }
    config_->set("content_servers", settings.dump());
    for (const auto& entry : config_->withPrefix("ecs.server.")) config_->erase("ecs.server." + entry.first);
    config_->erase("ecs.last_server");
    config_->save();
}

void ContentServerBrowser::saveToken(ContentServerTab& tab) {
    if (!persist_) return;
    auto& store = secret::active();
    tab.credentialNotice.clear();
    if (tab.rememberToken && tab.token[0]) {
        if (store.set(kService, tab.credentialKey(), tab.token) != secret::Status::Ok)
            tab.credentialNotice = "Credential storage unavailable. Token kept for this session.";
    } else if (store.erase(kService, tab.credentialKey()) == secret::Status::Unavailable) {
        tab.credentialNotice = "Credential storage unavailable. A previously saved token could not be removed.";
    }
}

} // namespace vj
