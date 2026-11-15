// The shared symbol table.  See symbol_store.h.
#include "symbol_store.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace vj {

namespace {

constexpr uintmax_t kMaxFileBytes = 64ull * 1024 * 1024;   // well past any cart and its symbols; keeps disc images out
constexpr uint32_t kBusMask = 0x00FFFFFFu;
constexpr uint32_t kEquateFloor = 0x1000u;                 // an absolute symbol below this is a constant, not a place

std::string fileName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool defaultReader(const std::string& path, std::vector<unsigned char>& out) {
    out.clear();
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::u8path(path);
    if (!std::filesystem::is_regular_file(p, ec) || ec) return false;
    const uintmax_t size = std::filesystem::file_size(p, ec);
    if (ec || size > kMaxFileBytes) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.resize((size_t)size);
    if (size && !f.read(reinterpret_cast<char*>(out.data()), (std::streamsize)size)) {
        out.clear();
        return false;
    }
    return true;
}

const char* whyNot(SymbolParse r) {
    switch (r) {
        case SymbolParse::NoSymbols: return "has no symbol table. Link it with rln -s (add -l for local labels).";
        case SymbolParse::Malformed: return "has a damaged symbol table.";
        default: return "is not a .cof or .abs with symbols.";
    }
}

bool isLabel(const Symbol& s) {
    if (s.value > kBusMask) return false;
    return s.kind != SymbolKind::Abs || s.value >= kEquateFloor;
}

bool equalNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// "$802000", "0x802000", "802000"; the whole string must be hex.  `plain` reports that no prefix marked it as a number.
bool parseHexAll(const std::string& text, uint32_t& out, bool* plain = nullptr) {
    size_t i = 0;
    bool prefixed = false;
    if (i < text.size() && text[i] == '$') i++, prefixed = true;
    else if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) i = 2, prefixed = true;
    if (i >= text.size() || text.size() - i > 8) return false;
    uint32_t v = 0;
    for (; i < text.size(); i++) {
        const int c = std::toupper((unsigned char)text[i]);
        if (!std::isxdigit(c)) return false;
        v = (v << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'A' + 10);
    }
    out = v;
    if (plain) *plain = !prefixed;
    return true;
}

}  // namespace

std::string symbolPathWithExtension(const std::string& path, const char* ext) {
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of("/\\");
    const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    return (hasExt ? path.substr(0, dot) : path) + ext;
}

void SymbolStore::setStatus(const std::string& m, bool err) {
    status_ = m;
    statusIsError_ = err;
}

void SymbolStore::setContent(const std::string& path) {
    if (path == contentPath_) return;
    contentPath_ = path;
    if (!pinned_) pickedPath_.clear();
    stale_ = true;
}

void SymbolStore::setPicked(const std::string& path, bool pinned) {
    pickedPath_ = path;
    pinned_ = pinned && !path.empty();
    stale_ = true;
}

void SymbolStore::dropPicked() {
    if (pinned_ || pickedPath_.empty()) return;
    pickedPath_.clear();
    stale_ = true;
}

SymbolParse SymbolStore::readSymbols(const std::string& path, std::vector<Symbol>& out) const {
    out.clear();
    std::vector<unsigned char> bytes;
    if (!(reader ? reader(path, bytes) : defaultReader(path, bytes))) return SymbolParse::NotExecutable;
    return parseSymbols(bytes.data(), bytes.size(), out);
}

void SymbolStore::adopt(const std::string& path, std::vector<Symbol>& symbols) {
    symbols_.swap(symbols);
    sourcePath_ = path;
    byAddr_.clear();
    for (size_t i = 0; i < symbols_.size(); i++)
        if (isLabel(symbols_[i])) byAddr_.emplace_back(symbols_[i].value, (uint32_t)i);
    // By address; at one address, code and data before equates, then by name -- so the label shown is stable.
    std::sort(byAddr_.begin(), byAddr_.end(), [this](const std::pair<uint32_t, uint32_t>& a, const std::pair<uint32_t, uint32_t>& b) {
        if (a.first != b.first) return a.first < b.first;
        const Symbol& x = symbols_[a.second];
        const Symbol& y = symbols_[b.second];
        const bool xa = x.kind == SymbolKind::Abs, ya = y.kind == SymbolKind::Abs;
        if (xa != ya) return !xa;
        return x.name < y.name;
    });
    char line[64];
    std::snprintf(line, sizeof line, "%zu symbols from ", symbols_.size());
    setStatus(line + fileName(path), false);
}

void SymbolStore::load() {
    stale_ = false;
    generation_++;
    symbols_.clear();
    byAddr_.clear();
    sourcePath_.clear();
    std::vector<Symbol> found;
    if (!pickedPath_.empty()) {
        const SymbolParse r = readSymbols(pickedPath_, found);
        if (r == SymbolParse::Ok) adopt(pickedPath_, found);
        else setStatus(fileName(pickedPath_) + " " + whyNot(r), true);
        return;
    }
    if (contentPath_.empty()) {
        setStatus("No game is loaded", false);
        return;
    }
    const SymbolParse own = readSymbols(contentPath_, found);
    if (own == SymbolParse::Ok) {
        adopt(contentPath_, found);
        return;
    }
    for (const char* ext : {".cof", ".COF"}) {
        const std::string beside = symbolPathWithExtension(contentPath_, ext);
        if (beside != contentPath_ && readSymbols(beside, found) == SymbolParse::Ok) {
            adopt(beside, found);
            return;
        }
    }
    const std::string name = fileName(contentPath_);
    if (own == SymbolParse::NotExecutable) setStatus(name + " carries no symbols. Load a .cof, or put one with the same name beside it.", false);
    else setStatus(name + " " + whyNot(own), own == SymbolParse::Malformed);
}

bool SymbolStore::ensure() {
    if (stale_) load();
    return !symbols_.empty();
}

bool SymbolStore::find(const std::string& name, uint32_t& value) const {
    if (name.empty()) return false;
    const Symbol* loose = nullptr;
    int looseCount = 0;
    for (const Symbol& s : symbols_) {
        if (s.name == name) {
            value = s.value;
            return true;
        }
        if (equalNoCase(s.name, name)) {
            if (!loose || loose->value != s.value) looseCount++;
            loose = &s;
        }
    }
    if (looseCount != 1) return false;
    value = loose->value;
    return true;
}

bool SymbolStore::resolve(const std::string& exprIn, uint32_t& addr) const {
    const std::string expr = trim(exprIn);
    if (expr.empty()) return false;
    uint32_t v = 0;
    bool plain = false;
    const bool hex = parseHexAll(expr, v, &plain);
    if (hex && !plain) {   // "$..." / "0x...": a number, never a name
        addr = v & kBusMask;
        return true;
    }
    // A spelling that is both ("ABCD"): the symbol wins only on an exact, case-sensitive match.
    for (const Symbol& s : symbols_)
        if (s.name == expr) {
            addr = s.value & kBusMask;
            return true;
        }
    if (hex) {
        addr = v & kBusMask;
        return true;
    }
    const size_t plus = expr.find_last_of('+');
    if (plus != std::string::npos && plus > 0) {
        uint32_t base = 0, off = 0;
        if (find(trim(expr.substr(0, plus)), base) && parseHexAll(trim(expr.substr(plus + 1)), off)) {
            addr = (base + off) & kBusMask;
            return true;
        }
    }
    if (find(expr, v)) {
        addr = v & kBusMask;
        return true;
    }
    return false;
}

const Symbol* SymbolStore::at(uint32_t addr) const {
    auto it = std::lower_bound(byAddr_.begin(), byAddr_.end(), std::make_pair(addr, (uint32_t)0));
    if (it == byAddr_.end() || it->first != addr) return nullptr;
    return &symbols_[it->second];
}

bool SymbolStore::nearest(uint32_t addr, uint32_t maxOffset, std::string& name, uint32_t& offset) const {
    auto it = std::upper_bound(byAddr_.begin(), byAddr_.end(), std::make_pair(addr, ~(uint32_t)0));
    while (it != byAddr_.begin()) {
        --it;
        if (addr - it->first > maxOffset) return false;
        const Symbol& s = symbols_[it->second];
        if (s.kind == SymbolKind::Abs) continue;   // an equate is a value, not the start of anything
        // The first entry at this address is the one at() shows: step back to it.
        auto first = it;
        while (first != byAddr_.begin() && (first - 1)->first == it->first && symbols_[(first - 1)->second].kind != SymbolKind::Abs) --first;
        name = symbols_[first->second].name;
        offset = addr - it->first;
        return true;
    }
    return false;
}

std::string SymbolStore::describe(uint32_t addr, uint32_t maxOffset) const {
    std::string name;
    uint32_t off = 0;
    if (!nearest(addr, maxOffset, name, off)) {
        const Symbol* s = at(addr);   // an equate that names exactly this place (a hardware register)
        return s ? s->name : std::string();
    }
    if (off == 0) return name;
    char buf[16];
    std::snprintf(buf, sizeof buf, "+$%X", off);
    return name + buf;
}

std::string SymbolStore::annotate(const std::string& text) const {
    if (byAddr_.empty()) return text;
    std::string names;
    int count = 0;
    for (size_t i = 0; i < text.size() && count < 2; i++) {
        if (text[i] != '$') continue;
        size_t j = i + 1;
        uint32_t v = 0;
        while (j < text.size() && std::isxdigit((unsigned char)text[j]) && j - i <= 8) {
            const int c = std::toupper((unsigned char)text[j]);
            v = (v << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'A' + 10);
            j++;
        }
        const size_t digits = j - i - 1;
        const bool ends = j >= text.size() || !std::isxdigit((unsigned char)text[j]);
        if (digits >= 5 && digits <= 8 && ends && v <= kBusMask) {
            if (const Symbol* s = at(v)) {
                if (names.find(s->name) == std::string::npos) {
                    names += names.empty() ? "" : ", ";
                    names += s->name;
                    count++;
                }
            }
        }
        i = j > i ? j - 1 : i;
    }
    return names.empty() ? text : text + "  ; " + names;
}

}  // namespace vj
