#include "file_watch.h"

#include <filesystem>
#include <system_error>

namespace vj {

FileStamp stampFile(const std::string& path) {
    FileStamp s;
    if (path.empty()) return s;
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::u8path(path);
    if (!std::filesystem::is_regular_file(p, ec) || ec) return s;
    const uintmax_t size = std::filesystem::file_size(p, ec);
    if (ec) return s;
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return s;
    s.exists = true;
    s.size = (uint64_t)size;
    s.mtime = (int64_t)t.time_since_epoch().count();
    return s;
}

void FileWatch::watch(const std::string& path, const FileStamp& now) {
    path_ = path;
    loaded_ = last_ = now;
    changing_ = false;
    still_ = 0;
}

void FileWatch::clear() { watch(std::string(), FileStamp()); }

bool FileWatch::poll(const FileStamp& now) {
    if (path_.empty()) return false;
    if (now != last_) {   // still being written (or just replaced): start counting again
        last_ = now;
        still_ = 0;
        changing_ = now != loaded_;
        return false;
    }
    if (!changing_) return false;
    // A file the build deleted and has not written back yet, or an empty one, is not something to load.
    if (!now.exists || now.size == 0) return false;
    if (++still_ < kSettlePolls) return false;
    loaded_ = now;
    changing_ = false;
    still_ = 0;
    return true;
}

}  // namespace vj
