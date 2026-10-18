// The Neuro API protocol (https://github.com/VedalAI/neuro-sdk, API/SPECIFICATION.md) for a running Jaguar game.
//
// Link speaks the protocol and nothing else: it gets text messages in (onMessage) and hands text messages out
// (the `send` callback), so tests/neuro_test.cpp drives it without a socket.  The app (neuro_app.cpp) owns the
// WebSocket and the game state.
//
//   connected        startup, then actions/register for the game's actions, then the profile's "about" as
//                    context
//   action           validated by the ActionSet, answered with action/result AT ONCE (before the input has played
//                    out, as the spec asks), the input queued
//   re-register      actions/reregister_all (a proposal Randy already sends) registers everything again
//   forcing          a real-time game never pauses for Neuro, so with auto-force on the link asks for the next move
//                    (actions/force with the game state) whenever the last one has played out: one force at a
//                    time, never while the game is paused or nothing is loaded, and not more often than
//                    forceInterval
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "neuro_actions.h"

namespace vj {
namespace neuro {

class Link {
public:
    explicit Link(ActionSet& actions) : actions_(actions) {}

    std::function<void(const std::string&)> send;   // one text frame to the server
    std::function<void(const std::string&)> log;    // a line for the app log (optional)

    // ---- settings ----
    bool autoForce = true;
    double forceInterval = 1.0;    // seconds between the end of one move and the next force
    double forceTimeout = 60.0;    // a force with no answer after this long is given up on
    std::string query = "The game is running in real time. Choose your next move.";

    // ---- the game ----
    void setGameName(const std::string& n) { game_ = n; }
    const std::string& gameName() const { return game_; }
    // The action set changed (a profile was attached or dropped): unregisters what is gone, registers what is
    // new.  Call after ActionSet::setProfile().
    void actionsChanged();

    // ---- the connection ----
    void onConnected();
    void onDisconnected();
    void onMessage(const std::string& text);
    bool connected() const { return connected_; }

    void context(const std::string& message, bool silent);
    // Once per UI frame.  `canForce`: a game is running and unpaused, and its input queue is idle.
    void tick(double now, bool canForce, const std::string& state);

    // ---- status (Settings > Neuro, the log) ----
    bool forcePending() const { return forcePending_; }
    unsigned actionsReceived() const { return actionsReceived_; }
    unsigned forcesSent() const { return forcesSent_; }
    const std::string& lastAction() const { return lastAction_; }

    // The messages, for tests.
    std::string startupMessage() const;
    std::string registerMessage(const std::vector<std::string>& names) const;
    std::string unregisterMessage(const std::vector<std::string>& names) const;
    std::string resultMessage(const std::string& id, bool success, const std::string& message) const;
    std::string forceMessage(const std::string& state) const;

private:
    ActionSet& actions_;
    std::string game_ = "Atari Jaguar";
    bool connected_ = false;
    std::vector<std::string> registered_;
    bool forcePending_ = false;
    double forceAt_ = 0;
    double idleSince_ = -1;
    unsigned actionsReceived_ = 0, forcesSent_ = 0;
    std::string lastAction_;

    void out(const std::string& command, Json data);
    void handleAction(const Json& data);
    void say(const std::string& line) const {
        if (log) log(line);
    }
};

}  // namespace neuro
}  // namespace vj
