#include "core_host.h"

#include "bios.h"
#include "m3u.h"
#include "paths.h"
#include "typed_options.h"

#include <vfs/vfs_implementation.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// The core's public libretro entry points (statically linked).
extern "C" {
void retro_set_environment(retro_environment_t);
void retro_set_video_refresh(retro_video_refresh_t);
void retro_set_audio_sample(retro_audio_sample_t);
void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void retro_set_input_poll(retro_input_poll_t);
void retro_set_input_state(retro_input_state_t);
void retro_init(void);
void retro_deinit(void);
void retro_get_system_info(struct retro_system_info*);
void retro_get_system_av_info(struct retro_system_av_info*);
void retro_set_controller_port_device(unsigned, unsigned);
void retro_reset(void);
void retro_run(void);
size_t retro_serialize_size(void);
bool retro_serialize(void*, size_t);
bool retro_unserialize(const void*, size_t);
bool retro_load_game(const struct retro_game_info*);
void retro_unload_game(void);
void* retro_get_memory_data(unsigned);
size_t retro_get_memory_size(unsigned);
}

namespace vj {

// bios.h keeps its own copies of the core's ids so that it needs no core headers.
static_assert(bios::CartBoot == VJ_BIOS_CART_BOOT && bios::Cd == VJ_BIOS_CD, "BIOS role ids drifted from vj_frontend_ext.h");
static_assert(bios::SrcBuiltin == VJ_BIOS_SRC_BUILTIN && bios::SrcSystem == VJ_BIOS_SRC_SYSTEM &&
                  bios::SrcFile == VJ_BIOS_SRC_FRONTEND,
              "BIOS source ids drifted from vj_frontend_ext.h");

namespace {

// ---- logging ----------------------------------------------------------------
bool g_logVerbose = false;

void RETRO_CALLCONV logCb(enum retro_log_level level, const char* fmt, ...) {
    CoreHost& h = CoreHost::instance();
    const bool print = level >= RETRO_LOG_WARN || g_logVerbose;
    if (!print && !h.onLog && !h.onUiEvent) return;
    static const char* const tag[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    // The taps see every line, INFO included: the Netplay window reads the core's
    // "[NETLINK] link UP / link stats" lines from here (they are INFO).
    if (h.onLog || h.onUiEvent) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (h.onLog) h.onLog((int)level, line.c_str());
        h.emitLogLine((int)level, line);
    }
    if (!print) return;
    std::fprintf(stderr, "[core %s] %s", (unsigned)level < 4 ? tag[level] : "?", buf);
    size_t n = std::strlen(buf);
    if (n == 0 || buf[n - 1] != '\n') std::fputc('\n', stderr);
}

// ---- VFS: hand the core libretro-common's own implementation -----------------
// (already linked into the core), wrapped to the interface's handle types.
using VFile = retro_vfs_file_handle;
using VDir = retro_vfs_dir_handle;
libretro_vfs_implementation_file* F(VFile* h) { return reinterpret_cast<libretro_vfs_implementation_file*>(h); }
libretro_vfs_implementation_dir* D(VDir* h) { return reinterpret_cast<libretro_vfs_implementation_dir*>(h); }

const char* RETRO_CALLCONV vfsGetPath(VFile* s) { return retro_vfs_file_get_path_impl(F(s)); }
VFile* RETRO_CALLCONV vfsOpen(const char* p, unsigned m, unsigned h) { return reinterpret_cast<VFile*>(retro_vfs_file_open_impl(p, m, h)); }
int RETRO_CALLCONV vfsClose(VFile* s) { return retro_vfs_file_close_impl(F(s)); }
int64_t RETRO_CALLCONV vfsSize(VFile* s) { return retro_vfs_file_size_impl(F(s)); }
int64_t RETRO_CALLCONV vfsTruncate(VFile* s, int64_t l) { return retro_vfs_file_truncate_impl(F(s), l); }
int64_t RETRO_CALLCONV vfsTell(VFile* s) { return retro_vfs_file_tell_impl(F(s)); }
int64_t RETRO_CALLCONV vfsSeek(VFile* s, int64_t o, int w) { return retro_vfs_file_seek_impl(F(s), o, w); }
int64_t RETRO_CALLCONV vfsRead(VFile* s, void* b, uint64_t n) { return retro_vfs_file_read_impl(F(s), b, n); }
int64_t RETRO_CALLCONV vfsWrite(VFile* s, const void* b, uint64_t n) { return retro_vfs_file_write_impl(F(s), b, n); }
int RETRO_CALLCONV vfsFlush(VFile* s) { return retro_vfs_file_flush_impl(F(s)); }
int RETRO_CALLCONV vfsRemove(const char* p) { return retro_vfs_file_remove_impl(p); }
int RETRO_CALLCONV vfsRename(const char* a, const char* b) { return retro_vfs_file_rename_impl(a, b); }
int RETRO_CALLCONV vfsStat(const char* p, int32_t* sz) { return retro_vfs_stat_impl(p, sz); }
int RETRO_CALLCONV vfsMkdir(const char* d) { return retro_vfs_mkdir_impl(d); }
VDir* RETRO_CALLCONV vfsOpendir(const char* d, bool h) { return reinterpret_cast<VDir*>(retro_vfs_opendir_impl(d, h)); }
bool RETRO_CALLCONV vfsReaddir(VDir* d) { return retro_vfs_readdir_impl(D(d)); }
const char* RETRO_CALLCONV vfsDirentName(VDir* d) { return retro_vfs_dirent_get_name_impl(D(d)); }
bool RETRO_CALLCONV vfsDirentIsDir(VDir* d) { return retro_vfs_dirent_is_dir_impl(D(d)); }
int RETRO_CALLCONV vfsClosedir(VDir* d) { return retro_vfs_closedir_impl(D(d)); }

retro_vfs_interface g_vfs = {
    vfsGetPath, vfsOpen, vfsClose, vfsSize, vfsTell, vfsSeek, vfsRead, vfsWrite, vfsFlush,
    vfsRemove, vfsRename, vfsTruncate, vfsStat, vfsMkdir, vfsOpendir, vfsReaddir,
    vfsDirentName, vfsDirentIsDir, vfsClosedir};

std::string str(const char* s) { return s ? s : ""; }

uint32_t fnv1a(const unsigned char* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

// Static strings whose address is handed to the core (system/save dir).
std::string g_systemDirStr, g_saveDirStr;

retro_audio_buffer_status_callback_t g_audioBufferCb = nullptr;

}  // namespace

CoreHost& CoreHost::instance() {
    static CoreHost h;
    return h;
}

void CoreHost::setDirectories(const std::string& systemDir, const std::string& saveDir) {
    systemDir_ = systemDir;
    saveDir_ = saveDir;
    g_systemDirStr = systemDir;
    g_saveDirStr = saveDir;
}

std::string CoreHost::saveFallbackPath() const {
    if (saveFallbackDir_.empty() || loadedName_.empty()) return "";
    return saveFallbackDir_ + loadedName_ + ".srm";
}

std::string CoreHost::validExtensions() {
    retro_system_info si;
    std::memset(&si, 0, sizeof si);
    retro_get_system_info(&si);
    return str(si.valid_extensions);
}

float CoreHost::aspectLive() const {
    float a = avInfo_.geometry.aspect_ratio;
    if (a > 0.0f) return a;
    if (frame_.height) return (float)frame_.width / (float)frame_.height;
    return 4.0f / 3.0f;
}

// ---- static trampolines --------------------------------------------------------
bool CoreHost::envCb(unsigned cmd, void* data) { return instance().env(cmd, data); }

void CoreHost::videoCb(const void* data, unsigned w, unsigned h, size_t pitch) {
    CoreHost& s = instance();
    if (!data) return;  // duplicated frame: keep the previous pixels
    Frame& f = s.frame_;
    f.width = w;
    f.height = h;
    f.pixels.resize((size_t)w * h);
    const unsigned char* src = (const unsigned char*)data;
    for (unsigned y = 0; y < h; y++)
        std::memcpy(&f.pixels[(size_t)y * w], src + (size_t)y * pitch, (size_t)w * 4);
    f.serial = ++s.serial_;
}

void CoreHost::audioSampleCb(int16_t l, int16_t r) {
    CoreHost& s = instance();
    int16_t b[2] = {l, r};
    if (s.onAudio) s.onAudio(b, 1);
}

size_t CoreHost::audioBatchCb(const int16_t* data, size_t frames) {
    CoreHost& s = instance();
    if (s.onAudio) s.onAudio(data, frames);
    return frames;
}

void CoreHost::inputPollCb() {
    CoreHost& s = instance();
    if (s.inputPoll) s.inputPoll();
}

int16_t CoreHost::inputStateCb(unsigned port, unsigned device, unsigned index, unsigned id) {
    CoreHost& s = instance();
    return s.inputState ? s.inputState(port, device, index, id) : 0;
}

// ---- options -------------------------------------------------------------------
void CoreHost::captureOptions(const retro_core_options_v2* o) {
    if (!o) return;
    categories_.clear();
    defs_.clear();
    // A definitions push rebuilds the frontend's option manager with every row
    // visible again (RetroArch's behaviour; harness.c models the same).  The
    // core re-pushes visibility afterwards.
    visibility_.clear();
    if (o->categories) {
        for (const retro_core_option_v2_category* c = o->categories; c->key; c++)
            categories_.push_back({str(c->key), str(c->desc), str(c->info)});
    }
    if (o->definitions) {
        for (const retro_core_option_v2_definition* d = o->definitions; d->key; d++) {
            OptionDef def;
            def.key = d->key;
            def.desc = str(d->desc);
            def.descCategorized = str(d->desc_categorized);
            def.info = str(d->info);
            def.infoCategorized = str(d->info_categorized);
            def.categoryKey = str(d->category_key);
            def.defaultValue = str(d->default_value);
            for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && d->values[i].value; i++)
                def.values.push_back({d->values[i].value, str(d->values[i].label)});
            if (def.defaultValue.empty() && !def.values.empty()) def.defaultValue = def.values[0].value;
            defs_.push_back(std::move(def));
        }
    }
    locked_.clear();
    if (optionLock) {
        for (const OptionDef& d : defs_) {
            std::string v;
            if (!optionLock(d.key, v)) continue;
            locked_[d.key] = lockedValue(d, v);
        }
    }
    optionPushes_++;
    optSerial_++;
}

// A locked value outside the option's own list falls back to its default ("" means the default too).
std::string CoreHost::lockedValue(const OptionDef& d, const std::string& v) {
    for (const OptionValue& ov : d.values)
        if (ov.value == v) return v;
    return d.defaultValue;
}

void CoreHost::refreshLocks() {
    if (!onOwner()) {
        queue([this] { refreshLocks(); });
        return;
    }
    if (inFrame_ || !optionLock) return;
    bool changed = false;
    for (const OptionDef& d : defs_) {
        auto l = locked_.find(d.key);
        if (l == locked_.end()) continue;   // which keys are locked is settled at the push; only values move
        std::string v;
        if (!optionLock(d.key, v)) continue;
        std::string nv = lockedValue(d, v);
        if (nv == l->second) continue;
        l->second = nv;
        changed = true;
    }
    if (!changed) return;
    variableUpdate_ = true;
    optSerial_++;         // the UI's copy of the table (locked values included) is out of date
    publishSnapshot();    // threaded: at once, as a disc change does (inline: a no-op)
}

bool CoreHost::optionVisible(const std::string& key) const {
    const bool own = onOwner();
    const std::map<std::string, std::string>& locked = own ? locked_ : uiTable().locked;
    const std::map<std::string, bool>& vis = own ? visibility_ : uiTable().visibility;
    if (locked.count(key)) return false;
    auto it = vis.find(key);
    return it == vis.end() ? true : it->second;
}

// A value the core will be handed: one of the core's own list entries, or -- for the keys in
// typed_options.h -- any free-form value that passes that key's validator.  Enumerations stay strict.
static bool validFor(const OptionDef& d, const std::string& v) {
    for (const OptionValue& ov : d.values)
        if (ov.value == v) return true;
    const TypedOption* t = typedOptionFor(d.key);
    return t && validTypedValue(*t, v);
}

std::string CoreHost::optionValue(const OptionDef& d) const {
    const bool own = onOwner();
    const std::map<std::string, std::string>& locked = own ? locked_ : uiTable().locked;
    const std::map<std::string, std::string>& session = own ? sessionValues_ : uiSession_;
    const std::map<std::string, std::string>& user = own ? userValues_ : uiUser_;
    auto l = locked.find(d.key);
    if (l != locked.end()) return l->second;
    auto s = session.find(d.key);
    if (s != session.end() && validFor(d, s->second)) return s->second;
    auto u = user.find(d.key);
    if (u != user.end() && validFor(d, u->second)) return u->second;
    return d.defaultValue;
}

void CoreHost::loadUserOptions(const std::vector<std::pair<std::string, std::string>>& kv) {
    for (const auto& p : kv) userValues_[p.first] = p.second;
}

static CoreEvent optionEvent(CoreEvent::Kind k, const std::string& key, const std::string& value, bool erased) {
    CoreEvent e;
    e.kind = k;
    e.text = key;
    e.value = value;
    e.erased = erased;
    return e;
}

void CoreHost::emitUi(CoreEvent e) {
    e.epoch = epoch_;
    const bool option = e.kind == CoreEvent::Kind::UserOption || e.kind == CoreEvent::Kind::SessionOption;
    if (threaded() && onOwner()) {
        if (option || onUiEvent) uiEvents_.push(std::move(e));
        return;
    }
    if (!option && onUiEvent) onUiEvent(e);   // inline: the option writes were applied directly by the caller
}

void CoreHost::emitLogLine(int level, const std::string& line) {
    if (!onUiEvent) return;
    CoreEvent e;
    e.kind = CoreEvent::Kind::Log;
    e.level = level;
    e.text = line;
    emitUi(std::move(e));
}

void CoreHost::applyUiEvent(CoreEvent& e) {
    switch (e.kind) {
    case CoreEvent::Kind::UserOption:
        if (e.erased) uiUser_.erase(e.text);
        else uiUser_[e.text] = e.value;
        if (onOptionChanged) onOptionChanged(e.text, e.value, e.erased);
        break;
    case CoreEvent::Kind::SessionOption:
        if (e.erased) uiSession_.erase(e.text);
        else uiSession_[e.text] = e.value;
        break;
    default:
        if (onUiEvent) onUiEvent(e);
    }
}

uint32_t CoreHost::eventEpochBump() {
    const uint32_t n = ++uiEpoch_;
    if (onOwner()) epoch_ = n;
    else queue([this, n] { epoch_ = n; });
    return n;
}

void CoreHost::overrideOption(const std::string& key, const std::string& value) {
    if (!onOwner()) {
        // The UI's copy now, on the UI thread; the core's copy at the next frame boundary (never this method again
        // on the emulation thread: that would write uiSession_ there, racing the UI's reads).
        uiSession_[key] = value;
        queue([this, key, value] { applyOverride(key, &value); });
        return;
    }
    if (inFrame_) return;   // the debugger has the core halted inside retro_run(): no option changes
    if (threaded()) emitUi(optionEvent(CoreEvent::Kind::SessionOption, key, value, false));   // the UI applies it at its next pin
    applyOverride(key, &value);
}

static std::string problemFor(const std::vector<OptionDef>& defs, const std::string& key,
                              const std::string& value) {
    for (const OptionDef& d : defs) {
        if (d.key != key) continue;
        if (validFor(d, value)) return "";
        std::string list;
        for (const OptionValue& v : d.values) list += (list.empty() ? "" : ", ") + v.value;
        if (const TypedOption* t = typedOptionFor(key)) {
            std::string why;
            validTypedValue(*t, value, &why);
            return "'" + value + "' is not a valid value for " + key + " (" + why + "; or one of: " + list + ")";
        }
        return "'" + value + "' is not a valid value for " + key + " (valid: " + list + ")";
    }
    return "'" + key + "' is not an option this core registers";
}

std::vector<std::string> CoreHost::checkOverrides(bool sessionOnly) const {
    noteOffOwner("checkOverrides");
    std::vector<std::string> out;
    const std::map<std::string, std::string>& m = sessionOnly ? sessionValues_ : userValues_;
    for (const auto& kv : m) {
        auto l = locked_.find(kv.first);
        if (l != locked_.end()) {
            // The app forces it.  Silently running the forced value would be the "--option did
            // nothing" trap, so a value that differs is reported (an error for --option, a
            // warning for a stale saved one).
            if (kv.second != l->second)
                out.push_back("'" + kv.first + "' is managed by the app (Settings > Input / Controls)" +
                              (sessionOnly ? "; --option cannot change it" : "; the saved value is ignored"));
            continue;
        }
        std::string p = problemFor(defs_, kv.first, kv.second);
        if (!p.empty()) out.push_back(p);
    }
    return out;
}

unsigned CoreHost::visibleOptionCount() const {
    unsigned n = 0;
    for (const OptionDef& d : optionDefs())
        if (optionVisible(d.key)) n++;
    return n;
}

void CoreHost::setOptionValue(const std::string& key, const std::string& value) {
    if (!onOwner()) {
        uiUser_[key] = value;
        if (onOptionChanged) onOptionChanged(key, value, false);   // config.ini is only ever written on the UI thread
        queue([this, key, value] { applyUserValue(key, &value); });
        return;
    }
    if (inFrame_) return;
    if (threaded()) emitUi(optionEvent(CoreEvent::Kind::UserOption, key, value, false));   // the UI applies it at its next pin
    else if (onOptionChanged) onOptionChanged(key, value, false);
    applyUserValue(key, &value);
}

void CoreHost::clearOverride(const std::string& key) {
    if (!onOwner()) {
        uiSession_.erase(key);
        queue([this, key] { applyOverride(key, nullptr); });
        return;
    }
    if (inFrame_) return;
    if (threaded()) emitUi(optionEvent(CoreEvent::Kind::SessionOption, key, "", true));
    applyOverride(key, nullptr);
}

void CoreHost::resetOptionToDefault(const std::string& key) {
    if (!onOwner()) {
        uiUser_.erase(key);
        if (onOptionChanged) onOptionChanged(key, "", true);
        queue([this, key] { applyUserValue(key, nullptr); });
        return;
    }
    if (inFrame_) return;
    if (threaded()) emitUi(optionEvent(CoreEvent::Kind::UserOption, key, "", true));
    else if (onOptionChanged) onOptionChanged(key, "", true);
    applyUserValue(key, nullptr);
}

// The core's copy of a saved value (the owner side of setOptionValue / resetOptionToDefault).
void CoreHost::applyUserValue(const std::string& key, const std::string* value) {
    if (inFrame_) return;
    if (value) userValues_[key] = *value;
    else userValues_.erase(key);
    variableUpdate_ = true;
    if (updateDisplayCb_) updateDisplayCb_();
}

// The core's copy of a session override (the owner side of overrideOption / clearOverride).
void CoreHost::applyOverride(const std::string& key, const std::string* value) {
    if (inFrame_) return;
    if (value) {
        sessionValues_[key] = *value;
        variableUpdate_ = true;
    } else if (sessionValues_.erase(key)) {
        variableUpdate_ = true;
    }
}

// ---- messages ------------------------------------------------------------------
std::vector<OsdMessage> CoreHost::takeMessages() {
    std::vector<OsdMessage> out;
    std::lock_guard<std::mutex> l(msgMu_);
    out.swap(messages_);
    return out;
}

void CoreHost::pushMessage(const std::string& text, uint32_t ms, int level) {
    OsdMessage m;
    m.text = text;
    m.durationMs = ms;
    m.level = level;
    std::lock_guard<std::mutex> l(msgMu_);
    messages_.push_back(std::move(m));
}

// ---- environment ---------------------------------------------------------------
// --verbose: say once per change when the core is handed a non-default value.
void CoreHost::traceOption(const std::string& key, const char* value, const char* why) {
    if (!logVerbose_) return;
    std::string& last = optionTrace_[key];
    if (last == value) return;
    last = value;
    std::fprintf(stderr, "[app] option %s=%s (%s)\n", key.c_str(), value, why);
}

bool CoreHost::env(unsigned cmd, void* data) {
    // Experimental flag bit is part of some command ids; compare masked.
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((retro_log_callback*)data)->log = logCb;
        return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return data && *(const retro_pixel_format*)data == RETRO_PIXEL_FORMAT_XRGB8888;

    // ---- core options (v2 only; the legacy lists are reported unsupported) ----
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (data) *(unsigned*)data = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        captureOptions((const retro_core_options_v2*)data);
        return true;  // "true" = categories supported
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        // We never answer GET_LANGUAGE, so `local` is NULL and `us` is the table.
        const retro_core_options_v2_intl* o = (const retro_core_options_v2_intl*)data;
        captureOptions(o ? o->us : nullptr);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        return false;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        updateDisplayCb_ = data ? ((const retro_core_options_update_display_callback*)data)->callback : nullptr;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: {
        const retro_core_option_display* d = (const retro_core_option_display*)data;
        if (d && d->key) {
            auto it = visibility_.find(d->key);
            if (it == visibility_.end() || it->second != d->visible) {
                visibility_[d->key] = d->visible;
                optSerial_++;   // the UI's snapshot of the table is out of date
            }
        }
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        retro_variable* v = (retro_variable*)data;
        if (!v || !v->key) return false;
        v->value = nullptr;
        for (const OptionDef& d : defs_) {
            if (d.key != v->key) continue;
            // Pointers into the maps' node-stable strings, or into defs_ (only
            // rebuilt on a definitions push, which also happens on the core's
            // own call stack -- the core copies values before then).
            auto l = locked_.find(d.key);
            if (l != locked_.end()) {
                v->value = l->second.c_str();
                traceOption(d.key, v->value, "locked");
                return true;
            }
            auto s = sessionValues_.find(d.key);
            if (s != sessionValues_.end() && validFor(d, s->second)) {
                v->value = s->second.c_str();
                traceOption(d.key, v->value, "override");
                return true;
            }
            auto u = userValues_.find(d.key);
            if (u != userValues_.end() && validFor(d, u->second)) {
                v->value = u->second.c_str();
                traceOption(d.key, v->value, "saved");
                return true;
            }
            v->value = d.defaultValue.c_str();
            return true;
        }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (data) *(bool*)data = variableUpdate_;
        variableUpdate_ = false;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLE:
        return false;  // the core must never call this; refuse loudly in the log
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        return false;

    // ---- geometry / AV -------------------------------------------------------
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        if (data) avInfo_.geometry = *(const retro_game_geometry*)data;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        if (data) avInfo_ = *(const retro_system_av_info*)data;
        return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        if (data) {
            int flags = RETRO_AV_ENABLE_AUDIO;
            if (wantVideo) flags |= RETRO_AV_ENABLE_VIDEO;
            *(int*)data = flags;
        }
        return true;
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
        g_audioBufferCb = data ? ((const retro_audio_buffer_status_callback*)data)->callback : nullptr;
        return true;
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
        if (data) minAudioLatencyMs_.store(*(const unsigned*)data);
        return true;

    // ---- directories / VFS ---------------------------------------------------
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char**)data = g_systemDirStr.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char**)data = g_saveDirStr.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: {
        retro_vfs_interface_info* i = (retro_vfs_interface_info*)data;
        if (!i) return false;
        if (i->required_interface_version > 3) return false;
        i->required_interface_version = 3;
        i->iface = &g_vfs;
        return true;
    }

    // ---- input ---------------------------------------------------------------
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;  // descriptors are for remapping UIs (#837)
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
        controllerPorts_.clear();
        const retro_controller_info* ci = (const retro_controller_info*)data;
        for (; ci && ci->types; ci++) {
            std::vector<ControllerType> port;
            for (unsigned i = 0; i < ci->num_types; i++)
                port.push_back({str(ci->types[i].desc), ci->types[i].id});
            controllerPorts_.push_back(std::move(port));
        }
        return true;
    }
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        keyboardCb_ = data ? ((const retro_keyboard_callback*)data)->callback : nullptr;
        return true;

    // ---- disk control --------------------------------------------------------
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        if (data) *(unsigned*)data = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
        if (data) {
            disk_ = *(const retro_disk_control_ext_callback*)data;
            diskRegistered_ = true;
        }
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        return false;  // we only speak the ext interface

    // ---- state / misc --------------------------------------------------------
    case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
        if (data) *(int*)data = RETRO_SAVESTATE_CONTEXT_NORMAL;
        return true;
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        // Kept (deep-copied) for achievements (#836): rcheevos resolves emulated
        // addresses through this very map, as RetroArch does.
        const retro_memory_map* m = (const retro_memory_map*)data;
        memDescs_.clear();
        memMap_ = retro_memory_map{};
        if (m && m->descriptors)
            memDescs_.assign(m->descriptors, m->descriptors + m->num_descriptors);
        memMap_.descriptors = memDescs_.empty() ? nullptr : memDescs_.data();
        memMap_.num_descriptors = (unsigned)memDescs_.size();
        return true;
    }
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        supportsNoGame_ = data && *(const bool*)data;
        return true;
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE: {
        contentOverrideExts_.clear();
        const retro_system_content_info_override* o = (const retro_system_content_info_override*)data;
        // Only the need_fullpath rows matter to us: those extensions are handed
        // over as a path, never as a memory image.
        for (; o && o->extensions; o++)
            if (o->need_fullpath) contentOverrideExts_ += std::string(contentOverrideExts_.empty() ? "" : "|") + o->extensions;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
        const retro_message_ext* m = (const retro_message_ext*)data;
        if (!m || !m->msg) return true;
        if (m->target == RETRO_MESSAGE_TARGET_LOG || m->target == RETRO_MESSAGE_TARGET_ALL)
            logCb(m->level, "%s", m->msg);
        if (m->target != RETRO_MESSAGE_TARGET_LOG) pushMessage(m->msg, m->duration ? m->duration : 3000, m->level);
        return true;
    }

    // ---- the core's private event channel (vj_frontend_ext.h) ----------------------
    // Only this app answers; RetroArch gets false and the core never sends an event.
    case VJ_ENV_HELLO: {
        vj_env_hello* h = (vj_env_hello*)data;
        if (!announceExtensions || !h || h->abi != VJ_FRONTEND_EXT_ABI) return false;
        h->caps_out = VJ_CAP_EVENTS | (announceBios ? VJ_CAP_BIOS : 0u) | (announceDebug ? VJ_CAP_DEBUG : 0u);
        helloSeen_ = true;
        return true;
    }
    case VJ_ENV_SET_DEBUG_IFACE: {
        // Only to a frontend that asked for it, and only an interface this build understands: the struct is
        // append-only, so a larger `size` is fine, a smaller one or another version is not.
        const vj_debug_iface* i = (const vj_debug_iface*)data;
        if (!i || !helloSeen_ || !announceDebug) return false;
        if (i->version != VJ_DEBUG_IFACE_VERSION || i->size < offsetof(vj_debug_iface, frame_number) + sizeof(i->frame_number))
            return false;
        debugIface_ = i;
        if (onDebugIface) onDebugIface(i);
        return true;
    }
    case VJ_ENV_GET_BIOS_PATH: {
        vj_env_bios_path* q = (vj_env_bios_path*)data;
        if (!q || !helloSeen_ || !announceBios || q->role < 1 || q->role > 2) return false;
        const std::string& p = biosPath_[q->role];
        // A file that has gone away is "no preference", not an error: the core's own search runs.
        if (p.empty() || !fileExists(p)) return false;
        q->path = p.c_str();   // valid until the selection changes; the core copies it during this call
        return true;
    }
    case VJ_ENV_GET_BOOT_TARGET:
        if (!data || !helloSeen_ || !announceBios) return false;
        *(unsigned*)data = bootTarget_;
        return true;
    case VJ_ENV_EVENT: {
        const vj_event* e = (const vj_event*)data;
        if (!e || !helloSeen_) return false;
        eventsSeen_++;
        if ((e->type == VJ_EVT_BIOS_STAGED || e->type == VJ_EVT_BIOS_REJECTED) && e->a >= 1 && e->a <= 2) {
            StagedBios& b = staged_[e->a];
            if (e->type == VJ_EVT_BIOS_STAGED) {
                b.seen = true;
                b.crc = e->b;
                b.source = e->c;
                b.where = e->s ? e->s : "";
            } else {
                b.rejected = true;
                b.rejectedPath = e->s ? e->s : "";
            }
        }
        if (e->type == VJ_EVT_CRASH_WATCHDOG) {
            crashEvents_.fetch_add(1);
            // The watchdog's own log line is easy to miss; say it on screen too.  The core rate-limits
            // each signature (about every 10 s), so this cannot flood.
            pushMessage(std::string("Core watchdog: ") + (e->s ? e->s : "?") +
                            " - the game may be stuck (details in the log)",
                        6000, RETRO_LOG_WARN);
        }
        if (onEvent) onEvent(*e);
        if (onUiEvent) {
            CoreEvent ce;
            ce.kind = CoreEvent::Kind::Event;
            ce.type = e->type;
            ce.a = e->a, ce.b = e->b, ce.c = e->c, ce.d = e->d;
            if (e->s) ce.text = e->s;
            emitUi(std::move(ce));
        }
        return true;
    }

    // ---- reported unsupported by the app -----------------------------------------
    case RETRO_ENVIRONMENT_GET_MICROPHONE_INTERFACE:
    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
    case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
    default:
        return false;
    }
}

// ---- content -------------------------------------------------------------------
bool CoreHost::startSession(std::string& err) {
    g_logVerbose = logVerbose_;
    // Tear down the previous game completely (the iOS path: statics are reset in
    // retro_unload_game / retro_deinit, then the whole init sequence runs again).
    stopSession();

    categories_.clear();
    defs_.clear();
    visibility_.clear();
    optSerial_++;
    controllerPorts_.clear();
    updateDisplayCb_ = nullptr;
    keyboardCb_ = nullptr;
    diskRegistered_ = false;
    variableUpdate_ = false;
    helloSeen_ = false;
    uiEvents_.dropLogsAndEvents();
    for (StagedBios& b : staged_) b = StagedBios();
    contentOverrideExts_.clear();
    supportsNoGame_ = false;
    minAudioLatencyMs_.store(0);
    g_audioBufferCb = nullptr;
    frame_ = Frame();
    frameCount_.store(0);

    retro_set_environment(envCb);
    retro_init();
    initialized_ = true;
    retro_set_video_refresh(videoCb);
    retro_set_audio_sample(audioSampleCb);
    retro_set_audio_sample_batch(audioBatchCb);
    retro_set_input_poll(inputPollCb);
    retro_set_input_state(inputStateCb);
    std::memset(&sysInfo_, 0, sizeof sysInfo_);
    retro_get_system_info(&sysInfo_);
    if (defs_.empty()) {
        err = "core did not register its options";
        publishSnapshot();
        return false;
    }
    return true;
}

bool CoreHost::finishLoad(const std::string& label, std::string& err, const std::string& opened) {
    std::memset(&avInfo_, 0, sizeof avInfo_);
    retro_get_system_av_info(&avInfo_);
    loaded_ = true;
    crashEvents_.store(0);
    discDirty_ = true;
    contentName_ = label;
    loadedPath_ = opened.empty() ? contentPath_ : opened;   // an .m3u: the playlist, while the content is its first disc
    loadedName_ = opened.empty() ? label : pathStem(opened);
    // A desktop host always has a keyboard.  The core only polls its keypad key fallbacks
    // (the only route to keypad 7-9, * and #) once it has seen a keyboard event, and
    // retro_deinit forgets that on every load, so a gamepad-only player would otherwise
    // never get a keypad.  (libretro.c: kb_event_seen / keyboard_event_cb.)
    if (announceKeyboard) sendKeyEvent(false, 0, 0, 0);
    loadSaveRam();
    // Re-read AV info after the SRAM write: region (PAL/NTSC) is latched at load.
    retro_get_system_av_info(&avInfo_);
    (void)err;
    // A load made through EmuThread::call() returns before the batch ends (afterCommands), so publish now: the UI
    // thread pins right after the call and must see the new table, AV info and discs.
    publishSnapshot();
    return true;
}

bool CoreHost::loadContent(const std::string& path, std::string& err) {
    if (noteOffOwner("loadContent")) { err = "called off the emulation thread"; return false; }
    if (inFrame_) { err = "the debugger has the game halted (resume it first)"; return false; }
    if (!startSession(err)) return false;

    // An .m3u: the core boots the first disc, and every disc goes in its disc list after the load.
    std::vector<std::string> discs;
    if (pathExt(path) == "m3u" && !readPlaylist(path, discs, err)) {
        stopSession();
        return false;
    }
    const std::string boot = discs.empty() ? path : discs[0];

    retro_game_info gi;
    std::memset(&gi, 0, sizeof gi);
    gi.path = boot.c_str();

    std::string ext = pathExt(boot);
    bool fullpath = false;
    {
        const std::string& list = contentOverrideExts_;
        size_t pos = 0;
        while (pos <= list.size()) {
            size_t bar = list.find('|', pos);
            std::string tok = list.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
            if (tok == ext) fullpath = true;
            if (bar == std::string::npos) break;
            pos = bar + 1;
        }
    }
    contentData_.clear();
    if (fullpath) {
        if (!fileExists(boot)) {
            err = "cannot open " + boot;
            stopSession();
            return false;
        }
    } else {
        if (!readFile(boot, contentData_) || contentData_.empty()) {
            err = "cannot read " + boot;
            stopSession();
            return false;
        }
        gi.data = contentData_.data();
        gi.size = contentData_.size();
    }

    contentPath_ = boot;   // an .m3u: the disc in the tray; the playlist is what was opened (finishLoad)
    if (!retro_load_game(&gi)) {
        err = "the core rejected " + boot;
        contentPath_.clear();
        stopSession();
        return false;
    }
    // The core's disc list starts empty even after it booted this CD (libretro.c: "nothing to insert"), so without
    // this the tray and disc menus never appear.  The count check keeps a core that lists its own disc working.
    if (discs.empty()) discs.push_back(path);
    if (isDiscImage(boot) && diskRegistered_ && disk_.get_num_images && disk_.get_num_images() == 0) {
        for (const std::string& d : discs)
            if (appendDiscImage(d) < 0) std::fprintf(stderr, "disc list: could not add %s (the list is full?)\n", d.c_str());
    }
    return finishLoad(pathStem(boot), err, path);
}

bool CoreHost::readPlaylist(const std::string& m3uPath, std::vector<std::string>& discs, std::string& err) {
    discs.clear();
    std::vector<unsigned char> raw;
    if (!readFile(m3uPath, raw)) {
        err = "cannot read " + m3uPath;
        return false;
    }
    for (const std::string& e : parseM3u(std::string(raw.begin(), raw.end()), pathDir(absolutePath(m3uPath)))) {
        const std::string d = absolutePath(e);
        if (!isDiscImage(d)) {   // a cartridge, or another playlist
            err = pathStem(m3uPath) + ".m3u lists " + e + ", which is not a CD image (cue, cdi, chd or iso)";
            return false;
        }
        if (!fileExists(d)) {
            err = pathStem(m3uPath) + ".m3u lists " + e + ", which does not exist";
            return false;
        }
        discs.push_back(d);
    }
    if (discs.empty()) {
        err = pathStem(m3uPath) + ".m3u lists no disc images";
        return false;
    }
    return true;
}

bool CoreHost::loadNoContent(std::string& err) {
    if (noteOffOwner("loadNoContent")) { err = "called off the emulation thread"; return false; }
    if (inFrame_) { err = "the debugger has the game halted (resume it first)"; return false; }
    if (!startSession(err)) return false;
    if (!supportsNoGame_) {
        err = "the core does not support booting without content";
        stopSession();
        return false;
    }
    contentPath_.clear();
    if (!retro_load_game(nullptr)) {
        err = "the core failed to boot without content";
        stopSession();
        return false;
    }
    // The core boots the CD BIOS unless it was told the cartridge boot ROM (VJ_ENV_GET_BOOT_TARGET, asked only
    // after HELLO with VJ_CAP_BIOS).  The CD BIOS has a CD unit with nothing in it: the disc list is empty, yet
    // inserting a disc is the reason to be there, so the disc menu must show (#646).
    emptyCdTray_ = !(helloSeen_ && announceBios && bootTarget_ == VJ_BOOT_CART_BOOT_ROM);
    return finishLoad("(no cartridge)", err);
}

void CoreHost::stopSession() {
    // The debug interface belongs to the core instance that is about to go away.
    if (debugIface_) {
        debugIface_ = nullptr;
        if (onDebugIface) onDebugIface(nullptr);
    }
    if (loaded_) {
        flushSaveRam(true);
        retro_unload_game();
        loaded_ = false;
    }
    if (initialized_) {
        retro_deinit();
        initialized_ = false;
    }
    // The core must not reference the image any more.
    contentData_.clear();
    contentData_.shrink_to_fit();
    contentPath_.clear();
    contentName_.clear();
    loadedPath_.clear();
    loadedName_.clear();
    saveHashValid_ = false;
    g_audioBufferCb = nullptr;
    updateDisplayCb_ = nullptr;   // belongs to the core instance that just went away
    keyboardCb_ = nullptr;
    diskRegistered_ = false;
    emptyCdTray_ = false;
    memDescs_.clear();   // the descriptors point into the core instance that just went away
    memMap_ = retro_memory_map{};
    discDirty_ = true;
    publishSnapshot();   // see finishLoad: an unload through call() must not leave the old discs / AV info pinned
}

bool CoreHost::memoryRegion(unsigned id, uint8_t*& data, size_t& size) const {
    data = nullptr;
    size = 0;
    if (noteOffOwner("memoryRegion")) return false;
    if (!loaded_) return false;
    data = (uint8_t*)retro_get_memory_data(id);
    size = data ? retro_get_memory_size(id) : 0;
    return data && size;
}

void CoreHost::unloadContent() {
    if (noteOffOwner("unloadContent")) return;
    if (inFrame_) return;   // never tear the core down from inside its own retro_run()
    stopSession();
}

void CoreHost::runFrame() {
    if (noteOffOwner("runFrame")) return;
    if (!loaded_ || inFrame_) return;
    if (g_audioBufferCb && audioBufferStatus) {
        bool active = false, underrun = false;
        unsigned occ = 0;
        audioBufferStatus(active, occ, underrun);
        g_audioBufferCb(active, occ, underrun);
    }
    inFrame_ = true;
    retro_run();   // a debugger halt parks the core in here and runs the UI (see debug_session.h)
    inFrame_ = false;
    frameCount_.fetch_add(1);
}

void CoreHost::reset() {
    if (noteOffOwner("reset")) return;
    if (loaded_ && !inFrame_) retro_reset();
}

void CoreHost::sendKeyEvent(bool down, unsigned retroKey, uint32_t character, uint16_t mods) {
    if (!onOwner()) {
        queue([this, down, retroKey, character, mods] { sendKeyEvent(down, retroKey, character, mods); });
        return;
    }
    if (keyboardCb_) keyboardCb_(down, retroKey, character, mods);
}

// ---- save states -----------------------------------------------------------------
bool CoreHost::saveStateToFile(const std::string& path, std::string& err) {
    if (noteOffOwner("saveStateToFile")) { err = "called off the emulation thread"; return false; }
    if (!loaded_) { err = "no game loaded"; return false; }
    if (inFrame_) { err = "the debugger has the game halted (resume it first)"; return false; }
    size_t size = retro_serialize_size();
    if (!size) { err = "the core cannot save states"; return false; }
    std::vector<unsigned char> buf(size);
    if (!retro_serialize(buf.data(), size)) { err = "retro_serialize failed"; return false; }
    if (!writeFileAtomic(path, buf.data(), size)) { err = "cannot write " + path; return false; }
    return true;
}

bool CoreHost::loadStateFromFile(const std::string& path, std::string& err) {
    if (noteOffOwner("loadStateFromFile")) { err = "called off the emulation thread"; return false; }
    if (!loaded_) { err = "no game loaded"; return false; }
    if (inFrame_) { err = "the debugger has the game halted (resume it first)"; return false; }
    std::vector<unsigned char> buf;
    if (!readFile(path, buf) || buf.empty()) { err = "cannot read " + path; return false; }
    if (!retro_unserialize(buf.data(), buf.size())) { err = "the core rejected the state"; return false; }
    return true;
}

// ---- SRAM -------------------------------------------------------------------------
std::string CoreHost::saveRamPath() const {
    if (loadedName_.empty()) return "";
    return saveDir_ + loadedName_ + ".srm";
}

void CoreHost::loadSaveRam() {
    saveHashValid_ = false;
    std::string p = saveRamPath();
    void* mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (p.empty() || !mem || !size) return;
    std::vector<unsigned char> buf;
    // A second copy of the app (instance.h) keeps its saves in its own folder; the first time it
    // plays a game it starts from the first copy's save, which it only ever reads.
    if (!fileExists(p) && !saveFallbackPath().empty()) readFile(saveFallbackPath(), buf);
    else readFile(p, buf);
    if (!buf.empty()) {
        // A shorter/longer file than the current layout is copied as far as it fits.
        std::memcpy(mem, buf.data(), std::min(size, buf.size()));
    }
    lastSaveHash_ = fnv1a((const unsigned char*)mem, size);
    saveHashValid_ = true;
}

bool CoreHost::flushSaveRam(bool force) {
    if (noteOffOwner("flushSaveRam")) return false;
    if (!loaded_) return true;
    std::string p = saveRamPath();
    void* mem = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (p.empty() || !mem || !size) return true;
    uint32_t h = fnv1a((const unsigned char*)mem, size);
    if (!force && saveHashValid_ && h == lastSaveHash_) return true;
    // Never create an all-default file for a game that never wrote anything.
    if (!saveHashValid_ && !force) { lastSaveHash_ = h; saveHashValid_ = true; return true; }
    if (saveHashValid_ && h == lastSaveHash_) return true;
    if (!writeFileAtomic(p, mem, size)) return false;
    lastSaveHash_ = h;
    saveHashValid_ = true;
    return true;
}

// ---- controllers / discs -------------------------------------------------------------
void CoreHost::setControllerDevice(unsigned port, unsigned device) {
    if (!onOwner()) {
        queue([this, port, device] { setControllerDevice(port, device); });
        return;
    }
    if (loaded_ && !inFrame_) retro_set_controller_port_device(port, device);
}

DiscInfo CoreHost::discInfo() {
    if (!onOwner()) return ui_.disc;
    return discInfoLive();
}

DiscInfo CoreHost::discInfoLive() {
    DiscInfo d;
    if (!loaded_ || !diskRegistered_ || !disk_.get_num_images) return d;
    d.count = disk_.get_num_images();
    // Not just "registered": the core registers disk control for a cartridge too, which must not show a disc menu.
    d.available = d.count > 0 || emptyCdTray_;
    d.ejected = disk_.get_eject_state ? disk_.get_eject_state() : false;
    d.index = disk_.get_image_index ? disk_.get_image_index() : 0;
    for (unsigned i = 0; i < d.count; i++) {
        char image[4096] = {0};
        d.paths.push_back(disk_.get_image_path && disk_.get_image_path(i, image, sizeof image) ? image : "");
        char label[256] = {0};
        std::string name;
        if (disk_.get_image_label && disk_.get_image_label(i, label, sizeof label) && label[0]) {
            // The core's label is the path after its last '/', so a Windows path ("D:\Games\Disc 1.chd") arrives
            // whole: keep only the file name, as on every other OS.
            name = label;
            const size_t sep = name.find_last_of("/\\");
            if (sep != std::string::npos) name.erase(0, sep + 1);
        }
        else if (disk_.get_image_path && disk_.get_image_path(i, label, sizeof label) && label[0])
            name = pathStem(label);
        else
            name = "Disc " + std::to_string(i + 1);
        d.labels.push_back(name);
    }
    return d;
}

void CoreHost::discSetEject(bool eject) {
    if (!onOwner()) {
        queue([this, eject] { discSetEject(eject); });
        return;
    }
    if (loaded_ && !inFrame_ && diskRegistered_ && disk_.set_eject_state) {
        const bool ok = disk_.set_eject_state(eject);
        if (!eject) followMountedDisc(ok);
        discDirty_ = true;
        publishSnapshot();
    }
}

void CoreHost::discSetIndex(unsigned idx) {
    if (!onOwner()) {
        queue([this, idx] { discSetIndex(idx); });
        return;
    }
    if (loaded_ && !inFrame_ && diskRegistered_ && disk_.set_image_index) {
        followMountedDisc(disk_.set_image_index(idx));
        discDirty_ = true;
        publishSnapshot();
    }
}

// After an insert: the disc that booted becomes the content.  Only a call that worked and left the tray closed counts.
// The core moves its image index BEFORE it tries the insert and keeps it there when the insert fails (it puts the
// previous disc back and leaves the tray open), so the index alone does not say which disc is in.
void CoreHost::followMountedDisc(bool ok) {
    if (!ok || !disk_.get_eject_state || disk_.get_eject_state() || !disk_.get_image_index || !disk_.get_image_path)
        return;
    char path[4096] = {0};
    if (!disk_.get_image_path(disk_.get_image_index(), path, sizeof path) || !path[0] || contentPath_ == path) return;
    contentPath_ = path;
    contentName_ = pathStem(contentPath_);
    discSwaps_++;
}

bool CoreHost::isDiscImage(const std::string& path) {
    // The core's own is_cd_content test (libretro.c).  A cartridge must never reach the disc list: the core registers
    // disk control for a cartridge too, and closing the tray would try to mount the cartridge as a disc.
    const std::string e = pathExt(path);
    return e == "cue" || e == "cdi" || e == "chd" || e == "iso";
}

int CoreHost::discAppend(const std::string& path) {
    if (noteOffOwner("discAppend")) return -1;
    if (!loaded_ || inFrame_) return -1;
    const int idx = appendDiscImage(path);
    if (idx >= 0) {
        discDirty_ = true;
        publishSnapshot();
    }
    return idx;
}

int CoreHost::appendDiscImage(const std::string& path) {
    if (!isDiscImage(path) || !diskRegistered_ || !disk_.add_image_index || !disk_.replace_image_index ||
        !disk_.get_num_images)
        return -1;
    if (!disk_.add_image_index()) return -1;   // the list is full
    const unsigned idx = disk_.get_num_images() - 1;
    retro_game_info gi;
    std::memset(&gi, 0, sizeof gi);
    gi.path = path.c_str();   // the core copies the path
    if (!disk_.replace_image_index(idx, &gi)) {
        disk_.replace_image_index(idx, nullptr);   // drop the empty slot again
        return -1;
    }
    return (int)idx;
}

// ---- threading (#967) -------------------------------------------------------------
void CoreHost::setOwner(std::thread::id owner) {
    if (owner != std::thread::id()) {
        // Runs on the new emulation thread while the UI thread waits in EmuThread::start(): the copies start equal.
        uiUser_ = userValues_;
        uiSession_ = sessionValues_;
        pubOptSerial_ = optSerial_ - 1;   // publish the table at once
        discDirty_ = true;
    }
    owner_.store(owner);
    if (owner != std::thread::id()) {
        publishSnapshot();
        std::lock_guard<std::mutex> l(snapMu_);
        ui_ = pub_;
    }
}

bool CoreHost::noteOffOwner(const char* what) const {
    if (onOwner()) return false;
    ownerViolations_.fetch_add(1);
    std::lock_guard<std::mutex> l(violationMu_);
    if (violationSeen_.insert(what).second)
        std::fprintf(stderr, "[core_host] owner violation: %s called off the emulation thread\n", what);
    return true;
}

bool CoreHost::noteWaitingRead(const char* what) const {
    if (!uiWaiting_.load() || onOwner()) return false;
    ownerViolations_.fetch_add(1);
    std::lock_guard<std::mutex> l(violationMu_);
    if (violationSeen_.insert(what).second)
        std::fprintf(stderr, "[core_host] owner violation: %s while the UI waits in a call\n", what);
    return true;
}

void CoreHost::queue(std::function<void()> fn) {
    if (post) {
        post(std::move(fn));
        return;
    }
    // Threaded with nowhere to queue: running fn() here would re-enter the same off-owner method and recurse, and
    // would touch the core's copy from the UI thread.  Count it and drop the work.
    noteOffOwner("queue without post");
}

const OptionTable& CoreHost::uiTable() const {
    static const OptionTable kNone;
    return ui_.options ? *ui_.options : kNone;
}

void CoreHost::publishSnapshot() {
    if (!threaded() || !onOwner()) return;
    std::shared_ptr<const OptionTable> table;
    if (optSerial_ != pubOptSerial_) {
        std::shared_ptr<OptionTable> t = std::make_shared<OptionTable>();
        t->categories = categories_;
        t->defs = defs_;
        t->visibility = visibility_;
        t->locked = locked_;
        table = t;
        pubOptSerial_ = optSerial_;
    }
    const bool discChanged = discDirty_;
    DiscInfo disc;
    if (discChanged) {
        disc = discInfoLive();
        discDirty_ = false;
    }
    std::lock_guard<std::mutex> l(snapMu_);
    if (table) pub_.options = table;
    pub_.optionPushes = optionPushes_;
    pub_.fps = avInfo_.timing.fps;
    pub_.sampleRate = avInfo_.timing.sample_rate;
    pub_.aspect = aspectLive();
    if (discChanged) {
        pub_.disc = disc;
        pub_.contentPath = contentPath_;
        pub_.contentName = contentName_;
        pub_.discSwaps = discSwaps_;
    }
}

void CoreHost::pinSnapshot() {
    if (onOwner()) {
        // Inline (or after the emulation thread stopped) the UI thread is the owner: option events queued just before
        // the thread went away still have to be applied.  (On the emulation thread itself this does nothing: the UI
        // applies its own queue.)  Inline the queue is normally empty.
        if (!threaded())
            for (CoreEvent& e : uiEvents_.take()) applyUiEvent(e);
        return;
    }
    {
        std::lock_guard<std::mutex> l(snapMu_);
        ui_ = pub_;
    }
    for (CoreEvent& e : uiEvents_.take()) applyUiEvent(e);
}

}  // namespace vj
