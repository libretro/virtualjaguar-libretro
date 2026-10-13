// A blocking HTTP(S) GET, nothing more.  SDL3 has no HTTP client, and the app needs
// exactly one thing from the network: small files (box art, the game database).
//
// Backends, chosen at configure time (app/CMakeLists.txt):
//   VJ_HTTP_CURL     libcurl: macOS (in the SDK), Linux (libcurl4-openssl-dev), MinGW if installed
//   VJ_HTTP_WINHTTP  WinHTTP, part of Windows: no dependency
//   neither          get() fails with a clear error and available() is false; the library
//                    keeps working from files and cached art
// Callers run it on a worker thread; it never touches SDL or the UI.
#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace vj {
namespace http {

struct Response {
    int status = 0;                       // HTTP status, 0 if no response arrived
    std::vector<unsigned char> body;
    std::string error;                    // transport failure, empty otherwise
};

bool available();                         // a backend is compiled in
const char* backendName();                // "libcurl", "WinHTTP" or "none"

// GET `url` (http:// or https:// only), following up to 5 redirects.  Returns true when a
// response arrived, whatever its status; false on a transport error (resp.error says why),
// when the body would exceed `maxBytes`, or when `cancel` turned true.
bool get(const std::string& url, Response& resp, size_t maxBytes, const std::atomic<bool>* cancel);

}  // namespace http
}  // namespace vj
