#include "present_util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

#include "paths.h"

namespace vj {

namespace fs = std::filesystem;

// ---- placement ---------------------------------------------------------------------------

PresentRect computePresentRect(float ax, float ay, float aw, float ah, unsigned srcW, unsigned srcH,
                               float coreAspect, const PresentOptions& opt) {
    PresentRect r;
    if (srcW == 0 || srcH == 0 || aw < 1 || ah < 1) {
        r.x = ax;
        r.y = ay;
        return r;
    }
    float aspect = opt.aspectOverride > 0.0f ? opt.aspectOverride
                 : coreAspect > 0.0f ? coreAspect : (float)srcW / (float)srcH;
    float dw, dh;
    if (opt.stretch) {
        dw = aw;
        dh = ah;
    } else {
        dh = ah;
        dw = dh * aspect;
        if (dw > aw) {
            dw = aw;
            dh = dw / aspect;
        }
        if (opt.integerScale) {
            // Largest whole multiple of the source height that fits, never below 1x
            // unless the window is smaller than the source.
            float k = std::floor(dh / (float)srcH);
            if (k >= 1.0f) {
                dh = k * (float)srcH;
                dw = dh * aspect;
                if (dw > aw) {
                    float kw = std::floor(aw / (aspect * (float)srcH));
                    if (kw >= 1.0f) {
                        dh = kw * (float)srcH;
                        dw = dh * aspect;
                    }
                }
            }
        }
    }
    r.x = std::floor(ax + (aw - dw) * 0.5f);
    r.y = std::floor(ay + (ah - dh) * 0.5f);
    r.w = dw;
    r.h = dh;
    return r;
}

// ---- presets -----------------------------------------------------------------------------

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// value of `key = value`: quoted values keep everything between the quotes,
// bare values stop at a " #" comment.
std::string unquote(const std::string& v) {
    std::string t = trim(v);
    if (!t.empty() && (t[0] == '"' || t[0] == '\'')) {
        size_t e = t.find(t[0], 1);
        return e == std::string::npos ? t.substr(1) : t.substr(1, e - 1);
    }
    size_t c = t.find(" #");
    if (c != std::string::npos) t = trim(t.substr(0, c));
    return t;
}

bool isAbsolute(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return p.size() > 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':';
}

std::string resolveRel(const std::string& baseDir, const std::string& rel) {
    if (isAbsolute(rel)) return rel;
    return baseDir + rel;
}

struct Entry {
    std::string value;
    std::string baseDir;   // directory of the file that defined the key
};

struct Parse {
    std::map<std::string, Entry> keys;
    std::vector<std::string> references;
    std::set<std::string> visiting;
    std::string error;
};

bool parseFile(const std::string& path, Parse& ps, int depth) {
    if (depth > 8) {
        ps.error = "#reference chain is nested more than 8 deep at " + path;
        return false;
    }
    std::error_code ec;
    std::string canon = fs::weakly_canonical(fs::u8path(path), ec).string();
    if (ec) canon = path;
    if (!ps.visiting.insert(canon).second) {
        ps.error = "#reference loop at " + path;
        return false;
    }
    std::vector<unsigned char> bytes;
    if (!readFile(path, bytes)) {
        ps.error = "cannot read " + path;
        return false;
    }
    std::string text(bytes.begin(), bytes.end());
    std::string dir = pathDir(path);
    std::map<std::string, Entry> mine;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.compare(0, 10, "#reference") == 0) {
                std::string ref = unquote(line.substr(10));
                if (ref.empty()) continue;
                std::string rp = resolveRel(dir, ref);
                ps.references.push_back(rp);
                if (!parseFile(rp, ps, depth + 1)) return false;
            }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        if (k.empty()) continue;
        mine[k] = Entry{unquote(line.substr(eq + 1)), dir};
    }
    // Keys of the referencing preset win over the referenced one's.
    for (auto& kv : mine) ps.keys[kv.first] = kv.second;
    ps.visiting.erase(canon);
    return true;
}

}  // namespace

bool hasPresetExtension(const std::string& path) { return pathExt(path) == "slangp"; }

PresetInfo inspectPreset(const std::string& path) {
    PresetInfo info;
    info.path = path;
    try {
        if (path.empty()) {
            info.error = "no preset selected";
            return info;
        }
        std::error_code ec;
        fs::path p = fs::u8path(path);
        if (!fs::exists(p, ec)) {
            info.error = "preset not found: " + path;
            return info;
        }
        if (!fs::is_regular_file(p, ec)) {
            info.error = "not a preset file: " + path;
            return info;
        }
        if (!hasPresetExtension(path)) {
            std::string e = pathExt(path);
            info.error = (e == "glslp" || e == "cgp")
                             ? "only .slangp (Slang) presets are supported, not ." + e
                             : "not a .slangp preset: " + path;
            return info;
        }
        Parse ps;
        if (!parseFile(path, ps, 0)) {
            info.error = ps.error;
            return info;
        }
        info.references = ps.references;
        auto it = ps.keys.find("shaders");
        if (it == ps.keys.end()) {
            info.error = "preset has no 'shaders = N' line";
            return info;
        }
        int n = (int)std::strtol(it->second.value.c_str(), nullptr, 10);
        if (n < 1 || n > 64) {
            info.error = "preset says shaders = " + it->second.value + " (expected 1 to 64)";
            return info;
        }
        for (int i = 0; i < n; i++) {
            auto s = ps.keys.find("shader" + std::to_string(i));
            if (s == ps.keys.end() || s->second.value.empty()) {
                info.error = "pass " + std::to_string(i) + " has no shader" + std::to_string(i) + " line";
                return info;
            }
            std::string sp = resolveRel(s->second.baseDir, s->second.value);
            if (!fs::is_regular_file(fs::u8path(sp), ec)) {
                info.error = "pass " + std::to_string(i) + ": shader file not found: " + sp;
                return info;
            }
            info.shaders.push_back(sp);
        }
        auto t = ps.keys.find("textures");
        if (t != ps.keys.end()) {
            std::string list = t->second.value;
            size_t a = 0;
            while (a <= list.size()) {
                size_t b = list.find(';', a);
                std::string name = trim(list.substr(a, b == std::string::npos ? std::string::npos : b - a));
                a = b == std::string::npos ? list.size() + 1 : b + 1;
                if (name.empty()) continue;
                auto f = ps.keys.find(name);
                if (f == ps.keys.end()) {
                    info.error = "texture '" + name + "' is listed but has no path";
                    return info;
                }
                std::string tp = resolveRel(f->second.baseDir, f->second.value);
                if (!fs::is_regular_file(fs::u8path(tp), ec)) {
                    info.error = "texture '" + name + "' not found: " + tp;
                    return info;
                }
                info.textures.push_back(tp);
            }
        }
        info.ok = true;
    } catch (const std::exception& e) {
        info.ok = false;
        info.error = std::string("cannot inspect preset: ") + e.what();
    }
    return info;
}

std::vector<std::string> scanPresets(const std::string& root, size_t maxResults, int maxDepth, bool* truncated) {
    std::vector<std::string> out;
    if (truncated) *truncated = false;
    try {
        std::error_code ec;
        fs::path r = fs::u8path(root);
        if (root.empty() || !fs::is_directory(r, ec)) return out;
        fs::recursive_directory_iterator it(r, fs::directory_options::skip_permission_denied, ec), end;
        while (!ec && it != end) {
            const fs::directory_entry& de = *it;
            std::error_code e2;
            if (de.is_directory(e2)) {
                std::string name = de.path().filename().string();
                if (name == ".git" || name == ".github" || it.depth() >= maxDepth) {
                    if (it.depth() >= maxDepth && name != ".git" && truncated) *truncated = true;
                    it.disable_recursion_pending();
                }
            } else if (de.is_regular_file(e2) && hasPresetExtension(de.path().string())) {
                if (out.size() >= maxResults) {
                    if (truncated) *truncated = true;
                    break;
                }
                std::string rel = fs::relative(de.path(), r, e2).generic_string();
                if (!e2) out.push_back(rel);
            }
            it.increment(ec);
        }
    } catch (const std::exception&) {
        // a directory vanishing mid-walk is not worth a crash
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace vj
