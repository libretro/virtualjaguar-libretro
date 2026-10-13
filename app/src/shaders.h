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

    // ---- per frame (GL context current) -----------------------------------------------
    // True when a frame would go through the chain.
    bool active() const { return enabled && !preset.empty() && !failed_ && libraryLoaded(); }
    // Runs the chain into an internal RGBA8 texture of outW x outH and returns its
    // GL name, or 0 meaning "draw the plain picture" (disabled, library missing,
    // preset broken, GL error).  The first failure of a kind queues a notice.
    unsigned render(const ShaderFrame& f);
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
    std::string libPath_, libErr_;
    unsigned apiVersion_ = 0;
    bool probed_ = false;
    bool failed_ = false;       // chain could not be built or a frame errored
    bool dirty_ = true;         // chain must be rebuilt
    std::string lastErr_;
    std::string notice_;
    bool noticedMissing_ = false;
    std::vector<ShaderParam> params_;

    void setNotice(const std::string& s);
};

// ---- UI (shaders_ui.cpp) ------------------------------------------------------------------
class App;
// Inside Settings > Video.
void drawShaderSettings(App& app);
// Once per UI frame: collects file-dialog results and turns the chain's notices
// into on-screen messages.
void shaderFrameHook(App& app);

}  // namespace vj
