// The Neuro API client of the app: the WebSocket (neuro_ws.h), the protocol (neuro_link.h), the actions and the
// input they press (neuro_actions.h), and the game profiles.  No App, no ImGui: neuro_app.cpp feeds it the app's
// state and draws Settings > Neuro.
//
// Threads: tick(), configure(), publishTo() and absorb() run on the UI thread; the buttons are played by an AgentPlayer
// (neuro_actions.h: pollAgent) on the thread that polls input (the emulation thread when the app runs one), fed through
// the InputSnapshot: the Agent publishes what was accepted as an AgentProgram and absorb() takes the progress back.  Only
// the WebSocket worker runs elsewhere, behind WsClient's queues (docs/neuro.md).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "neuro_actions.h"
#include "neuro_link.h"
#include "neuro_ws.h"

namespace vj {
namespace neuro {

struct AgentSettings {
    bool enabled = false;
    std::string url = "ws://localhost:8000";
    bool autoForce = true;
    double forceInterval = 1.0;
    std::string gameNameOverride;   // "" = the profile's game_name, else the game's title
    // RetroAchievements hardcore is on: an AI is not a human player, so Neuro stays disconnected (like cheats and
    // state loads).  `enabled` keeps the user's choice, so it connects by itself when hardcore ends.
    bool hardcore = false;

    bool operator==(const AgentSettings& o) const {
        return enabled == o.enabled && url == o.url && autoForce == o.autoForce && forceInterval == o.forceInterval &&
               gameNameOverride == o.gameNameOverride && hardcore == o.hardcore;
    }
    bool operator!=(const AgentSettings& o) const { return !(*this == o); }
};

struct GameState {
    bool loaded = false;
    bool paused = false;
    std::string title;   // display title ("Tempest 2000")
    uint32_t crc = 0;    // the content's CRC-32 when the library knows it, else 0
    // The bytes of one DebugViews request covering the profile's watches (Agent::watchRange), starting at watchBase;
    // empty = not known (no profile, not filled yet, a content switch): the state text then says "?".
    std::vector<uint8_t> watchBytes;
    uint32_t watchBase = 0;
    // Why Player 1's buttons are not reaching the game right now ("the in-game menu is open"), or null.  Points at a
    // string with static lifetime; compared by content.
    const char* inputBlock = nullptr;
};

class Agent {
public:
    Agent();
    ~Agent() { ws_.stop(); }

    // Profiles: the built-in ones, then every *.json in `dir` (a file with the same game_name replaces a
    // built-in).  Problems are returned one line each.
    std::vector<std::string> loadProfiles(const std::string& dir);
    const std::vector<Profile>& profiles() const { return profiles_; }

    void configure(const AgentSettings& s);   // cheap when nothing changed
    // Connecting is allowed: enabled, and hardcore is not on.
    bool allowed() const { return s_.enabled && !s_.hardcore; }
    bool blockedByHardcore() const { return s_.enabled && s_.hardcore; }
    // Connected, with a game running: the AI is (or may at any moment be) pressing Player 1's buttons.
    bool driving() const { return allowed() && link_.connected() && actions_.active(); }
    const AgentSettings& settings() const { return s_; }
    // Once per UI frame: pumps the socket, follows the game (profile, name, context), forces the next move.
    void tick(double now, const GameState& g);
    // Once per UI iteration, after tick(): hands the accepted input to the emulation side as one AgentProgram (a no-op
    // while nothing changed).  Disabled while Neuro is not allowed or the app is not delivering Player 1's buttons
    // (GameState::inputBlock): the replica then presses nothing and does not advance behind the menu's back.  Called
    // even when tick() returned early, so a clear made while !allowed() (hardcore, Neuro off) still reaches the core.
    void publishTo(InputSnapshot& s);
    // Once per UI iteration, before tick(): takes the replica's progress back.  Trims the queue by what was played, so
    // busy() means "accepted and not yet seen played out" (a force never goes out the instant an action is accepted),
    // and records what is being pressed for pressedNow().  Progress from an older lineage says nothing and is ignored.
    void absorb(const AgentProgress& p);
    uint64_t framesPlayed() const { return playedTotal_; }   // frames of input the replica played, over all lineages

    std::string statusText() const;
    const Link& link() const { return link_; }
    Link& link() { return link_; }
    const Profile* profile() const { return actions_.profile(); }
    ControlSet pressedNow() const { return allowed() ? pressed_ : ControlSet(); }   // what the last emulated frame pressed
    // The range of main RAM the current profile's watches cover (base = the lowest address, len = bytes up to the end of
    // the highest); false without a profile or without watches.  The app asks the emulation thread for these bytes
    // (neuro_watch.h) and hands them back in GameState::watchBytes.
    bool watchRange(uint32_t& base, unsigned& len) const;
    // The state text sent with each force (also shown in Settings > Neuro).
    std::string stateText(const GameState& g) const;
    std::function<void(const std::string&)> log;
    std::function<void(const std::string&)> notify;   // a line for the player (the app shows a toast): connected, dropped, blocked

private:
    AgentSettings s_;
    WsClient ws_;
    InputQueue queue_;
    ActionSet actions_{queue_};
    Link link_{actions_};
    std::vector<Profile> profiles_;
    std::string gameKey_;    // title|crc of the game the link was set up for
    bool wasLoaded_ = false, wasPaused_ = false;
    bool startPending_ = false;
    // publishTo / absorb
    uint64_t programGen_ = 0;                          // monotonic: takeAgent delivers a gen it has not taken
    uint64_t pubVersion_ = 0, pubLineage_ = 0;         // what was last published
    bool pubEnabled_ = false;
    uint64_t playedTotal_ = 0, lastLineage_ = ~0ull, lastPlayed_ = 0;
    ControlSet pressed_;

    void setInputBlock(const char* why);
    void tellBlock();
    void followGame(const GameState& g);
    void gameChanged(const GameState& g, bool first);
    void pump();
    std::string wantedName(const GameState& g) const;
};

}  // namespace neuro
}  // namespace vj
