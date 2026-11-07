#pragma once

#include <mutex>
#include <optional>

#include "content_server.h"
#include "library_service.h"

namespace vj {

class Config;

// UI state is owned by the main thread. The worker delivers one result through pump().
class ContentServerBrowser {
public:
    ~ContentServerBrowser() { stop(); }
    void init(Config& config, const std::string& dataRoot, bool persistSettings);
    void stop();
    void pump();
    void selectServer(size_t index);
    void addressEdited();
    void tokenEdited() { tokenTouched_ = true; }
    void connect();
    void forget();
    void browse(int library, bool append = false);
    void download(int game);
    void cancel() { cancel_.store(true); }

    bool show = false, busy = false, downloading = false, rememberToken = true;
    bool launchReady = false;
    int selectedLibrary = -1, selectedGame = -1, system = 0;
    char address[512] = "http://127.0.0.1:8473";
    char token[2048] = {};
    char search[257] = {};
    std::string message, credentialNotice, nextCursor, readyPath;
    std::vector<std::string> servers;
    std::vector<ecs::Library> libraries;
    std::vector<ecs::Game> games;
    uint64_t received() const { return received_.load(); }
    uint64_t total() const { return total_.load(); }

private:
    enum class Kind { Libraries, Games, Download, Error };
    struct Result {
        Kind kind = Kind::Error;
        std::vector<ecs::Library> libraries;
        ecs::Page page;
        std::string path, error;
        bool append = false;
    };
    void start(std::function<Result()> task);
    void saveServer();
    Config* config_ = nullptr;
    std::string root_, origin_, activeSearch_, activeSystem_;
    bool persist_ = false, tokenTouched_ = false;
    Worker worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<uint64_t> received_{0}, total_{0};
    std::mutex mutex_;
    std::optional<Result> result_;
};

class App;
void drawContentServers(App& app);

} // namespace vj
