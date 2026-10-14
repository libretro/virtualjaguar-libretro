#include "library_lists.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <system_error>

#include "paths.h"

namespace fs = std::filesystem;

namespace vj {
namespace lib {

namespace {

std::string lowerStr(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string oneLine(std::string s) {
    for (char& c : s)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

bool fileInfo(const std::string& path, int64_t& mtimeSec, int64_t& size, int64_t* mtimeNs = nullptr) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return false;
    mtimeSec = (int64_t)(info.modify_time / 1000000000LL);
    size = (int64_t)info.size;
    if (mtimeNs) *mtimeNs = (int64_t)info.modify_time;
    return true;
}

}  // namespace

// ---- display options and tabs --------------------------------------------------------------------------------

int clampShowAs(int v, int def) { return v >= 0 && v < kShowAsCount ? v : def; }
bool showsTab(int mode) { return mode == ShowTab || mode == ShowBoth; }
bool showsStrip(int mode) { return mode == ShowSections || mode == ShowBoth; }

const char* showAsLabel(int mode) {
    switch (mode) {
    case ShowHidden: return "Hidden";
    case ShowTab: return "Its own tab";
    case ShowSections: return "In the Cartridges and Jaguar CD tabs";
    case ShowBoth: return "Tab, and in the Cartridges and Jaguar CD tabs";
    }
    return "";
}

std::vector<int> visibleTabs(int favoritesMode, int recentsMode) {
    std::vector<int> v;
    v.push_back(TabCart);
    v.push_back(TabDisc);
    if (showsTab(favoritesMode)) v.push_back(TabFavorites);
    if (showsTab(recentsMode)) v.push_back(TabRecents);
    v.push_back(TabStates);
    v.push_back(TabBios);
    return v;
}

int effectiveTab(int wanted, int favoritesMode, int recentsMode) {
    for (int t : visibleTabs(favoritesMode, recentsMode))
        if (t == wanted) return t;
    return TabCart;
}

const char* tabName(int tab) {
    switch (tab) {
    case TabCart: return "Cartridges";
    case TabDisc: return "Jaguar CD";
    case TabBios: return "BIOS & system";
    case TabFavorites: return "Favorites";
    case TabRecents: return "Recently played";
    case TabStates: return "Save states";
    }
    return "";
}

bool stripOnTab(int tab, int mode) { return (tab == TabCart || tab == TabDisc) && showsStrip(mode); }

// ---- personal lists ----------------------------------------------------------------------------------------------

UserItem userItemFor(const Entry& e, int64_t when) {
    UserItem it;
    it.key = coverKey(e);
    it.path = e.file.path;
    it.title = e.title;
    it.crc = e.file.crcWhole;
    it.when = when;
    return it;
}

bool sameGame(const UserItem& a, const UserItem& b) {
    if (!a.path.empty() && a.path == b.path) return true;
    if (a.crc != 0 && a.crc == b.crc) return true;
    if (!a.key.empty() && a.key == b.key) return true;
    return false;
}

std::string serializeUserLists(const UserLists& l) {
    std::string out = "# Virtual Jaguar library lists: favorites and recently played.\n"
                      "# kind <TAB> time <TAB> crc <TAB> key <TAB> path <TAB> title\n";
    auto put = [&](const char* kind, const UserItem& it) {
        char head[64];
        std::snprintf(head, sizeof head, "%s\t%lld\t%08X\t", kind, (long long)it.when, (unsigned)it.crc);
        out += head;
        out += oneLine(it.key) + "\t" + oneLine(it.path) + "\t" + oneLine(it.title) + "\n";
    };
    for (const UserItem& it : l.favorites) put("fav", it);
    for (const UserItem& it : l.recents) put("rec", it);
    return out;
}

UserLists parseUserLists(const std::string& text) {
    UserLists l;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t a = 0;
        for (int i = 0; i < 5; i++) {   // the first five fields end at a tab; the title is the rest
            size_t t = line.find('\t', a);
            if (t == std::string::npos) break;
            f.push_back(line.substr(a, t - a));
            a = t + 1;
        }
        if (f.size() < 5) continue;
        f.push_back(line.substr(a));
        UserItem it;
        it.when = (int64_t)std::strtoll(f[1].c_str(), nullptr, 10);
        it.crc = (uint32_t)std::strtoul(f[2].c_str(), nullptr, 16);
        it.key = f[3];
        it.path = f[4];
        it.title = f[5];
        if (it.key.empty() && it.path.empty() && it.crc == 0) continue;
        if (f[0] == "fav") l.favorites.push_back(it);
        else if (f[0] == "rec") l.recents.push_back(it);
    }
    return l;
}

bool hasFavorite(const UserLists& l, const UserItem& it) {
    for (const UserItem& f : l.favorites)
        if (sameGame(f, it)) return true;
    return false;
}

bool hasRecent(const UserLists& l, const UserItem& it) {
    for (const UserItem& f : l.recents)
        if (sameGame(f, it)) return true;
    return false;
}

void setFavoriteIn(UserLists& l, const UserItem& it, bool on) {
    for (size_t i = 0; i < l.favorites.size();) {
        if (sameGame(l.favorites[i], it)) {
            if (on) {   // already there: keep its place and date, refresh the handles
                UserItem& f = l.favorites[i];
                f.key = it.key;
                f.path = it.path;
                f.title = it.title;
                if (it.crc) f.crc = it.crc;
                return;
            }
            l.favorites.erase(l.favorites.begin() + (long)i);
        } else {
            i++;
        }
    }
    if (on) l.favorites.push_back(it);
}

void removeRecentIn(UserLists& l, const UserItem& it) {
    l.recents.erase(std::remove_if(l.recents.begin(), l.recents.end(), [&](const UserItem& r) { return sameGame(r, it); }),
                    l.recents.end());
}

void recordPlayIn(UserLists& l, const UserItem& it, size_t cap) {
    removeRecentIn(l, it);
    l.recents.insert(l.recents.begin(), it);
    if (l.recents.size() > cap) l.recents.resize(cap);
}

std::vector<Resolved> resolveUserItems(const std::vector<UserItem>& items, const std::vector<Entry>& entries) {
    std::map<std::string, size_t> byPath, byKey;
    std::multimap<uint32_t, size_t> byCrc;
    for (size_t i = 0; i < entries.size(); i++) {
        const Entry& e = entries[i];
        if (e.section == Section::Bios) continue;
        byPath.emplace(e.file.path, i);
        if (e.file.crcWhole) byCrc.emplace(e.file.crcWhole, i);
        byKey.emplace(coverKey(e), i);
    }
    std::vector<Resolved> out;
    std::vector<char> used(entries.size(), 0);
    for (const UserItem& it : items) {
        size_t idx = (size_t)-1;
        auto p = byPath.find(it.path);
        if (!it.path.empty() && p != byPath.end()) idx = p->second;
        if (idx == (size_t)-1 && it.crc) {
            auto r = byCrc.equal_range(it.crc);
            for (auto c = r.first; c != r.second; ++c)
                if (!used[c->second]) {   // the copy in another folder that is not already listed
                    idx = c->second;
                    break;
                }
        }
        if (idx == (size_t)-1 && !it.key.empty()) {
            auto k = byKey.find(it.key);
            if (k != byKey.end()) idx = k->second;
        }
        if (idx == (size_t)-1 || used[idx]) continue;
        used[idx] = 1;
        Resolved r;
        r.entry = &entries[idx];
        r.when = it.when;
        out.push_back(r);
    }
    return out;
}

void sortByTitle(std::vector<Resolved>& v) {
    std::stable_sort(v.begin(), v.end(), [](const Resolved& a, const Resolved& b) {
        return lowerStr(a.entry->title) < lowerStr(b.entry->title);
    });
}

std::vector<Resolved> filterBySection(const std::vector<Resolved>& v, Section s) {
    std::vector<Resolved> out;
    for (const Resolved& r : v)
        if (r.entry && r.entry->section == s) out.push_back(r);
    return out;
}

// ---- the shared file -------------------------------------------------------------------------------------------------

void UserStore::open(const std::string& file) {
    file_ = file;
    seenMtime_ = seenSize_ = -1;
    lists_ = UserLists();
    gen_++;
    read();
}

bool UserStore::read() {
    int64_t mt = 0, sz = 0;
    if (!fileInfo(file_, mt, sz)) {   // no file: no lists
        seenMtime_ = seenSize_ = -1;
        return true;
    }
    std::vector<unsigned char> bytes;
    if (!readFile(file_, bytes)) return false;   // there but unreadable: change nothing, write nothing
    lists_ = parseUserLists(std::string(bytes.begin(), bytes.end()));
    seenMtime_ = mt;
    seenSize_ = sz;
    return true;
}

bool UserStore::write() {
    std::string text = serializeUserLists(lists_);
    if (!writeFileAtomic(file_, text.data(), text.size())) return false;
    int64_t mt = 0, sz = 0;
    if (fileInfo(file_, mt, sz)) {
        seenMtime_ = mt;
        seenSize_ = sz;
    }
    return true;
}

bool UserStore::refresh() {
    if (file_.empty()) return false;
    int64_t mt = 0, sz = 0;
    const bool there = fileInfo(file_, mt, sz);
    if (!there && seenMtime_ < 0) return false;
    if (there && mt == seenMtime_ && sz == seenSize_) return false;
    UserLists before = lists_;
    if (!there) lists_ = UserLists();
    else if (!read()) return false;
    if (!there) seenMtime_ = seenSize_ = -1;
    const bool changed = serializeUserLists(before) != serializeUserLists(lists_);
    if (changed) gen_++;
    return changed;
}

template <class F> bool UserStore::apply(F&& f) {
    if (file_.empty()) return false;
    // Take what the other copies did first, then add this one change on top.
    if (!read()) return false;
    f(lists_);
    gen_++;
    return write();
}

bool UserStore::setFavorite(const UserItem& it, bool on) {
    return apply([&](UserLists& l) { setFavoriteIn(l, it, on); });
}
bool UserStore::recordPlay(const UserItem& it) {
    return apply([&](UserLists& l) { recordPlayIn(l, it); });
}
bool UserStore::removeRecent(const UserItem& it) {
    return apply([&](UserLists& l) { removeRecentIn(l, it); });
}
bool UserStore::clearRecents() {
    return apply([](UserLists& l) { l.recents.clear(); });
}
bool UserStore::clearFavorites() {
    return apply([](UserLists& l) { l.favorites.clear(); });
}

// ---- save states ------------------------------------------------------------------------------------------------------------

std::string stateFileName(const std::string& stem, int slot) {
    return slot == kAutoSlot ? stem + ".auto.state" : stem + ".slot" + std::to_string(slot) + ".state";
}
std::string thumbFileName(const std::string& stem, int slot) {
    return slot == kAutoSlot ? stem + ".auto.png" : stem + ".slot" + std::to_string(slot) + ".png";
}
std::string stateLabel(int slot) { return slot == kAutoSlot ? std::string("Auto-save") : "Slot " + std::to_string(slot); }

int clampResumeMode(int v) { return v >= 0 && v < kResumeModeCount ? v : kResumeDefault; }
const char* resumeModeLabel(int mode) {
    switch (mode) {
    case ResumeOff: return "Off";
    case ResumeAsk: return "Ask";
    case ResumeAlways: return "Always";
    }
    return "";
}
bool shouldAutoSave(int mode, bool hardcore, bool netplay, double ranSeconds, bool crashed) {
    return mode != ResumeOff && !hardcore && !netplay && !crashed && ranSeconds >= kAutoSaveMinSeconds;
}
ResumeAction resumeAction(int mode, bool hardcore, bool netplay, bool autoSaveExists) {
    if (mode == ResumeOff || hardcore || netplay || !autoSaveExists) return ResumeAction::None;
    return mode == ResumeAlways ? ResumeAction::Load : ResumeAction::Ask;
}
std::string autoStatesDir(const std::string& statesDir, int instance) {
    return instance <= 1 ? statesDir : statesDir + "instance-" + std::to_string(instance) + "/";
}

bool parseStateFileName(const std::string& name, std::string& stem, int& slot) {
    static const char ext[] = ".state";
    const size_t el = sizeof ext - 1;
    if (name.size() <= el || name.compare(name.size() - el, el, ext) != 0) return false;
    const std::string base = name.substr(0, name.size() - el);
    static const char autoExt[] = ".auto";
    const size_t al = sizeof autoExt - 1;
    if (base.size() > al && base.compare(base.size() - al, al, autoExt) == 0) {
        stem = base.substr(0, base.size() - al);
        slot = kAutoSlot;
        return true;
    }
    const size_t at = base.rfind(".slot");
    if (at == std::string::npos || at == 0) return false;
    const std::string num = base.substr(at + 5);
    if (num.empty() || num.size() > 3) return false;
    for (char c : num)
        if (c < '0' || c > '9') return false;
    stem = base.substr(0, at);
    slot = std::atoi(num.c_str());
    return true;
}

std::vector<StateGame> listStates(const std::string& dir) {
    std::map<std::string, StateGame> games;
    std::error_code ec;
    fs::directory_iterator it(fs::u8path(dir), ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const std::string name = it->path().filename().u8string();
        std::string stem;
        int slot = 0;
        if (!parseStateFileName(name, stem, slot)) continue;
        StateInfo si;
        si.slot = slot;
        si.path = dir + name;
        int64_t mt = 0, sz = 0, ns = 0;
        if (!fileInfo(si.path, mt, sz, &ns)) continue;
        si.mtime = mt;
        si.mtimeNs = ns;
        si.size = (uint64_t)sz;
        int64_t tm = 0, ts = 0;
        const std::string thumb = dir + thumbFileName(stem, slot);
        if (fileInfo(thumb, tm, ts)) si.thumbPath = thumb;
        StateGame& g = games[stem];
        g.stem = stem;
        g.states.push_back(si);
    }
    std::vector<StateGame> out;
    for (auto& kv : games) {
        StateGame& g = kv.second;
        std::sort(g.states.begin(), g.states.end(), [](const StateInfo& a, const StateInfo& b) {
            return a.mtime != b.mtime ? a.mtime > b.mtime : a.slot < b.slot;
        });
        g.newest = g.states.front().mtime;
        out.push_back(std::move(g));
    }
    std::sort(out.begin(), out.end(), [](const StateGame& a, const StateGame& b) {
        return a.newest != b.newest ? a.newest > b.newest : a.stem < b.stem;
    });
    return out;
}

std::vector<StateGame> listStatesFor(const std::string& statesDir, int instance) {
    std::vector<StateGame> root = listStates(statesDir);
    const std::string autoDir = autoStatesDir(statesDir, instance);
    if (autoDir == statesDir) return root;
    // copy N: not the first copy's auto-saves, its own
    std::map<std::string, StateGame> by;
    for (StateGame& g : root) {
        g.states.erase(std::remove_if(g.states.begin(), g.states.end(), [](const StateInfo& s) { return s.slot == kAutoSlot; }),
                       g.states.end());
        if (!g.states.empty()) by[g.stem] = g;
    }
    for (StateGame& g : listStates(autoDir)) {
        StateGame& d = by[g.stem];
        d.stem = g.stem;
        for (const StateInfo& s : g.states)
            if (s.slot == kAutoSlot) d.states.push_back(s);
        if (d.states.empty()) by.erase(g.stem);
    }
    std::vector<StateGame> out;
    for (auto& kv : by) {
        StateGame& g = kv.second;
        std::sort(g.states.begin(), g.states.end(), [](const StateInfo& a, const StateInfo& b) {
            return a.mtime != b.mtime ? a.mtime > b.mtime : a.slot < b.slot;
        });
        g.newest = g.states.front().mtime;
        out.push_back(std::move(g));
    }
    std::sort(out.begin(), out.end(), [](const StateGame& a, const StateGame& b) {
        return a.newest != b.newest ? a.newest > b.newest : a.stem < b.stem;
    });
    return out;
}

bool deleteState(const StateInfo& s) {
    int64_t mt = 0, sz = 0;
    if (!fileInfo(s.path, mt, sz)) return false;   // already gone (SDL_RemovePath calls that success)
    if (!SDL_RemovePath(s.path.c_str())) return false;
    if (!s.thumbPath.empty()) SDL_RemovePath(s.thumbPath.c_str());
    SDL_RemovePath((s.path + ".rcp").c_str());   // the RetroAchievements progress saved with the state (a missing one is fine)
    return true;
}

const Entry* entryForStem(const std::vector<Entry>& entries, const std::string& stem) {
    for (const Entry& e : entries)
        if (e.section != Section::Bios && pathStem(e.file.path) == stem) return &e;
    return nullptr;
}

// ---- file manager ---------------------------------------------------------------------------------------------------------

std::string folderUrlOf(const std::string& path) {
    size_t cut = path.find_last_of("/\\");
    if (cut == std::string::npos) return std::string();
    std::string dir = path.substr(0, cut + 1);
    for (char& c : dir)
        if (c == '\\') c = '/';
    std::string out = "file://";
    if (!dir.empty() && dir[0] != '/') out += '/';   // C:/... -> file:///C:/...
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned char c : dir) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

}  // namespace lib
}  // namespace vj
