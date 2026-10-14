// Discord Rich Presence (#833): what the app says it is doing, and the glue that publishes it.
//
//   PresenceInput        plain description of the app state (no App, no ImGui): the unit tests drive this
//   buildActivity()      PresenceInput -> the "activity" JSON object of SET_ACTIVITY
//   chooseArtUrl()       which public box-art URL Discord may fetch for a game (only art we know exists)
//   Presence             owns the discord::Client: lazy start, change detection, "first copy only", status text
//
// What is sent (all of it optional and user-controlled in Settings > Discord):
//   details       the game's title (dat name, region/revision tags dropped) / "Browsing the library"
//   state         "Atari Jaguar" | "Jaguar CD", then Paused, Netplay, Hardcore, "12/30 achievements"
//   timestamps    start = when the game booted (elapsed time on the profile)
//   large_image   the game's box art: an https URL (libretro-thumbnails, or the project site's Jaguar CD mirror).
//                 Discord's media proxy accepts an external URL in large_image; the asset keys "jaguar" (cartridge) /
//                 "jaguarcd" (disc) are the fallback and the picture when artwork is off; "library" while browsing
//   small_image   asset key "app", small_text "Virtual Jaguar <version>"
//   buttons       "Get Virtual Jaguar" (the site's download page) and, for a RetroAchievements game, the game's page.
//                 Discord shows buttons to OTHER people only; your own profile never shows them.
// The application ID defaults to the project's Discord application (VJ_DISCORD_APP_ID, CMake; empty = feature off);
// config discord.app_id overrides it.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "discord_ipc.h"
#include "library.h"

namespace vj {
namespace discord {

extern const char* const kDownloadUrl;     // https://jaguar.provenance-emu.com/app.html
extern const char* const kAssetJaguar;     // "jaguar": cartridge fallback large image
extern const char* const kAssetJaguarCd;   // "jaguarcd": Jaguar CD fallback large image
extern const char* const kAssetLibrary;    // "library": large image while browsing the library
extern const char* const kAssetApp;        // "app": the app icon

struct PresenceInput {
    bool playing = false;          // a game (or BIOS boot) is running; false = the library / empty window
    std::string title;             // display title (see displayTitle())
    bool disc = false;             // Jaguar CD
    bool paused = false;
    long long startUnix = 0;       // Unix seconds; 0 = no timestamp
    std::string netplay;           // "" | "hosting" | "joined" | "waiting"
    bool hardcore = false;         // RetroAchievements hardcore
    unsigned achUnlocked = 0, achTotal = 0;   // achTotal 0 = no RetroAchievements set for this game
    unsigned raGameId = 0;         // 0 = none
    std::string artUrl;            // box art URL ("" = none known)
    std::string version;           // "3.8.1"
    bool showArt = true;
    bool showButtons = true;
};

// "Tempest 2000 (World) (Rev 1)" -> "Tempest 2000".  A "(Disc N)" tag is kept (multi-disc sets); "[...]" groups and
// the other "(...)" groups are dropped; never returns "" for a non-empty input.
std::string displayTitle(const std::string& datOrStem);

// The activity object.  withArtUrl=false builds the plain form (asset keys only), which the client falls back to if
// Discord refuses the first.
std::string buildActivity(const PresenceInput& in, bool withArtUrl = true);
// The state line on its own (tests, status display).
std::string stateLine(const PresenceInput& in);

// Box art Discord can show: the first libretro-thumbnails / site-mirror source whose picture is ALREADY cached on
// disk (the library downloaded it, so it exists and is not a 404), as that source's public URL.  "" when the game
// is not identified (datName empty) or no copy is cached -- the caller then uses the "jaguar" asset.
std::string chooseArtUrl(lib::Section section, const std::string& datName, const std::string& dataRoot,
                         const std::string& upstreamBase, const std::string& mirrorBase,
                         const std::function<bool(const std::string&)>& exists);

struct PresenceSettings {
    std::string appId;             // "" = the feature is unavailable
    bool enabled = true;           // "Show what I'm playing on Discord"
    int instance = 1;              // Paths::instance: only copy 1 publishes
    unsigned long pid = 0;
};

class Presence {
public:
    ~Presence() { shutdown(); }

    // Applies the settings: starts the client when the feature is wanted and possible, stops (clearing the
    // presence) when it is not.  Cheap to call every frame.
    void configure(const PresenceSettings& s);
    // Publishes `in` when it differs from what was last handed to the client.  No-op while stopped.
    void update(const PresenceInput& in);
    void shutdown();                // clears the presence and stops the worker (bounded, ~250 ms)

    bool available() const { return !s_.appId.empty() && s_.instance <= 1; }
    bool active() const { return client_ && client_->running(); }
    // One line for Settings > Discord.
    std::string statusText() const;
    unsigned updatesSent() const { return client_ ? client_->sent() : 0; }
    Client* client() { return client_.get(); }
    void setClientOptions(const Client::Options& o) { optionsOverride_ = o; haveOverride_ = true; }

private:
    PresenceSettings s_;
    std::unique_ptr<Client> client_;
    std::string lastActivity_;
    bool haveOverride_ = false;
    Client::Options optionsOverride_;
};

}  // namespace discord
}  // namespace vj
