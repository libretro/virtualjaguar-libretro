// Blocking HTTP(S) GET with bounded memory or streamed output. Workers use it for
// box art, the game database, and content-server game downloads.
//
// Backends, chosen at configure time (app/CMakeLists.txt):
//   VJ_HTTP_CURL     libcurl: macOS (in the SDK), Linux (libcurl4-openssl-dev), MinGW if installed
//   VJ_HTTP_WINHTTP  WinHTTP, part of Windows: no dependency
//   neither          get() fails with a clear error and available() is false; the library
//                    keeps working from files and cached art
// Callers run it on a worker thread; it never touches SDL or the UI.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vj {
namespace http {

struct Response {
    int status = 0;                       // HTTP status, 0 if no response arrived
    std::vector<unsigned char> body;
    std::string error;                    // transport failure, empty otherwise
    uint64_t bytesReceived = 0;
};

struct Options {
    std::string bearerToken;
    bool followRedirects = true;
    // Optional streaming sink. Returning false aborts the request. Runs on the caller's worker.
    std::function<bool(const void*, size_t)> write;
};

bool available();                         // a backend is compiled in
const char* backendName();                // "libcurl", "WinHTTP" or "none"

// GET `url` (http:// or https:// only), following up to 5 redirects.  Returns true when a
// response arrived, whatever its status; false on a transport error (resp.error says why),
// when the body would exceed `maxBytes`, or when `cancel` turned true.
// Credentialed requests never follow redirects. Streaming responses leave body empty.
bool get(const std::string& url, Response& resp, uint64_t maxBytes, const std::atomic<bool>* cancel,
         const Options& options = {});

}  // namespace http
}  // namespace vj
