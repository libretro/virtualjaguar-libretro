#include "neuro_agent.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace vj {
namespace neuro {

Agent::Agent() {
    link_.log = [this](const std::string& l) {
        if (log) log(l);
    };
    link_.send = [this](const std::string& m) { ws_.send(m); };
    loadProfiles("");
}

std::vector<std::string> Agent::loadProfiles(const std::string& dir) {
    std::vector<std::string> problems;
    std::vector<Profile> all;
    for (size_t i = 0; i < builtinProfileSources().size(); i++) {
        Profile p;
        std::string err;
        p.id = "builtin-" + std::to_string(i);
        if (parseProfile(builtinProfileSources()[i], p, err)) all.push_back(p);
        else problems.push_back("built-in profile " + std::to_string(i) + ": " + err);
    }
    std::error_code ec;
    if (!dir.empty() && std::filesystem::is_directory(dir, ec)) {
        std::vector<Profile> mine;
        for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
            if (f.path().extension() != ".json") continue;
            std::ifstream in(f.path(), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            Profile p;
            std::string err;
            p.id = f.path().stem().string();
            if (!parseProfile(ss.str(), p, err)) {
                problems.push_back(f.path().filename().string() + ": " + err);
                continue;
            }
            for (size_t i = 0; i < all.size(); i++)
                if (all[i].gameName == p.gameName) all.erase(all.begin() + (long)i--);
            mine.push_back(p);
        }
        all.insert(all.begin(), mine.begin(), mine.end());   // the user's files are tried first
    }
    actions_.setProfile(nullptr);   // the pointer would dangle; followGame() picks again
    gameKey_ = "\x01";
    profiles_ = all;
    return problems;
}

void Agent::configure(const AgentSettings& s) {
    const bool restart = s.enabled != s_.enabled || s.url != s_.url || s.gameNameOverride != s_.gameNameOverride;
    s_ = s;
    link_.autoForce = s.autoForce;
    link_.forceInterval = s.forceInterval;
    if (!restart) return;
    link_.onDisconnected();
    queue_.clear();
    gameKey_ = "\x01";   // the game name may differ now
    ws_.stop();
    startPending_ = s_.enabled;   // tick() connects, once it knows the game's name
}

std::string Agent::wantedName(const GameState& g) const {
    if (!s_.gameNameOverride.empty()) return s_.gameNameOverride;
    if (!g.loaded) return "Atari Jaguar";
    const Profile* p = actions_.profile();
    if (p) return p->gameName;
    return g.title.empty() ? "Atari Jaguar" : g.title;
}

void Agent::pump() {
    WsClient::Event ev;
    while (ws_.poll(ev)) {
        if (ev.kind == WsClient::Event::Connected) link_.onConnected();
        else if (ev.kind == WsClient::Event::Message) link_.onMessage(ev.text);
        else {
            link_.onDisconnected();
            if (log) log("neuro: disconnected: " + ev.text);
        }
    }
}

void Agent::followGame(const GameState& g) {
    const std::string key = g.loaded ? g.title + "|" + std::to_string(g.crc) : std::string();
    if (key != gameKey_) {
        gameChanged(g, gameKey_ == "\x01");
        gameKey_ = key;
        wasPaused_ = g.paused;
    }
    if (g.loaded && g.paused != wasPaused_)
        link_.context(g.paused ? "The game is paused." : "The game is running again.", true);
    wasPaused_ = g.paused;
    wasLoaded_ = g.loaded;
}

// A game was loaded or closed (`first`: the link is being set up, nothing to tell her yet).
void Agent::gameChanged(const GameState& g, bool first) {
    queue_.clear();
    actions_.setActive(g.loaded);
    actions_.setProfile(g.loaded ? findProfile(profiles_, g.title, g.crc) : nullptr);
    const std::string name = wantedName(g);
    if (name != link_.gameName() && ws_.running()) {
        // The API's game name is fixed for a session: a different game is a new connection.
        link_.setGameName(name);
        link_.onDisconnected();
        ws_.start(s_.url);
        return;
    }
    link_.setGameName(name);
    link_.actionsChanged();
    if (first) return;
    if (g.loaded) link_.context("A new game was loaded: " + g.title + ".", false);
    else if (wasLoaded_) link_.context("The game was closed.", true);
    const Profile* p = actions_.profile();
    if (p && !p->about.empty()) link_.context(p->about, true);
}

void Agent::tick(double now, const GameState& g) {
    if (!s_.enabled) return;
    pump();
    followGame(g);
    if (startPending_) {
        startPending_ = false;
        ws_.start(s_.url);
    }
    const bool canForce = g.loaded && !g.paused && !queue_.busy();
    link_.tick(now, canForce, canForce && link_.connected() ? stateText(g) : std::string());
}

std::string Agent::stateText(const GameState& g) const {
    if (!g.loaded) return "No game is running.";
    std::string s = "# " + (g.title.empty() ? std::string("Atari Jaguar game") : g.title) + "\n";
    s += std::string("- Status: ") + (g.paused ? "paused" : "running") + "\n";
    const ControlSet h = queue_.hold();
    s += "- Held buttons: " + describe(h) + "\n";
    if (const Profile* p = actions_.profile())
        for (const Watch& w : p->watches)
            s += "- " + w.name + ": " + watchValue(w, g.ram, g.ramSize) + (w.note.empty() ? "" : " " + w.note) + "\n";
    return s;
}

std::string Agent::statusText() const {
    if (!s_.enabled) return "Off";
    std::string e = ws_.lastError();
    switch (ws_.state()) {
    case WsClient::State::Open: return "Connected to " + s_.url + " as \"" + link_.gameName() + "\"";
    case WsClient::State::Connecting: return "Connecting to " + s_.url + "...";
    case WsClient::State::Waiting: return "Not connected (" + (e.empty() ? std::string("retrying") : e) + "); retrying";
    case WsClient::State::Stopped: return e.empty() ? "Stopped" : "Cannot connect: " + e;
    }
    return "";
}

}  // namespace neuro
}  // namespace vj
