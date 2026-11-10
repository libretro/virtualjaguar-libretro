// RetroArch .slangp shader presets on the GL 3.3 presenter, through librashader.
//
// librashader (MPL-2.0) is loaded at RUNTIME: its MIT-licensed C loader header
// is fetched at configure time (pinned by SHA256, see app/CMakeLists.txt) and
// the library itself is looked for next to the executable, in the data folder
// and on the system path.  If it is not there, or is the wrong ABI, or a preset
// does not compile, the app shows the plain picture and says so in one line;
// nothing here is ever a failure.
//
// Integer scaling and the aspect modes are NOT part of this: they live in
// computePresentRect() (present_util.h) and apply identically with or without
// a shader.  The chain renders into an offscreen texture sized to that
// rectangle, which the presenter then draws.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vj {

class Config;

struct ShaderParam {
    std::string name;
    std::string description;
    float initial = 0, minimum = 0, maximum = 1, step = 0.01f;
    float value = 0;   // current (what the UI edits)
};

struct ShaderFrame {
    unsigned srcTex = 0;               // the core frame (GL texture)
    unsigned srcW = 0, srcH = 0;
    unsigned outW = 0, outH = 0;       // size of the rectangle the result is drawn in
    uint64_t frameCount = 0;           // advances once per emulated frame
    float aspect = 0;                  // display aspect (what the player sees)
    float fps = 0;                     // the core's field rate
};

class ShaderChain {
public:
    ShaderChain();
    ~ShaderChain();
    ShaderChain(const ShaderChain&) = delete;
    ShaderChain& operator=(const ShaderChain&) = delete;

    // ---- settings (persisted by the caller through loadSettings/saveSettings) ----
    bool enabled = false;       // user switch
    std::string preset;         // path to a .slangp
    std::string folder;         // a slang-shaders checkout or any folder of presets
    // `dataRoot` is the app's data folder; <dataRoot>librashader/ is searched for the library.
    void loadSettings(const Config& cfg, const std::string& dataRoot);
    void saveSettings(Config& cfg) const;

    // ---- the build default (#1154) ------------------------------------------------------
    // The preset a config that never chose one starts on: a file name inside the bundled
    // shaders/ folder ("vj-scanlines.slangp"), or "off".  The app passes VJ_SHADER_DEFAULT_PRESET
    // (app/CMakeLists.txt's VJ_SHADER_DEFAULT); the tests set theirs.  Must precede loadSettings().
    void setBuildDefault(const std::string& presetOrOff) { buildDefault_ = presetOrOff; }
    const std::string& buildDefault() const { return buildDefault_; }
    // True while the switch and preset are what loadSettings() took from the build default and the
    // user has not moved them.  saveSettings() then writes no shader.enabled / shader.preset, so a
    // later build can move the default under people who never chose; the first change the user
    // makes stores their choice (with shader.chosen) and it wins from then on.
    bool usingBuildDefault() const { return defaultApplied_ && enabled == loadedEnabled_ && preset == loadedPreset_; }
    // Re-runs usePreset on the current preset (Settings > Reset re-read the config under us).
    void reloadPreset();
    // The bundled presets: shaders/ beside the executable (ours plus the slang-shaders subset).
    // The tests point it at a temporary folder.
    static std::string builtinPresetDir();
    static void setBuiltinPresetDir(const std::string& dir);
    // A --shader argument as a full path: a .slangp file path, or a name inside the bundled folder
    // with or without .slangp ("vj-scanlines", "slang-shaders/crt/crt-easymode", "crt/crt-easymode").
    // "" when nothing matches.
    static std::string resolvePresetArg(const std::string& arg);

    // ---- the librashader library ----------------------------------------------------
    // Directories searched, in order, before the system loader path.  The env var
    // VJ_LIBRASHADER (a file, or a directory) is tried first; setting it to a
    // path that does not exist forces "library missing" (used by the tests).
    void setLibrarySearchDirs(std::vector<std::string> dirs) { dirs_ = std::move(dirs); }
    // Tries to load once; later calls return the cached answer unless `retry`.
    bool probeLibrary(bool retry = false);
    bool libraryLoaded() const;
    const std::string& libraryPath() const { return libPath_; }
    const std::string& libraryError() const { return libErr_; }
    // Why there is no library, for the Shader section.  Loaded: there is one.  NoSupport: the build
    // has no librashader headers.  NotFound: no librashader file where we looked (or on the
    // system path).  LoadFailed: a file was found and could not be used (wrong architecture, wrong
    // ABI, not librashader, a missing dependency); libraryError() has the loader's reason.
    enum class LibraryStatus { Loaded, NoSupport, NotFound, LoadFailed };
    LibraryStatus libraryStatus() const { return libStatus_; }
    // True when this build ships (or staged beside the executable) a runtime: a NotFound then means
    // the file went missing, not that this build never had one.
    static bool runtimeBundled();
    unsigned libraryApiVersion() const { return apiVersion_; }
    static const char* libraryFileName();   // librashader.dylib / .so / .dll
    static bool builtWithHeaders();         // false when configure could not fetch the header

    // ---- the preset ---------------------------------------------------------------------
    // Validates the file (inspectPreset), has librashader parse it when the library
    // is there, collects its parameters, and schedules a chain rebuild.  Returns
    // false with `err` set and leaves the previous preset untouched.
    bool usePreset(const std::string& path, std::string& err);
    void clearPreset();
    std::vector<ShaderParam>& params() { return params_; }
    void paramChanged(size_t index);        // pushes params()[index].value into the live chain
    void resetParams();

    // ---- what the GL context can compile (context current; render() also calls it) ----
    // Reads the GL version and extensions once.  Until then nothing is assumed missing.
    void probeGlCaps();
    // False when the context has neither OpenGL 4.3 nor GL_ARB_arrays_of_arrays (macOS):
    // presetNeedsArraysOfArrays() presets cannot compile there.
    bool glHasArraysOfArrays() const { return glAoA_ != 0; }
    const std::string& glVersionText() const { return glVer_; }   // "4.1", once probed

    // ---- per frame (GL context current) -----------------------------------------------
    // True when a frame would go through the chain.
    bool active() const { return enabled && !preset.empty() && !failed_ && libraryLoaded(); }
    // Runs the chain into an internal RGBA8 texture of outW x outH and returns its
    // GL name, or 0 meaning "draw the plain picture" (disabled, library missing,
    // preset broken, GL error).  The first failure of a kind queues a notice.
    unsigned render(const ShaderFrame& f);
    // How many calls ran the chain, and how many reused the previous output because
    // nothing it depends on had changed (same emulated frame, same size).
    bool reuseOutput = true;   // false: run the chain on every call (dev: --perf-no-reuse, for before/after numbers)
    uint64_t chainRuns() const { return runs_; }
    uint64_t chainReuses() const { return reuses_; }
    // Frees GL objects; call with the context still current (shutdown).
    void destroyGL();
    // Clears a sticky failure so the next frame tries again.
    void retry() {
        failed_ = false;
        dirty_ = true;
        noticedMissing_ = false;
        if (!libraryLoaded()) probed_ = false;   // look for the library again
    }

    // One-line message for the OSD, once per event.
    bool takeNotice(std::string& out);
    const std::string& lastError() const { return lastErr_; }

    struct Impl;   // librashader types stay out of this header

private:
    std::unique_ptr<Impl> impl_;
    std::vector<std::string> dirs_;
    std::string buildDefault_ = "off";
    bool defaultApplied_ = false;   // loadSettings() used the build default (nothing stored had chosen)
    bool loadedEnabled_ = false;    // the switch and preset loadSettings() produced (usingBuildDefault compares)
    std::string loadedPreset_;
    std::string libPath_, libErr_;
    LibraryStatus libStatus_ = LibraryStatus::NotFound;
    unsigned apiVersion_ = 0;
    bool probed_ = false;
    bool failed_ = false;       // chain could not be built or a frame errored
    bool dirty_ = true;         // chain must be rebuilt
    std::string lastErr_;
    std::string notice_;
    bool noticedMissing_ = false;
    std::vector<ShaderParam> params_;
    uint64_t runs_ = 0, reuses_ = 0;
    int glAoA_ = -1;            // arrays of arrays: -1 not probed yet, 0 no, 1 yes
    std::string glVer_;
    unsigned glsl_ = 330;       // GLSL version librashader generates (the context's once probed)

    void setNotice(const std::string& s);
    // A failure's one-line message: a toast for a preset the user chose; for the build default
    // one stderr line (#1154: a missing runtime or a preset this GL cannot build must not nag
    // people who never asked for a shader; Settings > Video > Shader still says why).
    void notify(const std::string& s);
};

// ---- UI (shaders_ui.cpp) ------------------------------------------------------------------
class App;
// Inside Settings > Video.
void drawShaderSettings(App& app);
// Once per UI frame: collects file-dialog results and turns the chain's notices
// into on-screen messages.
void shaderFrameHook(App& app);

}  // namespace vj
