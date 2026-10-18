#include "neuro_link.h"

#include <algorithm>

namespace vj {
namespace neuro {

namespace {

std::string message(const std::string& command, const std::string& game, const Json& data) {
    Json m = Json::object();
    m.set("command", Json::string(command));
    m.set("game", Json::string(game));
    if (!data.isNull()) m.set("data", data);
    return m.dump();
}

Json stringArray(const std::vector<std::string>& v) {
    Json a = Json::array();
    for (const std::string& s : v) a.push(Json::string(s));
    return a;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// messages
// ---------------------------------------------------------------------------------------------------------------
std::string Link::startupMessage() const { return message("startup", game_, Json()); }

std::string Link::registerMessage(const std::vector<std::string>& names) const {
    Json list = Json::array();
    const Json defs = actions_.definitions();
    for (const Json& a : defs.items())
        if (contains(names, a["name"].asString())) list.push(a);
    Json d = Json::object();
    d.set("actions", list);
    return message("actions/register", game_, d);
}

std::string Link::unregisterMessage(const std::vector<std::string>& names) const {
    Json d = Json::object();
    d.set("action_names", stringArray(names));
    return message("actions/unregister", game_, d);
}

std::string Link::resultMessage(const std::string& id, bool success, const std::string& msg) const {
    Json d = Json::object();
    d.set("id", Json::string(id));
    d.set("success", Json::boolean(success));
    if (!msg.empty()) d.set("message", Json::string(msg));
    return message("action/result", game_, d);
}

std::string Link::forceMessage(const std::string& state) const {
    Json d = Json::object();
    if (!state.empty()) d.set("state", Json::string(state));
    d.set("query", Json::string(query));
    d.set("ephemeral_context", Json::boolean(true));   // a stream of game states would flood her memory
    d.set("priority", Json::string("low"));
    d.set("action_names", stringArray(actions_.names()));
    return message("actions/force", game_, d);
}

void Link::out(const std::string& command, Json data) {
    if (connected_ && send) send(message(command, game_, data));
}

// ---------------------------------------------------------------------------------------------------------------
// connection
// ---------------------------------------------------------------------------------------------------------------
void Link::onConnected() {
    connected_ = true;
    forcePending_ = false;
    idleSince_ = -1;
    if (send) send(startupMessage());   // also clears whatever an earlier session registered
    registered_ = actions_.names();
    if (send && !registered_.empty()) send(registerMessage(registered_));
    const Profile* p = actions_.profile();
    if (p && !p->about.empty()) context(p->about, true);
    say("neuro: connected as \"" + game_ + "\", " + std::to_string(registered_.size()) + " actions");
}

void Link::onDisconnected() {
    connected_ = false;
    forcePending_ = false;
    registered_.clear();
}

void Link::actionsChanged() {
    const std::vector<std::string> now = actions_.names();
    std::vector<std::string> gone, added;
    for (const std::string& n : registered_)
        if (!contains(now, n)) gone.push_back(n);
    for (const std::string& n : now)
        if (!contains(registered_, n)) added.push_back(n);
    registered_ = now;
    if (!connected_ || !send) return;
    if (!gone.empty()) send(unregisterMessage(gone));
    if (!added.empty()) send(registerMessage(added));
}

void Link::context(const std::string& msg, bool silent) {
    Json d = Json::object();
    d.set("message", Json::string(msg));
    d.set("silent", Json::boolean(silent));
    out("context", d);
}

// ---------------------------------------------------------------------------------------------------------------
// incoming
// ---------------------------------------------------------------------------------------------------------------
void Link::onMessage(const std::string& text) {
    Json m;
    std::string err;
    if (!Json::parse(text, m, err) || !m.isObject()) {
        say("neuro: ignored a message that is not a JSON object (" + err + ")");
        return;
    }
    const std::string cmd = m["command"].asString();
    if (cmd == "action") {
        handleAction(m["data"]);
    } else if (cmd == "actions/reregister_all") {
        registered_ = actions_.names();
        if (send && !registered_.empty()) send(registerMessage(registered_));
    } else {
        say("neuro: ignored command \"" + cmd + "\"");
    }
}

void Link::handleAction(const Json& data) {
    const std::string id = data["id"].isString() ? data["id"].asString() : data["id"].dump();
    const std::string name = data["name"].asString();
    // "data" is a JSON string by the spec; tolerate a server that sends the object itself.
    const Json& raw = data["data"];
    const std::string params = raw.isString() ? raw.asString() : raw.isNull() ? std::string() : raw.dump();
    actionsReceived_++;
    const ActionResult r = actions_.execute(name, params);
    lastAction_ = name + (params.empty() ? "" : " " + params) + (r.success ? "" : "  -> refused: " + r.message);
    say("neuro: action " + lastAction_);
    if (send && connected_) send(resultMessage(id, r.success, r.message));
    // Any answer ends the force: a failed one is retried by Neuro herself (spec), a good one starts the move.
    if (r.success) forcePending_ = false;
    idleSince_ = -1;
}

void Link::tick(double now, bool canForce, const std::string& state) {
    if (forcePending_ && now - forceAt_ > forceTimeout) forcePending_ = false;
    if (!connected_ || !autoForce || forcePending_ || !canForce || actions_.names().empty()) {
        if (!canForce) idleSince_ = -1;
        return;
    }
    if (idleSince_ < 0) idleSince_ = now;
    if (now - idleSince_ < forceInterval) return;
    if (send) send(forceMessage(state));
    forcePending_ = true;
    forceAt_ = now;
    forcesSent_++;
    idleSince_ = -1;
}

}  // namespace neuro
}  // namespace vj
