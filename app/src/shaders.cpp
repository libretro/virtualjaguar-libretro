#include "shaders.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "paths.h"
#include "present_util.h"

#if defined(VJ_HAVE_LIBRASHADER)
#define LIBRA_RUNTIME_OPENGL
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
// MIT-licensed C headers from the librashader repo, fetched at configure time.
#include "librashader_ld.h"
// GL entry points from the ImGui GL3 backend's loader (declarations only).
#include "imgui_impl_opengl3_loader.h"
#endif

namespace vj {

#if defined(VJ_HAVE_LIBRASHADER)

namespace {

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGBA
#define GL_RGBA 0x1908
#endif
#ifndef GL_TEXTURE_SWIZZLE_A
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

typedef void (*BindFramebufferFn)(unsigned, unsigned);

const void* glLoader(const char* name) {
    return reinterpret_cast<const void*>(SDL_GL_GetProcAddress(name));
}

}  // namespace

struct ShaderChain::Impl {
    SDL_SharedObject* lib = nullptr;
    libra_instance_t inst = __librashader_make_null_instance();
    libra_gl_filter_chain_t chain = nullptr;
    unsigned outTex = 0;
    unsigned outW = 0, outH = 0;
    BindFramebufferFn bindFbo = nullptr;
    bool haveFnPtrs = false;

    // The inputs the output texture was last rendered from.  A UI frame that
    // would feed the chain the very same inputs (the display refreshes faster
    // than the core, the game is paused, a window repaint) reuses the texture:
    // same pixels, no GPU work, and the chain's history/feedback textures see
    // each emulated frame exactly once, as they do in RetroArch.
    bool haveOut = false;
    uint64_t keyFrame = 0;
    unsigned keySrcTex = 0, keySrcW = 0, keySrcH = 0, keyOutW = 0, keyOutH = 0;
    float keyAspect = 0, keyFps = 0;

    // Turns a librashader error into text and releases it.
    std::string take(libra_error_t e) {
        if (!e) return std::string();
        std::string s = "unknown librashader error";
        char* msg = nullptr;
        if (inst.error_write && inst.error_write(e, &msg) == 0 && msg) {
            s = msg;
            if (inst.error_free_string) inst.error_free_string(&msg);
        }
        if (inst.error_free) inst.error_free(&e);
        // librashader messages can be long and multi-line; the OSD wants one line.
        for (char& c : s)
            if (c == '\n' || c == '\r') c = ' ';
        if (s.size() > 2000) s.resize(2000);
        return s;
    }

    void freeChain() {
        haveOut = false;
        if (chain && inst.gl_filter_chain_free) take(inst.gl_filter_chain_free(&chain));
        chain = nullptr;
    }
    void freeOutTex() {
        if (outTex) {
            GLuint t = outTex;
            glDeleteTextures(1, &t);
            outTex = 0;
        }
        outW = outH = 0;
    }
};

namespace {

template <class Fn>
bool loadSym(SDL_SharedObject* lib, const char* name, Fn& slot, std::string& missing) {
    SDL_FunctionPointer p = SDL_LoadFunction(lib, name);
    if (!p) {
        missing += std::string(missing.empty() ? "" : ", ") + name;
        return false;
    }
    slot = reinterpret_cast<Fn>(p);
    return true;
}

}  // namespace

#else   // no header: a stub that is permanently "unavailable"

struct ShaderChain::Impl {};

#endif

ShaderChain::ShaderChain() : impl_(new Impl) {}
ShaderChain::~ShaderChain() = default;

bool ShaderChain::builtWithHeaders() {
#if defined(VJ_HAVE_LIBRASHADER)
    return true;
#else
    return false;
#endif
}

bool ShaderChain::runtimeBundled() {
#if defined(VJ_LIBRASHADER_BUNDLED)
    return true;
#else
    return false;
#endif
}

const char* ShaderChain::libraryFileName() {
#if defined(_WIN32)
    return "librashader.dll";
#elif defined(__APPLE__)
    return "librashader.dylib";
#else
    return "librashader.so";
#endif
}

void ShaderChain::loadSettings(const Config& cfg, const std::string& dataRoot) {
    enabled = cfg.getBool("shader.enabled", false);
    preset = cfg.get("shader.preset", "");
    folder = cfg.get("shader.folder", "");

    // Where the library may live, most specific first.  (VJ_LIBRASHADER, handled
    // in probeLibrary, overrides all of this.)
    std::vector<std::string> d;
    const char* base = SDL_GetBasePath();
    if (base && *base) {
        d.push_back(base);
#if defined(__APPLE__)
        d.push_back(std::string(base) + "../Frameworks/");   // inside a .app bundle
#endif
    }
#ifndef VJ_MAC_APP_STORE
    if (!dataRoot.empty()) d.push_back(dataRoot + "librashader/");
#if defined(__APPLE__)
    d.push_back("/opt/homebrew/lib/");   // Homebrew, which dlopen's fallback path misses
    d.push_back("/usr/local/lib/");
#endif
#endif
    dirs_ = std::move(d);
}

void ShaderChain::saveSettings(Config& cfg) const {
    cfg.setBool("shader.enabled", enabled);
    cfg.set("shader.preset", preset);
    cfg.set("shader.folder", folder);
}

void ShaderChain::setNotice(const std::string& s) { notice_ = s; }

bool ShaderChain::takeNotice(std::string& out) {
    if (notice_.empty()) return false;
    out = notice_;
    notice_.clear();
    return true;
}

// ---------------------------------------------------------------------------------------
// library
// ---------------------------------------------------------------------------------------

bool ShaderChain::libraryLoaded() const {
#if defined(VJ_HAVE_LIBRASHADER)
    return impl_ && impl_->lib && impl_->haveFnPtrs;
#else
    return false;
#endif
}

bool ShaderChain::probeLibrary(bool retry) {
#if !defined(VJ_HAVE_LIBRASHADER)
    (void)retry;
    probed_ = true;
    libStatus_ = LibraryStatus::NoSupport;
    libErr_ = "this build has no librashader support (its header could not be fetched at configure time)";
    return false;
#else
    if (probed_ && !retry) return libraryLoaded();
    if (libraryLoaded()) return true;
    probed_ = true;
    libErr_.clear();
    libPath_.clear();
    libStatus_ = LibraryStatus::NotFound;
    Impl& I = *impl_;

    const std::string leaf = libraryFileName();
    std::vector<std::string> cands;
    std::string searched;
#ifdef VJ_MAC_APP_STORE
    const char* env = nullptr; // Store code comes only from this signed bundle.
#else
    const char* env = std::getenv("VJ_LIBRASHADER");
#endif
    bool forced = env && *env;
    auto addDir = [&](std::string d) {
        if (d.empty()) return;
        if (d.back() != '/' && d.back() != '\\') d += '/';
        cands.push_back(d + leaf);
    };
    if (forced) {
        // An explicit choice is the only choice: a wrong path means "missing", not
        // "try something else", so a test (or a user) can rule the library out.
        std::string e = env;
        SDL_PathInfo pi;
        if (SDL_GetPathInfo(e.c_str(), &pi) && pi.type == SDL_PATHTYPE_DIRECTORY) addDir(e);
        else cands.push_back(e);
        searched = "VJ_LIBRASHADER=" + e;
    } else {
        for (const std::string& d : dirs_) addDir(d);
#ifndef VJ_MAC_APP_STORE
        cands.push_back(leaf);   // the system loader path
#endif
        searched = "the app folder, the data folder and the system library path";
    }

    // loadErr: the last file that EXISTS and could not be used (reported as "failed to load").
    // A bare name that the system loader cannot find only means "not found anywhere".
    std::string loadErr;
    for (const std::string& c : cands) {
        bool bare = c.find('/') == std::string::npos && c.find('\\') == std::string::npos;
        if (!bare && !fileExists(c)) continue;
#if defined(_WIN32)
        // A file that is not a loadable DLL must fail quietly, not raise a modal
        // "bad image" dialog that blocks the app (or a headless test) forever.
        UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
        SDL_SharedObject* h = SDL_LoadObject(c.c_str());
#if defined(_WIN32)
        SetErrorMode(oldMode);
#endif
        if (!h) {
            if (!bare) loadErr = c + ": " + SDL_GetError();
            continue;
        }
        // ABI first: an incompatible library must not have its other symbols read.
        libra_instance_t inst = __librashader_make_null_instance();
        std::string missing;
        bool ok = loadSym(h, "libra_instance_abi_version", inst.instance_abi_version, missing);
        ok &= loadSym(h, "libra_instance_api_version", inst.instance_api_version, missing);
        if (!ok) {
            loadErr = c + " is not librashader (no libra_instance_abi_version)";
            SDL_UnloadObject(h);
            continue;
        }
        size_t abi = inst.instance_abi_version();
        if (abi != LIBRASHADER_CURRENT_ABI) {
            loadErr = c + " has librashader ABI " + std::to_string(abi) + ", this app needs " +
                      std::to_string((size_t)LIBRASHADER_CURRENT_ABI);
            SDL_UnloadObject(h);
            continue;
        }
        loadSym(h, "libra_preset_create_with_options", inst.preset_create_with_options, missing);
        loadSym(h, "libra_preset_free", inst.preset_free, missing);
        loadSym(h, "libra_preset_set_param", inst.preset_set_param, missing);
        loadSym(h, "libra_preset_get_runtime_params", inst.preset_get_runtime_params, missing);
        loadSym(h, "libra_preset_free_runtime_params", inst.preset_free_runtime_params, missing);
        loadSym(h, "libra_error_write", inst.error_write, missing);
        loadSym(h, "libra_error_free", inst.error_free, missing);
        loadSym(h, "libra_error_free_string", inst.error_free_string, missing);
        loadSym(h, "libra_gl_filter_chain_create", inst.gl_filter_chain_create, missing);
        loadSym(h, "libra_gl_filter_chain_frame", inst.gl_filter_chain_frame, missing);
        loadSym(h, "libra_gl_filter_chain_free", inst.gl_filter_chain_free, missing);
        loadSym(h, "libra_gl_filter_chain_set_param", inst.gl_filter_chain_set_param, missing);
        if (!missing.empty()) {
            loadErr = c + " is too old or incomplete (missing " + missing + ")";
            SDL_UnloadObject(h);
            continue;
        }
        I.lib = h;
        I.inst = inst;
        I.haveFnPtrs = true;
        I.inst.instance_loaded = true;
        libPath_ = bare ? c : absolutePath(c);
        apiVersion_ = (unsigned)inst.instance_api_version();
        libStatus_ = LibraryStatus::Loaded;
        return true;
    }
    if (loadErr.empty()) {
        libStatus_ = LibraryStatus::NotFound;
        libErr_ = std::string(leaf) + " not found (looked in " + searched + ")";
    } else {
        libStatus_ = LibraryStatus::LoadFailed;
        libErr_ = loadErr;
    }
    return false;
#endif
}

// ---------------------------------------------------------------------------------------
// preset
// ---------------------------------------------------------------------------------------

#if defined(VJ_HAVE_LIBRASHADER)

namespace {

struct Created {
    libra_shader_preset_t preset = nullptr;
    std::string err;
};

Created createPreset(ShaderChain::Impl& I, unsigned apiVersion, const std::string& path) {
    Created c;
    libra_preset_opt_t opt;
    std::memset(&opt, 0, sizeof opt);
    opt.version = apiVersion < (unsigned)LIBRASHADER_CURRENT_VERSION ? apiVersion : (unsigned)LIBRASHADER_CURRENT_VERSION;
    opt.original_aspect_uniforms = apiVersion >= 2;   // OriginalAspect / aspect-aware CRT shaders
    opt.frametime_uniforms = apiVersion >= 2;         // OriginalFPS / FrametimeDelta
    opt.sensor_uniforms = false;
    // A null wildcard context: 0.12.0 ignores a supplied one (fixed in 0.12.1), and
    // the $CORE$/$CONTENT-DIR$ auto-selection it enables is RetroArch UI machinery.
    libra_error_t e = I.inst.preset_create_with_options(path.c_str(), nullptr, &opt, &c.preset);
    if (e) {
        c.err = I.take(e);
        if (c.err.empty()) c.err = "librashader rejected the preset";
        c.preset = nullptr;
    }
    return c;
}

void fetchParams(ShaderChain::Impl& I, libra_shader_preset_t preset, std::vector<ShaderParam>& out) {
    libra_preset_param_list_t list;
    std::memset(&list, 0, sizeof list);
    libra_error_t e = I.inst.preset_get_runtime_params(&preset, &list);
    if (e) {
        I.take(e);
        return;
    }
    std::vector<ShaderParam> fresh;
    for (uint64_t i = 0; i < list.length && list.parameters; i++) {
        const libra_preset_param_t& p = list.parameters[i];
        ShaderParam sp;
        sp.name = p.name ? p.name : "";
        sp.description = p.description ? p.description : sp.name;
        sp.initial = p.initial;
        sp.minimum = p.minimum;
        sp.maximum = p.maximum;
        sp.step = p.step;
        sp.value = p.initial;
        if (!sp.name.empty()) fresh.push_back(std::move(sp));
    }
    if (I.inst.preset_free_runtime_params) I.take(I.inst.preset_free_runtime_params(list));
    // keep the user's edits for parameters that are still there
    for (ShaderParam& np : fresh)
        for (const ShaderParam& op : out)
            if (op.name == np.name) np.value = std::min(np.maximum, std::max(np.minimum, op.value));
    out = std::move(fresh);
}

}  // namespace

#endif

bool ShaderChain::usePreset(const std::string& path, std::string& err) {
    PresetInfo info = inspectPreset(path);
    if (!info.ok) {
        err = info.error;
        return false;
    }
    // Re-validating the preset already in use keeps the user's parameter edits.
    std::vector<ShaderParam> newParams = (path == preset) ? params_ : std::vector<ShaderParam>();
#if defined(VJ_HAVE_LIBRASHADER)
    if (probeLibrary()) {
        // librashader parses the preset (no GL needed): a malformed one is caught
        // here, with its own message, instead of on the first frame.
        Created c = createPreset(*impl_, apiVersion_, path);
        if (!c.preset) {
            err = c.err;
            return false;
        }
        fetchParams(*impl_, c.preset, newParams);
        impl_->inst.preset_free(&c.preset);
    }
#endif
    params_ = std::move(newParams);
    preset = path;
    failed_ = false;
    dirty_ = true;
    lastErr_.clear();
    return true;
}

void ShaderChain::clearPreset() {
    preset.clear();
    params_.clear();
    dirty_ = true;
    failed_ = false;
}

void ShaderChain::resetParams() {
    for (size_t i = 0; i < params_.size(); i++) {
        params_[i].value = params_[i].initial;
        paramChanged(i);
    }
}

void ShaderChain::paramChanged(size_t i) {
#if defined(VJ_HAVE_LIBRASHADER)
    if (i >= params_.size() || !impl_->chain) return;
    impl_->haveOut = false;   // the next frame must re-render with the new value
    libra_error_t e = impl_->inst.gl_filter_chain_set_param(&impl_->chain, params_[i].name.c_str(), params_[i].value);
    if (e) impl_->take(e);
#else
    (void)i;
#endif
}

// ---------------------------------------------------------------------------------------
// frames
// ---------------------------------------------------------------------------------------

void ShaderChain::probeGlCaps() {
#if defined(VJ_HAVE_LIBRASHADER)
    if (glAoA_ >= 0) return;
    GLint major = 0, minor = 0, n = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major <= 0) return;   // no context current: try again later
    glVer_ = std::to_string(major) + "." + std::to_string(minor);
    // GLSL to generate: the context's own (4.1 on macOS, usually 4.6 elsewhere), capped at
    // librashader's highest.  330 everywhere threw away GLSL 4.00 (textureGather with a
    // component, packUnorm4x8, bitCount/findLSB) on contexts that have it.
    int glsl = major * 100 + minor * 10;
    glsl_ = (unsigned)(glsl < 330 ? 330 : glsl > 460 ? 460 : glsl);
    bool aoa = major > 4 || (major == 4 && minor >= 3);
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; !aoa && i < n; i++) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, (GLuint)i));
        if (e && std::strcmp(e, "GL_ARB_arrays_of_arrays") == 0) aoa = true;
    }
    glAoA_ = aoa ? 1 : 0;
#endif
}

void ShaderChain::destroyGL() {
#if defined(VJ_HAVE_LIBRASHADER)
    if (!impl_) return;
    impl_->freeChain();
    impl_->freeOutTex();
    dirty_ = true;
#endif
}

unsigned ShaderChain::render(const ShaderFrame& f) {
#if !defined(VJ_HAVE_LIBRASHADER)
    (void)f;
    if (enabled && !preset.empty() && !noticedMissing_) {
        noticedMissing_ = true;
        setNotice("Shaders are on but this build has no librashader support; showing the plain picture");
    }
    return 0;
#else
    if (!enabled || preset.empty()) return 0;
    if (!probeLibrary()) {
        if (!noticedMissing_) {
            noticedMissing_ = true;
            setNotice(libStatus_ == LibraryStatus::LoadFailed
                          ? "Shaders are on but the shader runtime failed to load (see Settings > Video > Shader); showing the plain picture"
                          : "Shaders are on but the shader runtime was not found; showing the plain picture");
        }
        return 0;
    }
    if (failed_ || f.srcTex == 0 || f.srcW == 0 || f.srcH == 0 || f.outW == 0 || f.outH == 0) return 0;
    Impl& I = *impl_;
    if (!I.bindFbo)
        I.bindFbo = reinterpret_cast<BindFramebufferFn>(SDL_GL_GetProcAddress("glBindFramebuffer"));

    probeGlCaps();
    auto fail = [&](const std::string& why) {
        failed_ = true;
        lastErr_ = why;
        I.freeChain();
        std::string brief = why.size() > 160 ? why.substr(0, 160) + "..." : why;   // full text stays in lastError()
        // SPIRV-Cross's GLSL for a multi-dimensional array needs GL 4.3 or this extension
        // (see presetsNeedingArraysOfArrays()); say so instead of quoting the GL compiler.
        if (why.find("GL_ARB_arrays_of_arrays") != std::string::npos) {
            brief = "it needs OpenGL 4.3 (arrays of arrays)";
            if (!glVer_.empty()) brief += "; this system has OpenGL " + glVer_;
            lastErr_ = "This preset needs OpenGL 4.3 (GL_ARB_arrays_of_arrays), which " +
                       (glVer_.empty() ? std::string("this system") : "OpenGL " + glVer_) +
                       " does not provide (macOS stops at 4.1). librashader: " + why;
        }
        setNotice("Shader " + pathStem(preset) + " failed (" + brief + "); showing the plain picture");
        return 0u;
    };

    if (dirty_ || !I.chain) {
        I.freeChain();
        PresetInfo pi = inspectPreset(preset);
        if (!pi.ok) return fail(pi.error);
        Created c = createPreset(I, apiVersion_, preset);
        if (!c.preset) return fail(c.err);
        // Parameters the user moved away from the preset's defaults.
        if (params_.empty()) fetchParams(I, c.preset, params_);
        for (const ShaderParam& p : params_)
            if (p.value != p.initial) I.take(I.inst.preset_set_param(&c.preset, p.name.c_str(), p.value));
        filter_chain_gl_opt_t co;
        std::memset(&co, 0, sizeof co);
        co.version = apiVersion_ < (unsigned)LIBRASHADER_CURRENT_VERSION ? apiVersion_ : (unsigned)LIBRASHADER_CURRENT_VERSION;
        co.glsl_version = (uint16_t)glsl_;   // the context's, from probeGlCaps()
        co.use_dsa = false;   // GL 3.3 core; DSA (and with it librashader's shader cache) needs 4.5
        co.force_no_mipmaps = false;
        co.disable_cache = false;
        libra_error_t e = I.inst.gl_filter_chain_create(&c.preset, glLoader, &co, &I.chain);
        // The create call takes the preset (and nulls our handle); free only a leftover.
        if (c.preset) I.inst.preset_free(&c.preset);
        if (e) {
            I.chain = nullptr;
            std::string why = I.take(e);
            return fail(why.empty() ? "could not build the filter chain" : why);
        }
        dirty_ = false;
    }

    if (!I.outTex || I.outW != f.outW || I.outH != f.outH) {
        I.haveOut = false;
        if (!I.outTex) {
            GLuint t = 0;
            glGenTextures(1, &t);
            I.outTex = t;
        }
        glBindTexture(GL_TEXTURE_2D, I.outTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Shader output alpha is whatever the preset writes; the presenter blends,
        // so sample it as opaque.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)f.outW, (GLsizei)f.outH, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        I.outW = f.outW;
        I.outH = f.outH;
    }

    if (reuseOutput && I.haveOut && I.keyFrame == f.frameCount && I.keySrcTex == f.srcTex && I.keySrcW == f.srcW &&
        I.keySrcH == f.srcH && I.keyOutW == f.outW && I.keyOutH == f.outH && I.keyAspect == f.aspect &&
        I.keyFps == f.fps) {
        reuses_++;
        return I.outTex;
    }

    libra_image_gl_t in;
    in.handle = f.srcTex;
    in.format = GL_RGBA8;
    in.width = f.srcW;
    in.height = f.srcH;
    libra_image_gl_t out;
    out.handle = I.outTex;
    out.format = GL_RGBA8;
    out.width = f.outW;
    out.height = f.outH;
    libra_viewport_t vp;
    vp.x = 0;
    vp.y = 0;
    vp.width = f.outW;
    vp.height = f.outH;
    frame_gl_opt_t fo;
    std::memset(&fo, 0, sizeof fo);
    fo.version = apiVersion_ < (unsigned)LIBRASHADER_CURRENT_VERSION ? apiVersion_ : (unsigned)LIBRASHADER_CURRENT_VERSION;
    fo.frame_direction = 1;
    fo.rotation = 0;
    fo.total_subframes = 1;
    fo.current_subframe = 1;
    fo.aspect_ratio = f.aspect;
    fo.frames_per_second = f.fps > 0 ? f.fps : 60.0f;
    fo.frametime_delta = f.fps > 0 ? (uint32_t)(1000.0f / f.fps + 0.5f) : 16;
    fo.color_space = LIBRA_COLOR_SPACE_SDR;
    fo.brightness_nits = 200.0f;

    libra_error_t e = I.inst.gl_filter_chain_frame(&I.chain, (size_t)f.frameCount, in, out, &vp, nullptr, &fo);

    // The chain leaves its own framebuffer, viewport and texture unit behind; hand
    // the window back (ImGui re-establishes everything else when it renders).
    if (I.bindFbo) I.bindFbo(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);

    if (e) {
        std::string why = I.take(e);
        return fail(why.empty() ? "frame error" : why);
    }
    runs_++;
    I.haveOut = true;
    I.keyFrame = f.frameCount;
    I.keySrcTex = f.srcTex;
    I.keySrcW = f.srcW;
    I.keySrcH = f.srcH;
    I.keyOutW = f.outW;
    I.keyOutH = f.outH;
    I.keyAspect = f.aspect;
    I.keyFps = f.fps;
    return I.outTex;
#endif
}

}  // namespace vj
