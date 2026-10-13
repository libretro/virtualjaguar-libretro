#include "config.h"

#include "paths.h"

#include <cstdio>
#include <cstdlib>

namespace vj {

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

bool Config::load(const std::string& path) {
    path_ = path;
    map_.clear();
    dirty_ = false;
    std::vector<unsigned char> bytes;
    if (!readFile(path, bytes)) return true;
    std::string text(bytes.begin(), bytes.end());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        map_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return true;
}

bool Config::save() {
    if (!dirty_ || path_.empty()) return true;
    std::string out = "# Virtual Jaguar settings\n";
    for (const auto& kv : map_) out += kv.first + "=" + kv.second + "\n";
    if (!writeFileAtomic(path_, out.data(), out.size())) return false;
    dirty_ = false;
    return true;
}

std::string Config::get(const std::string& key, const std::string& def) const {
    auto it = map_.find(key);
    return it == map_.end() ? def : it->second;
}
int Config::getInt(const std::string& key, int def) const {
    auto it = map_.find(key);
    return it == map_.end() ? def : (int)std::strtol(it->second.c_str(), nullptr, 10);
}
float Config::getFloat(const std::string& key, float def) const {
    auto it = map_.find(key);
    return it == map_.end() ? def : (float)std::strtod(it->second.c_str(), nullptr);
}
bool Config::getBool(const std::string& key, bool def) const {
    auto it = map_.find(key);
    if (it == map_.end()) return def;
    return it->second == "1" || it->second == "true";
}

void Config::set(const std::string& key, const std::string& value) {
    auto it = map_.find(key);
    if (it != map_.end() && it->second == value) return;
    map_[key] = value;
    dirty_ = true;
}
void Config::setInt(const std::string& key, int v) { set(key, std::to_string(v)); }
void Config::setFloat(const std::string& key, float v) {
    char b[48];
    std::snprintf(b, sizeof b, "%g", (double)v);
    set(key, b);
}
void Config::setBool(const std::string& key, bool v) { set(key, v ? "1" : "0"); }
void Config::erase(const std::string& key) {
    if (map_.erase(key)) dirty_ = true;
}

std::vector<std::pair<std::string, std::string>> Config::withPrefix(const std::string& prefix) const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto it = map_.lower_bound(prefix); it != map_.end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) break;
        out.emplace_back(it->first.substr(prefix.size()), it->second);
    }
    return out;
}

}  // namespace vj
