#include "content_server_browser.h"

#include <algorithm>
#include <cstdio>

#include "config.h"
#include "secret_store.h"

namespace vj {
namespace {
const char* kService = "io.github.libretro.VirtualJaguar.ContentServer";
const char* kSystems[] = {"com.provenance.jaguar", "com.provenance.jaguarcd"};
}

void ContentServerBrowser::init(Config& config, const std::string& dataRoot, bool persistSettings) {
    config_ = &config;
    root_ = dataRoot + "content-server/";
    persist_ = persistSettings;
    for (const auto& entry : config.withPrefix("ecs.server.")) {
        try {
            const std::string origin = ecs::normalizeOrigin(entry.second);
            if (std::find(servers.begin(), servers.end(), origin) == servers.end() && servers.size() < 16)
                servers.push_back(origin);
        } catch (const std::exception&) {}
    }
    const std::string last = config.get("ecs.last_server", servers.empty() ? address : servers.front());
    try {
        const std::string origin = ecs::normalizeOrigin(last);
        std::snprintf(address, sizeof address, "%s", origin.c_str());
    } catch (const std::exception&) {}
}

void ContentServerBrowser::stop() {
    cancel_.store(true);
    worker_.stop();
    std::lock_guard<std::mutex> lock(mutex_);
    result_.reset();
    busy = false;
}

void ContentServerBrowser::start(std::function<Result()> task) {
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

void ContentServerBrowser::pump() {
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
        saveServer();
        if (!libraries.empty()) browse(0);
        else message = "Connected. This token has no accessible libraries.";
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

void ContentServerBrowser::addressEdited() {
    if (busy) return;
    token[0] = 0;
    tokenTouched_ = true;
    origin_.clear();
    libraries.clear();
    games.clear();
    nextCursor.clear();
    selectedLibrary = selectedGame = -1;
    message.clear();
    credentialNotice.clear();
}

void ContentServerBrowser::selectServer(size_t index) {
    if (busy || index >= servers.size()) return;
    const std::string selected = servers[index];
    addressEdited();
    std::snprintf(address, sizeof address, "%s", selected.c_str());
    std::string saved;
    if (secret::active().get(kService, selected, saved) == secret::Status::Ok)
        std::snprintf(token, sizeof token, "%s", saved.c_str());
}

void ContentServerBrowser::connect() {
    if (busy) return;
    try { origin_ = ecs::normalizeOrigin(address); }
    catch (const std::exception& error) { message = error.what(); return; }
    if (!http::available()) { message = "This build has no HTTP support."; return; }
    credentialNotice.clear();
    if (!tokenTouched_ && !token[0] && std::find(servers.begin(), servers.end(), origin_) != servers.end()) {
        std::string saved;
        if (secret::active().get(kService, origin_, saved) == secret::Status::Ok)
            std::snprintf(token, sizeof token, "%s", saved.c_str());
    }
    libraries.clear();
    games.clear();
    nextCursor.clear();
    selectedLibrary = selectedGame = -1;
    const ecs::Client client(origin_, token);
    start([this, client] {
        Result result;
        result.kind = Kind::Libraries;
        result.libraries = client.libraries(&cancel_);
        return result;
    });
}

void ContentServerBrowser::saveServer() {
    if (std::find(servers.begin(), servers.end(), origin_) == servers.end()) {
        if (servers.size() >= 16) servers.erase(servers.begin());
        servers.push_back(origin_);
    }
    std::snprintf(address, sizeof address, "%s", origin_.c_str());
    if (!persist_ || !config_) return;
    for (const auto& entry : config_->withPrefix("ecs.server.")) config_->erase("ecs.server." + entry.first);
    for (size_t i = 0; i < servers.size(); ++i) config_->set("ecs.server." + std::to_string(i), servers[i]);
    config_->set("ecs.last_server", origin_);
    config_->save();
    if (rememberToken && token[0]) {
        if (secret::active().set(kService, origin_, token) != secret::Status::Ok)
            credentialNotice = "Credential storage unavailable. Token kept for this session.";
    } else if (secret::active().erase(kService, origin_) == secret::Status::Unavailable) {
        credentialNotice = "Credential storage unavailable. A previously saved token could not be removed.";
    }
}

void ContentServerBrowser::forget() {
    if (busy) return;
    std::string origin;
    try { origin = ecs::normalizeOrigin(address); }
    catch (const std::exception& error) { message = error.what(); return; }
    if (persist_ && secret::active().erase(kService, origin) == secret::Status::Unavailable) {
        message = "Credential storage unavailable. Try removing this server again when it is unlocked.";
        return;
    }
    servers.erase(std::remove(servers.begin(), servers.end(), origin), servers.end());
    addressEdited();
    if (persist_ && config_) {
        for (const auto& entry : config_->withPrefix("ecs.server.")) config_->erase("ecs.server." + entry.first);
        for (size_t i = 0; i < servers.size(); ++i) config_->set("ecs.server." + std::to_string(i), servers[i]);
        config_->erase("ecs.last_server");
        config_->save();
    }
    message = "Server removed. Downloaded games remain in the local cache.";
}

void ContentServerBrowser::browse(int library, bool append) {
    if (busy || library < 0 || size_t(library) >= libraries.size() || system < 0 || system > 1) return;
    if (append && (selectedLibrary != library || activeSearch_ != search || activeSystem_ != kSystems[system])) append = false;
    selectedLibrary = library;
    activeSearch_ = search;
    activeSystem_ = kSystems[system];
    const std::string cursor = append ? nextCursor : "";
    if (!append) { games.clear(); nextCursor.clear(); selectedGame = -1; }
    const std::string id = libraries[size_t(library)].id, query = activeSearch_, systemId = activeSystem_;
    const ecs::Client client(origin_, token);
    start([this, client, id, query, systemId, cursor, append] {
        Result result;
        result.kind = Kind::Games;
        result.append = append;
        result.page = client.games(id, query, systemId, cursor, &cancel_);
        return result;
    });
}

void ContentServerBrowser::download(int index) {
    if (busy || index < 0 || size_t(index) >= games.size()) return;
    received_ = total_ = 0;
    readyPath.clear();
    downloading = true;
    const ecs::Client client(origin_, token);
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

} // namespace vj
