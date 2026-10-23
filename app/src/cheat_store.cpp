#include "cheat_store.h"

#include "cheats.h"
#include "config.h"
#include "neuro/neuro_json.h"
#include "paths.h"
#include "sha256.h"

#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>

namespace vj {

namespace fs = std::filesystem;
using neuro::Json;

namespace {

std::string crcHex(uint32_t crc) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", (unsigned)crc);
    return b;
}

bool hexDigits(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (unsigned char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

uint32_t parseCrc(const std::string& hex) { return (uint32_t)std::strtoul(hex.c_str(), nullptr, 16); }

// A pack file is a plain relative path under the store base: no scheme, no drive, no "..", no backslash.
bool safeRelativeFile(const std::string& f) {
    if (f.empty() || f.size() > 128 || f[0] == '/' || f.find("..") != std::string::npos) return false;
    for (unsigned char c : f)
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '/')) return false;
    return true;
}

unsigned toUnsigned(const Json& j) {
    if (!j.isNumber() || !(j.asNumber() > 0)) return 0;
    return j.asNumber() >= 4294967295.0 ? 4294967295u : (unsigned)j.asNumber();
}

// A manifest is remote input: at most 8 messages of 200 bytes each reach the toasts (cut on a UTF-8 boundary).
constexpr size_t kMaxMessages = 8, kMaxMessageBytes = 200;
std::string clipMessage(std::string s) {
    if (s.size() <= kMaxMessageBytes) return s;
    size_t n = kMaxMessageBytes;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    s.resize(n);
    return s;
}

enum class Entry { Ok, Skip, Malformed };

// One packs[i] object.  Malformed = a required key is missing or of the wrong type; Skip = present but unusable.
Entry parseEntry(const Json& o, PackMeta& m) {
    if (!o.isObject()) return Entry::Malformed;
    for (const char* k : {"crc32", "file", "sha256"})
        if (!o[k].isString()) return Entry::Malformed;
    if (!o["bytes"].isNumber()) return Entry::Malformed;
    m = PackMeta();
    const std::string crc = o["crc32"].asString(), sha = o["sha256"].asString();
    const double bytes = o["bytes"].asNumber();
    if (!hexDigits(crc, 8) || !hexDigits(sha, 64) || !safeRelativeFile(o["file"].asString())) return Entry::Skip;
    if (!(bytes >= 1) || bytes > (double)kCheatPackMaxBytes || bytes != std::floor(bytes)) return Entry::Skip;
    m.crc32 = upper(crc);
    m.sha256 = lower(sha);
    m.file = o["file"].asString();
    m.bytes = (unsigned)bytes;
    m.name = o["name"].isString() ? o["name"].asString() : "";
    m.title = o["title"].isString() && !o["title"].asString().empty() ? o["title"].asString()
              : !m.name.empty() ? m.name : m.crc32;
    m.updated = o["updated"].isString() ? o["updated"].asString() : "";
    m.credit = o["credit"].isString() ? o["credit"].asString() : "";
    m.cheats = toUnsigned(o["cheats"]);
    m.writes = toUnsigned(o["writes"]);
    m.partial = o["partial"].isBool() && o["partial"].asBool();
    m.exceedsCoreLimit = o["exceeds_core_limit"].isBool() && o["exceeds_core_limit"].asBool();
    if (o["notes"].isArray())
        for (const Json& n : o["notes"].items())
            if (n.isObject())
                m.notes.emplace_back(n["setting"].isString() ? n["setting"].asString() : "",
                                     n["reason"].isString() ? n["reason"].asString() : "");
    if (o["messages"].isArray())
        for (const Json& s : o["messages"].items())
            if (s.isString() && !s.asString().empty() && m.messages.size() < kMaxMessages)
                m.messages.push_back(clipMessage(s.asString()));
    m.json = o.dump();
    return Entry::Ok;
}

std::string joinUrl(const std::string& base, const std::string& rel) { return base + rel; }

bool readText(const std::string& path, std::string& out) {
    std::vector<unsigned char> b;
    if (!readFile(path, b)) return false;
    out.assign(b.begin(), b.end());
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// pure helpers
// ---------------------------------------------------------------------------------------------
bool parseManifest(const std::string& text, std::vector<PackMeta>& out, std::string& err) {
    Json doc;
    std::string jerr;
    if (!Json::parse(text, doc, jerr) || !doc.isObject()) {
        err = "Store manifest not understood: " + (jerr.empty() ? std::string("not a JSON object") : jerr) + ".";
        return false;
    }
    const Json& fmt = doc["format"];
    if (!fmt.isNumber() || fmt.asNumber() != 1.0) {
        err = "Store manifest not understood (format " +
              (fmt.isNumber() ? std::to_string((long long)fmt.asNumber()) : std::string("?")) + ").";
        return false;
    }
    if (!doc["packs"].isArray()) {
        err = "Store manifest not understood (format 1, no pack list).";
        return false;
    }
    std::vector<PackMeta> packs;
    std::map<std::string, bool> seen;
    for (const Json& o : doc["packs"].items()) {
        PackMeta m;
        Entry e = parseEntry(o, m);
        if (e == Entry::Malformed) {
            err = "Store manifest not understood (format 1, a pack entry is incomplete).";
            return false;
        }
        if (e == Entry::Skip || seen.count(m.crc32)) continue;
        seen[m.crc32] = true;
        packs.push_back(std::move(m));
    }
    out = std::move(packs);
    err.clear();
    return true;
}

bool parsePackMeta(const std::string& text, PackMeta& out) {
    Json doc;
    std::string jerr;
    PackMeta m;
    if (!Json::parse(text, doc, jerr) || parseEntry(doc, m) != Entry::Ok) return false;
    out = std::move(m);
    return true;
}

bool verifyPack(const PackMeta& meta, uint32_t crc, const std::string& body, std::string& err) {
    if (body.size() != meta.bytes) {
        err = "Pack changed on the server (size differs from the index): check for updates again.";
        return false;
    }
    if (sha256Hex(body.data(), body.size()) != meta.sha256) {
        err = "Pack changed on the server (checksum differs from the index): check for updates again.";
        return false;
    }
    ChtSet set;
    if (!loadChtText(body, crcHex(crc) + ".cht", set) || set.cheats.empty()) {
        err = "Pack rejected: it contains no cheats.";
        return false;
    }
    if (!set.revisionBound || !set.restriction.empty() || set.romCrc != crc) {
        err = "Pack rejected: it is not bound to this cartridge (CRC32 " + crcHex(crc) + ").";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// hand-off block between the worker closures and the main thread
// ---------------------------------------------------------------------------------------------
struct CheatStore::Shared {
    struct Result {
        bool manifest = true;             // else a pack
        bool ok = false;
        std::string error;                // the status line when !ok
        std::string raw;                  // manifest.json bytes / pack bytes
        std::vector<PackMeta> packs;      // manifest
        PackMeta meta;                    // pack
        uint32_t crc = 0;                 // pack
    };
    std::mutex m;
    std::condition_variable cv;
    std::deque<Result> results;
    unsigned running = 0;
    std::atomic<bool> cancel{false};
    // postGameLoaded / postGameClosed hand-off (under m): 0 none, 1 loaded(pendingCrc), 2 closed.
    int pending = 0;
    uint32_t pendingCrc = 0;

    bool enter() {
        std::lock_guard<std::mutex> l(m);
        if (cancel) return false;
        running++;
        return true;
    }
    void leave(Result r) {
        std::lock_guard<std::mutex> l(m);
        running--;
        if (!cancel) results.push_back(std::move(r));
        cv.notify_all();
    }
};

// Holds a started job's slot: its destructor always runs leave(), so a throwing fetch hook (caught by the job,
// but this does not rely on it) can never leave `running` raised and hang stop().
struct JobSlot {
    std::shared_ptr<CheatStore::Shared> sh;
    CheatStore::Shared::Result r;
    explicit JobSlot(std::shared_ptr<CheatStore::Shared> s) : sh(std::move(s)) {}
    ~JobSlot() { sh->leave(std::move(r)); }
};

namespace {
std::string unreachable(const std::string& err) {
    return "Could not reach the cheat store: " + (err.empty() ? std::string("request failed") : err) +
           ". Installed packs still work.";
}
}  // namespace

CheatStore::CheatStore() : sh_(std::make_shared<Shared>()) {}

const std::atomic<bool>& CheatStore::cancelFlag() const { return sh_->cancel; }

// ---------------------------------------------------------------------------------------------
// setup and paths
// ---------------------------------------------------------------------------------------------
void CheatStore::setDir(const std::string& d) {
    dir_ = d;
    if (!dir_.empty() && dir_.back() != '/' && dir_.back() != '\\') dir_ += '/';
    installed_.clear();
    manifest_.clear();
    manifestLoaded_ = false;
    std::string text, err;
    std::vector<PackMeta> packs;
    if (!dir_.empty() && readText(dir_ + "manifest.json", text) && parseManifest(text, packs, err)) {
        for (PackMeta& p : packs) manifest_[parseCrc(p.crc32)] = std::move(p);
        manifestLoaded_ = true;
    }
}

void CheatStore::setBaseUrl(const std::string& url) {
    base_ = url.empty() ? std::string(kCheatStoreBase) : url;
    if (base_.back() != '/') base_ += '/';
}

std::string CheatStore::packsDir() const { return dir_ + "packs"; }
std::string CheatStore::chtPath(uint32_t crc) const { return dir_ + "packs/" + crcHex(crc) + ".cht"; }
std::string CheatStore::sidecarPath(uint32_t crc) const { return dir_ + "packs/" + crcHex(crc) + ".json"; }

// ---------------------------------------------------------------------------------------------
// installed packs (read lazily, re-read on window open / install / remove)
// ---------------------------------------------------------------------------------------------
void CheatStore::refresh(uint32_t crc) { installed_.erase(crc); }

const CheatStore::Installed& CheatStore::info(uint32_t crc) const {
    auto it = installed_.find(crc);
    if (it != installed_.end()) return it->second;
    Installed in;
    std::string cht, side;
    if (!dir_.empty() && crc != 0 && readText(chtPath(crc), cht)) {
        in.present = true;
        if (readText(sidecarPath(crc), side) && parsePackMeta(side, in.meta)) {
            in.hasMeta = true;
            in.edited = sha256Hex(cht.data(), cht.size()) != in.meta.sha256;
        }
    }
    return installed_.emplace(crc, std::move(in)).first->second;
}

const PackMeta* CheatStore::installed(uint32_t crc) const {
    const Installed& in = info(crc);
    return in.present && in.hasMeta ? &in.meta : nullptr;
}

const PackMeta* CheatStore::offered(uint32_t crc) const {
    auto it = manifest_.find(crc);
    return it == manifest_.end() ? nullptr : &it->second;
}

PackState CheatStore::state(uint32_t crc) const {
    if (!haveGame_) return PackState::NoGame;
    if (crc == 0) return PackState::Disc;
    const Installed& in = info(crc);
    const PackMeta* off = offered(crc);
    if (in.present) {
        if (in.hasMeta && in.edited) return PackState::EditedLocally;
        if (off) return in.hasMeta && in.meta.sha256 == off->sha256 ? PackState::Installed : PackState::UpdateAvailable;
        return manifestLoaded_ ? PackState::NoLongerOffered : PackState::Installed;
    }
    if (!downloadsAllowed || !downloadsAllowed()) return PackState::DownloadsOff;
    if (off) return PackState::Offered;
    return !manifestLoaded_ && manifestBusy_ ? PackState::Checking : PackState::NotOffered;
}

// ---------------------------------------------------------------------------------------------
// requests
// ---------------------------------------------------------------------------------------------
void CheatStore::dispatch(std::function<void()> job) {
    busy_ = true;
    if (run) run(std::move(job));
    else job();
}

void CheatStore::startManifestFetch() {
    manifestBusy_ = true;
    status_.clear();
    auto sh = sh_;
    auto get = fetch;
    const std::string url = joinUrl(base_, "manifest.json");
    dispatch([sh, get, url]() {
        if (!sh->enter()) return;
        JobSlot slot(sh);
        Shared::Result& r = slot.r;
        std::string body, err;
        try {
            bool ok = get && get(url, kCheatManifestMaxBytes, body, err);
            if (ok && body.size() > kCheatManifestMaxBytes) ok = false, err = "response too large";
            if (!ok) r.error = unreachable(err);
            else if (parseManifest(body, r.packs, r.error)) r.ok = true, r.raw = std::move(body);
        } catch (...) {
            r = Shared::Result();
            r.error = unreachable("the request failed unexpectedly");
        }
    });
}

void CheatStore::windowOpened(uint32_t crc, Config& cfg) {
    cfg_ = &cfg;
    crc_ = crc;
    haveGame_ = true;
    refresh(crc);
    if (crc == 0 || busy_ || !downloadsAllowed || !downloadsAllowed() || !fetch) return;
    const int64_t t = now ? now() : (int64_t)std::time(nullptr);
    const int64_t last = std::strtoll(cfg.get(kCheatCheckedKey, "0").c_str(), nullptr, 10);
    if (manifestLoaded_ && last <= t && t - last < kCheatCheckIntervalSeconds) return;
    startManifestFetch();
}

void CheatStore::checkNow() {
    if (!haveGame_ || crc_ == 0 || busy_ || !fetch || !downloadsAllowed || !downloadsAllowed()) return;
    startManifestFetch();
}

bool CheatStore::install(uint32_t crc) {
    if (crc == 0) {
        status_ = "Community packs cover cartridges only.";
        return false;
    }
    if (!downloadsAllowed || !downloadsAllowed() || !fetch) {
        status_ = "Downloads are off (Library: Download art and cheat packs).";
        return false;
    }
    if (busy_) {
        status_ = "Another cheat store request is in progress.";
        return false;
    }
    const PackMeta* off = offered(crc);
    if (!off) {
        status_ = "The store has no pack for this cartridge (CRC32 " + crcHex(crc) + ").";
        return false;
    }
    status_.clear();
    packBusyCrc_ = crc;
    auto sh = sh_;
    auto get = fetch;
    const PackMeta meta = *off;
    const std::string url = joinUrl(base_, meta.file);
    dispatch([sh, get, url, meta, crc]() {
        if (!sh->enter()) return;
        JobSlot slot(sh);
        Shared::Result& r = slot.r;
        r.manifest = false;
        r.crc = crc;
        r.meta = meta;
        std::string body, err;
        try {
            bool ok = get && get(url, kCheatPackMaxBytes, body, err);
            if (ok && body.size() > kCheatPackMaxBytes) ok = false, err = "response too large";
            if (!ok) r.error = unreachable(err);
            else if (verifyPack(meta, crc, body, r.error)) r.ok = true, r.raw = std::move(body);
        } catch (...) {
            r.ok = false;
            r.raw.clear();
            r.error = unreachable("the request failed unexpectedly");
        }
    });
    return true;
}

bool CheatStore::remove(uint32_t crc, CheatManager& cm) {
    if (crc == 0 || dir_.empty()) return false;
    if (packBusyCrc_ == crc) {   // pump() would write the pack back over the removal
        status_ = "An install for this cartridge is in progress; remove it once it has finished.";
        return false;
    }
    std::error_code ec;
    const bool had = fs::remove(fs::path(chtPath(crc)), ec);
    if (ec) {
        status_ = "Could not remove the pack: " + ec.message();
        return false;
    }
    fs::remove(fs::path(sidecarPath(crc)), ec);
    refresh(crc);
    if (cm.active() && cm.game().crc == crc) {
        cm.removeCommunityToggles();
        cm.reloadCommunity();
    }
    turnedOff_ = 0;
    status_.clear();
    return had;
}

void CheatStore::postGameLoaded(uint32_t crc) {
    std::lock_guard<std::mutex> l(sh_->m);
    sh_->pending = 1;
    sh_->pendingCrc = crc;
}

void CheatStore::postGameClosed() {
    std::lock_guard<std::mutex> l(sh_->m);
    sh_->pending = 2;
}

void CheatStore::pump(CheatManager& cm) {
    std::deque<Shared::Result> done;
    int pending;
    uint32_t pendingCrc;
    {
        std::lock_guard<std::mutex> l(sh_->m);
        done.swap(sh_->results);
        pending = sh_->pending;
        pendingCrc = sh_->pendingCrc;
        sh_->pending = 0;
    }
    if (pending == 1) gameLoaded(pendingCrc, cm);
    else if (pending == 2) gameClosed();
    for (Shared::Result& r : done) {
        busy_ = false;
        if (r.manifest) manifestBusy_ = false;
        else packBusyCrc_ = 0;
        if (!r.ok) {
            status_ = r.error;
            continue;
        }
        if (r.manifest) {
            manifest_.clear();
            for (PackMeta& p : r.packs) manifest_[parseCrc(p.crc32)] = std::move(p);
            manifestLoaded_ = true;
            std::error_code ec;
            fs::create_directories(fs::path(dir_), ec);
            writeFileAtomic(dir_ + "manifest.json", r.raw.data(), r.raw.size());   // a cache: failure is harmless
            if (cfg_) {
                cfg_->set(kCheatCheckedKey, std::to_string(now ? now() : (int64_t)std::time(nullptr)));
                cfg_->save();
            }
            status_.clear();
            continue;
        }
        // A verified pack: .cht first, then the sidecar; a failure on the second restores the first.
        std::error_code ec;
        fs::create_directories(fs::path(packsDir()), ec);
        std::string old;
        const bool hadOld = readText(chtPath(r.crc), old);
        bool ok = writeFileAtomic(chtPath(r.crc), r.raw.data(), r.raw.size());
        if (ok && !writeFileAtomic(sidecarPath(r.crc), r.meta.json.data(), r.meta.json.size())) {
            ok = false;
            if (hadOld) writeFileAtomic(chtPath(r.crc), old.data(), old.size());
            else fs::remove(fs::path(chtPath(r.crc)), ec);
        }
        refresh(r.crc);
        if (!ok) {
            status_ = "Could not write the pack to " + dir_ + ".";
            continue;
        }
        status_ = "Installed " + r.meta.title + ".";
        if (cm.active() && cm.game().crc == r.crc) {
            cm.reloadCommunity();
            turnedOff_ = cm.lastPrunedEnabled();
        }
    }
}

void CheatStore::stop() {
    sh_->cancel = true;
    std::unique_lock<std::mutex> l(sh_->m);
    sh_->cv.wait(l, [&] { return sh_->running == 0; });
    sh_->results.clear();
    sh_->pending = 0;
    busy_ = manifestBusy_ = false;
    packBusyCrc_ = 0;
}

// ---------------------------------------------------------------------------------------------
// pack messages
// ---------------------------------------------------------------------------------------------
void CheatStore::gameLoaded(uint32_t crc, const CheatManager& cm) {
    turnedOff_ = 0;
    crc_ = crc;
    haveGame_ = true;
    announced_ = false;
    refresh(crc);
    announce(cm);
}

void CheatStore::afterApply(const CheatManager& cm) {
    if (!announced_) announce(cm);
}

void CheatStore::gameClosed() {
    turnedOff_ = 0;
    haveGame_ = false;
    crc_ = 0;
    announced_ = false;
}

void CheatStore::announce(const CheatManager& cm) {
    if (!message || crc_ == 0 || !cm.active() || cm.game().crc != crc_ || cm.blockedNow()) return;
    const ChtSet* set = cm.communitySet();
    if (!set || !set->restriction.empty()) return;
    bool any = false;
    for (const ChtCheat& c : set->cheats)
        if (c.supported && cm.isEnabled(c, CheatOrigin::Community)) any = true;
    if (!any) return;
    announced_ = true;
    const PackMeta* m = installed(crc_);
    if (!m) return;
    for (const std::string& text : m->messages) message(m->title + ": " + text);
}

}  // namespace vj
