// The Neuro API client of the app: the WebSocket (neuro_ws.h), the protocol (neuro_link.h), the actions and the
// input they press (neuro_actions.h), and the game profiles.  No App, no ImGui: neuro_app.cpp feeds it the app's
// state and draws Settings > Neuro.
//
// Threads: everything here is called on the UI / emulation thread (they are the same thread in this app); only
// the WebSocket worker runs elsewhere, behind WsClient's queues.
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
};

struct GameState {
    bool loaded = false;
    bool paused = false;
    std::string title;   // display title ("Tempest 2000")
    uint32_t crc = 0;    // the content's CRC-32 when the library knows it, else 0
    const uint8_t* ram = nullptr;   // main RAM (2 MB), for the profile's watches
    size_t ramSize = 0;
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
    const AgentSettings& settings() const { return s_; }
    // Once per UI frame: pumps the socket, follows the game (profile, name, context), forces the next move.
    void tick(double now, const GameState& g);
    // Once per emulated frame (from the core's input poll): what Neuro presses in it.
    ControlSet frame() { return s_.enabled ? queue_.frame() : ControlSet(); }

    std::string statusText() const;
    const Link& link() const { return link_; }
    Link& link() { return link_; }
    const Profile* profile() const { return actions_.profile(); }
    ControlSet pressedNow() const { return queue_.peek(); }
    // The state text sent with each force (also shown in Settings > Neuro).
    std::string stateText(const GameState& g) const;
    std::function<void(const std::string&)> log;

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

    void followGame(const GameState& g);
    void gameChanged(const GameState& g, bool first);
    void pump();
    std::string wantedName(const GameState& g) const;
};

}  // namespace neuro
}  // namespace vj
