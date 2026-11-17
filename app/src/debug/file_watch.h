// Reload-on-rebuild: notices that the loaded program's file was rewritten (a relink) and says when it is safe to load
// it again.  A linker writes its output in pieces, so a change is reported only once the file has held still for a
// couple of polls.  Pure logic over a stamp the caller reads (stampFile), so it unit-tests without a clock or a disk.
// UI thread only.
#pragma once

#include <cstdint>
#include <string>

namespace vj {

struct FileStamp {
    bool exists = false;
    uint64_t size = 0;
    int64_t mtime = 0;   // the file system's own tick count; only ever compared for equality
    bool operator==(const FileStamp& o) const { return exists == o.exists && size == o.size && mtime == o.mtime; }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};

// The stamp of `path` now (a missing or unreadable file: exists == false).  UTF-8 path.
FileStamp stampFile(const std::string& path);

class FileWatch {
public:
    static constexpr int kSettlePolls = 2;   // unchanged polls before a changed file counts as written

    // Starts (or restarts) watching `path` from `now`: what is on disk at this moment is what is loaded.
    void watch(const std::string& path, const FileStamp& now);
    void clear();
    bool active() const { return !path_.empty(); }
    const std::string& path() const { return path_; }
    // One poll with the file's current stamp.  True exactly once per rewrite: the file differs from what was loaded,
    // exists, is not empty and has not changed for kSettlePolls polls.  The new stamp then becomes the baseline.
    bool poll(const FileStamp& now);
    bool settling() const { return changing_; }   // a change was seen and is being waited out

private:
    std::string path_;
    FileStamp loaded_;     // the file as it was when it was loaded
    FileStamp last_;       // the previous poll
    bool changing_ = false;
    int still_ = 0;
};

}  // namespace vj
