// A tiny key=value settings file.  Deliberately boring: one line per key, no
// sections, '#' comments, values never contain newlines.  App settings use
// plain keys (video.integer), core option overrides use the prefix "opt."
// followed by the libretro option key, recents use "recent.N".
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace vj {

class Config {
public:
    bool load(const std::string& path);   // missing file is not an error
    bool save();                          // no-op unless dirty
    void setPath(const std::string& p) { path_ = p; }
    // Merges the keys of another file over what is loaded (no dirty flag; a missing file is fine).
    void overlay(const std::string& path);
    // Several running copies (instance.h): a secondary copy writes ONLY keys with one of these
    // prefixes, to its own file (setPath), never the whole map -- so it can neither overwrite the
    // shared config.ini nor freeze a stale snapshot of it.  Empty list = write everything (default).
    void setSavePrefixes(std::vector<std::string> p) { savePrefixes_ = std::move(p); }

    std::string get(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def) const;
    float getFloat(const std::string& key, float def) const;
    bool getBool(const std::string& key, bool def) const;

    void set(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int v);
    void setFloat(const std::string& key, float v);
    void setBool(const std::string& key, bool v);
    void erase(const std::string& key);

    bool has(const std::string& key) const { return map_.count(key) != 0; }
    // All key/value pairs whose key starts with `prefix` (prefix stripped).
    std::vector<std::pair<std::string, std::string>> withPrefix(const std::string& prefix) const;

private:
    std::string path_;
    std::map<std::string, std::string> map_;
    std::vector<std::string> savePrefixes_;
    bool dirty_ = false;
};

}  // namespace vj
