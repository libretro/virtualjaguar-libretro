#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "http.h"
#include "neuro/neuro_json.h"

namespace vj::ecs {

struct Library {
    std::string id, name;
};

struct Game {
    std::string id, name, entityType, systemId;
    neuro::Json payload;
};

struct Page {
    std::vector<Game> games;
    std::string nextCursor;
};

// Server origins and remote paths are validated before credentials or local files are used.
std::string normalizeOrigin(const std::string& address);
bool safeRelativePath(const std::string& path);

using Transport = std::function<bool(const std::string&, http::Response&, uint64_t,
                                     const std::atomic<bool>*, const http::Options&)>;

class Client {
public:
    Client(std::string origin, std::string token, Transport transport = http::get);
    std::vector<Library> libraries(const std::atomic<bool>* cancel = nullptr) const;
    Page games(const std::string& library, const std::string& search, const std::string& system,
               const std::string& cursor, const std::atomic<bool>* cancel = nullptr) const;
    // Immutable local cache. A cancelled or failed transfer never publishes a launchable game.
    std::string download(const Game& game, const std::string& cacheRoot, const std::atomic<bool>* cancel,
                         const std::function<void(uint64_t, uint64_t)>& progress = {}) const;

private:
    neuro::Json json(const std::string& path, const std::atomic<bool>* cancel) const;
    std::string origin_, token_;
    Transport transport_;
};

} // namespace vj::ecs
