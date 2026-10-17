#include "neuro_actions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace vj {
namespace neuro {

namespace {

struct NamedCtl {
    const char* name;
    Ctl ctl;
};

const NamedCtl kButtons[] = {
    {"up", C_UP},     {"down", C_DOWN},     {"left", C_LEFT},   {"right", C_RIGHT}, {"a", C_A},
    {"b", C_B},       {"c", C_C},           {"option", C_OPTION}, {"pause", C_PAUSE}, {"0", C_K0},
    {"1", C_K1},      {"2", C_K2},          {"3", C_K3},        {"4", C_K4},        {"5", C_K5},
    {"6", C_K6},      {"7", C_K7},          {"8", C_K8},        {"9", C_K9},        {"star", C_STAR},
    {"hash", C_HASH},
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// A JSON number that is a whole number in [lo, hi].
bool wholeIn(const Json& j, double lo, double hi, unsigned& out) {
    if (!j.isNumber()) return false;
    const double d = j.asNumber();
    if (d != std::floor(d) || d < lo || d > hi) return false;
    out = (unsigned)d;
    return true;
}

bool parseButtons(const Json& arr, ControlSet& out, std::string& err) {
    out = ControlSet();
    if (!arr.isArray()) return (err = "\"buttons\" must be an array of button names", false);
    for (const Json& b : arr.items()) {
        Ctl c;
        if (!b.isString() || !controlByName(b.asString(), c))
            return (err = "unknown button " + (b.isString() ? "\"" + b.asString() + "\"" : b.dump()) +
                          "; the buttons are: up, down, left, right, a, b, c, option, pause, 0-9, star, hash",
                    false);
        out.set(c);
    }
    return true;
}

// {"buttons": [...], "frames": n} -> a step; `defFrames` when frames is absent.
bool parseStep(const Json& o, unsigned defFrames, Step& out, std::string& err, unsigned maxFrames = kMaxStepFrames) {
    if (!o.isObject()) return (err = "each step must be an object with \"buttons\" and \"frames\"", false);
    if (!parseButtons(o["buttons"].isNull() ? Json::array() : o["buttons"], out.held, err)) return false;
    out.frames = defFrames;
    if (o.has("frames") && !wholeIn(o["frames"], 1, maxFrames, out.frames))
        return (err = "\"frames\" must be a whole number from 1 to " + std::to_string(maxFrames), false);
    return true;
}

Json buttonsSchema() {
    Json items = Json::object();
    items.set("type", Json::string("string"));
    Json en = Json::array();
    for (const std::string& n : buttonNames()) en.push(Json::string(n));
    items.set("enum", en);
    Json s = Json::object();
    s.set("type", Json::string("array"));
    s.set("items", items);
    s.set("maxItems", Json::number(8));
    return s;
}

Json intSchema(unsigned lo, unsigned hi) {
    Json s = Json::object();
    s.set("type", Json::string("integer"));
    s.set("minimum", Json::number(lo));
    s.set("maximum", Json::number(hi));
    return s;
}

Json objectSchema(Json props, std::vector<std::string> required) {
    Json s = Json::object();
    s.set("type", Json::string("object"));
    s.set("properties", std::move(props));
    Json req = Json::array();
    for (const std::string& r : required) req.push(Json::string(r));
    s.set("required", req);
    return s;
}

Json action(const std::string& name, const std::string& desc, Json schema) {
    Json a = Json::object();
    a.set("name", Json::string(name));
    a.set("description", Json::string(desc));
    if (!schema.isNull()) a.set("schema", std::move(schema));
    return a;
}

Json stepSchema() {
    Json p = Json::object();
    p.set("buttons", buttonsSchema());
    p.set("frames", intSchema(1, kMaxSequenceStepFrames));
    return objectSchema(p, {"buttons", "frames"});
}

const char* const kPressDesc =
    "Press Jaguar controller buttons together for a number of frames (60 frames is about one second), then "
    "release them. Buttons: up, down, left, right, a, b, c, option, pause, the keypad 0-9, star and hash.";
const char* const kSequenceDesc =
    "Run a sequence of button presses back to back, for menus, combos and codes. Each step holds its buttons "
    "for its frames (up to 90, 1.5 seconds); a step with no buttons just waits.";
const char* const kHoldDesc =
    "Keep these buttons held down until you call hold_buttons again or release_all (an empty list releases "
    "them). Presses from other actions are added on top.";
const char* const kReleaseDesc = "Release every button and cancel the presses that are still queued.";

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// buttons
// ---------------------------------------------------------------------------------------------------------------
bool controlByName(const std::string& name, Ctl& out) {
    std::string n = lower(name);
    if (n == "*") n = "star";
    if (n == "#") n = "hash";
    for (const NamedCtl& b : kButtons)
        if (n == b.name) return (out = b.ctl, true);
    return false;
}

std::string controlName(Ctl c) {
    for (const NamedCtl& b : kButtons)
        if (b.ctl == c) return b.name;
    return "?";
}

std::vector<std::string> buttonNames() {
    std::vector<std::string> v;
    for (const NamedCtl& b : kButtons) v.push_back(b.name);
    return v;
}

std::string describe(ControlSet s) {
    std::string o;
    for (const NamedCtl& b : kButtons)
        if (s.test(b.ctl)) o += (o.empty() ? "" : "+") + std::string(b.name);
    return o.empty() ? "nothing" : o;
}

// ---------------------------------------------------------------------------------------------------------------
// InputQueue
// ---------------------------------------------------------------------------------------------------------------
unsigned InputQueue::queuedFrames() const {
    unsigned n = 0;
    for (const Step& s : steps_) n += s.frames;
    return n;
}

bool InputQueue::enqueue(const std::vector<Step>& steps) {
    unsigned add = 0;
    for (const Step& s : steps) add += s.frames;
    if (steps.empty() || queuedFrames() + add > kMaxQueuedFrames || steps_.size() + steps.size() > 4 * kMaxSteps)
        return false;
    for (const Step& s : steps)
        if (s.frames) steps_.push_back(s);
    return true;
}

void InputQueue::clear() {
    steps_.clear();
    hold_ = ControlSet();
}

ControlSet InputQueue::peek() const {
    ControlSet s = hold_;
    if (!steps_.empty()) s.bits |= steps_.front().held.bits;
    return s;
}

ControlSet InputQueue::frame() {
    const ControlSet s = peek();
    if (!steps_.empty() && --steps_.front().frames == 0) steps_.pop_front();
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
// profiles
// ---------------------------------------------------------------------------------------------------------------
namespace {

bool parseHex(const Json& j, uint32_t& out) {
    if (j.isNumber()) return (out = (uint32_t)j.asNumber(), j.asNumber() >= 0);
    if (!j.isString()) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(j.asString().c_str(), &end, 0);
    if (!end || *end || j.asString().empty()) return false;
    out = (uint32_t)v;
    return true;
}

bool parseWatch(const Json& w, Watch& out, std::string& err) {
    out.name = w["name"].asString();
    if (out.name.empty() || !parseHex(w["address"], out.address) || out.address >= 0x200000)
        return (err = "a watch needs a \"name\" and an \"address\" in main RAM (0x0-0x1FFFFF)", false);
    out.size = 1;
    if (w.has("size") && (!wholeIn(w["size"], 1, 4, out.size) || out.size == 3))
        return (err = "watch \"" + out.name + "\": size is 1, 2 or 4", false);
    if (w.has("format")) out.format = lower(w["format"].asString());
    if (out.format != "dec" && out.format != "hex" && out.format != "bcd")
        return (err = "watch \"" + out.name + "\": format is dec, hex or bcd", false);
    out.note = w["note"].asString();
    return true;
}

bool validName(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    for (char c : n)
        if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}

bool parseMacro(const Json& m, Macro& out, std::string& err) {
    out.name = m["name"].asString();
    out.description = m["description"].asString();
    if (!validName(out.name)) return (err = "action names are lowercase_with_underscores", false);
    if (out.description.empty()) return (err = "action \"" + out.name + "\" needs a description", false);
    if (!m["steps"].isArray() || m["steps"].items().empty() || m["steps"].items().size() > kMaxSteps)
        return (err = "action \"" + out.name + "\" needs 1-" + std::to_string(kMaxSteps) + " steps", false);
    for (const Json& s : m["steps"].items()) {
        Step st;
        if (!parseStep(s, 1, st, err)) return (err = "action \"" + out.name + "\": " + err, false);
        out.steps.push_back(st);
    }
    if (m.has("gap") && !wholeIn(m["gap"], 0, kMaxStepFrames, out.gap))
        return (err = "action \"" + out.name + "\": bad gap", false);
    if (m.has("max_times") && !wholeIn(m["max_times"], 1, 100, out.maxTimes))
        return (err = "action \"" + out.name + "\": max_times is 1-100", false);
    return true;
}

bool parseLists(const Json& d, Profile& p, std::string& err) {
    for (const Json& t : d["titles"].items())
        if (t.isString() && !t.asString().empty()) p.titles.push_back(t.asString());
    for (const Json& c : d["crc32"].items()) {
        uint32_t v = 0;
        if (!parseHex(c, v)) return (err = "crc32 entries are numbers or \"0x...\" strings", false);
        p.crcs.push_back(v);
    }
    for (const Json& w : d["watch"].items()) {
        Watch wt;
        if (!parseWatch(w, wt, err)) return false;
        p.watches.push_back(wt);
    }
    for (const Json& m : d["actions"].items()) {
        Macro mc;
        if (!parseMacro(m, mc, err)) return false;
        p.macros.push_back(mc);
    }
    return true;
}

}  // namespace

bool parseProfile(const std::string& text, Profile& out, std::string& err) {
    Json d;
    if (!Json::parse(text, d, err)) return false;
    if (!d.isObject()) return (err = "a profile is a JSON object", false);
    Profile p;
    p.gameName = d["game_name"].asString();
    if (p.gameName.empty()) return (err = "\"game_name\" is missing", false);
    p.about = d["about"].asString();
    p.generic = !d.has("generic_actions") || d["generic_actions"].asBool();
    if (!parseLists(d, p, err)) return false;
    if (p.titles.empty() && p.crcs.empty()) return (err = "a profile needs \"titles\" or \"crc32\" to match", false);
    p.id = out.id;
    out = p;
    return true;
}

const Profile* findProfile(const std::vector<Profile>& all, const std::string& title, uint32_t crc) {
    const std::string t = lower(title);
    for (const Profile& p : all) {
        if (crc && std::find(p.crcs.begin(), p.crcs.end(), crc) != p.crcs.end()) return &p;
        for (const std::string& want : p.titles)
            if (t.compare(0, want.size(), lower(want)) == 0) return &p;
    }
    return nullptr;
}

std::string watchValue(const Watch& w, const uint8_t* ram, size_t size) {
    if (!ram || (size_t)w.address + w.size > size) return "?";
    uint32_t v = 0;
    for (unsigned i = 0; i < w.size; i++) v = (v << 8) | ram[w.address + i];
    char buf[32];
    if (w.format == "hex") {
        std::snprintf(buf, sizeof buf, "0x%0*X", (int)(w.size * 2), (unsigned)v);
    } else if (w.format == "bcd") {
        std::snprintf(buf, sizeof buf, "%0*X", (int)(w.size * 2), (unsigned)v);
        std::string s = buf;
        if (s.find_first_not_of("0123456789") != std::string::npos) return "?";
        s.erase(0, std::min(s.find_first_not_of('0'), s.size() - 1));
        return s;
    } else {
        std::snprintf(buf, sizeof buf, "%u", (unsigned)v);
    }
    return buf;
}

// ---------------------------------------------------------------------------------------------------------------
// ActionSet
// ---------------------------------------------------------------------------------------------------------------
void ActionSet::setProfile(const Profile* p) { profile_ = p; }

std::vector<std::string> ActionSet::names() const {
    std::vector<std::string> v;
    if (!active_) return v;
    if (!profile_ || profile_->generic) v = {"press_buttons", "button_sequence", "hold_buttons", "release_all"};
    if (profile_)
        for (const Macro& m : profile_->macros) v.push_back(m.name);
    return v;
}

bool ActionSet::has(const std::string& name) const {
    const std::vector<std::string> v = names();
    return std::find(v.begin(), v.end(), name) != v.end();
}

Json ActionSet::definitions() const {
    Json arr = Json::array();
    if (!active_) return arr;
    if (!profile_ || profile_->generic) {
        Json press = Json::object();
        press.set("buttons", buttonsSchema());
        press.set("frames", intSchema(1, kMaxStepFrames));
        arr.push(action("press_buttons", kPressDesc, objectSchema(press, {"buttons", "frames"})));
        Json steps = Json::object();
        steps.set("type", Json::string("array"));
        steps.set("items", stepSchema());
        steps.set("minItems", Json::number(1));
        steps.set("maxItems", Json::number(kMaxSequenceSteps));
        Json seq = Json::object();
        seq.set("steps", steps);
        arr.push(action("button_sequence", kSequenceDesc, objectSchema(seq, {"steps"})));
        Json hold = Json::object();
        hold.set("buttons", buttonsSchema());
        arr.push(action("hold_buttons", kHoldDesc, objectSchema(hold, {"buttons"})));
        arr.push(action("release_all", kReleaseDesc, Json()));
    }
    if (profile_)
        for (const Macro& m : profile_->macros) {
            Json schema;
            if (m.maxTimes > 1) {
                Json p = Json::object();
                p.set("times", intSchema(1, m.maxTimes));
                schema = objectSchema(p, {"times"});
            }
            arr.push(action(m.name, m.description, schema));
        }
    return arr;
}

ActionResult ActionSet::execute(const std::string& name, const std::string& data) {
    if (!active_) return {true, "No game is running right now, so nothing was pressed."};
    if (!has(name)) return {false, "Unknown action \"" + name + "\"."};
    Json d = Json::object();
    if (!data.empty()) {
        std::string err;
        if (!Json::parse(data, d, err)) return {false, "The action data is not valid JSON (" + err + ")."};
        if (!d.isObject()) return {false, "The action data must be a JSON object."};
    }
    if (name == "press_buttons") return pressButtons(d);
    if (name == "button_sequence") return sequence(d);
    if (name == "hold_buttons") return holdButtons(d);
    if (name == "release_all") {
        q_.clear();
        return {true, "Released every button."};
    }
    for (const Macro& m : profile_->macros)
        if (m.name == name) return runMacro(m, d);
    return {false, "Unknown action \"" + name + "\"."};
}

ActionResult ActionSet::queue(const std::vector<Step>& steps, const std::string& what) {
    unsigned frames = 0;
    for (const Step& st : steps) frames += st.frames;
    if (frames > kMaxQueuedFrames)
        return {false, "That is " + std::to_string(frames) + " frames of input; at most " +
                           std::to_string(kMaxQueuedFrames) + " (30 seconds) are allowed. Use fewer or shorter steps."};
    if (!q_.enqueue(steps))
        return {false, "Too much input is already queued (" + std::to_string(q_.queuedFrames()) +
                           " frames). Wait for it to finish or call release_all."};
    return {true, what};
}

ActionResult ActionSet::pressButtons(const Json& d) {
    Step s;
    std::string err;
    if (!d.has("buttons")) return {false, "\"buttons\" is required."};
    if (!parseStep(d, 6, s, err)) return {false, err};
    if (!s.held.any()) return {false, "Name at least one button to press."};
    return queue({s}, "Pressing " + describe(s.held) + " for " + std::to_string(s.frames) + " frames.");
}

ActionResult ActionSet::sequence(const Json& d) {
    const Json& arr = d["steps"];
    if (!arr.isArray() || arr.items().empty()) return {false, "\"steps\" must be a non-empty array."};
    if (arr.items().size() > kMaxSequenceSteps)
        return {false, "At most " + std::to_string(kMaxSequenceSteps) + " steps."};
    std::vector<Step> steps;
    unsigned frames = 0;
    for (const Json& j : arr.items()) {
        Step s;
        std::string err;
        if (!parseStep(j, 6, s, err, kMaxSequenceStepFrames)) return {false, "Step " + std::to_string(steps.size() + 1) + ": " + err};
        steps.push_back(s);
        frames += s.frames;
    }
    return queue(steps, "Running " + std::to_string(steps.size()) + " steps (" + std::to_string(frames) + " frames).");
}

ActionResult ActionSet::holdButtons(const Json& d) {
    ControlSet s;
    std::string err;
    if (!parseButtons(d["buttons"], s, err)) return {false, err};
    q_.setHold(s);
    return {true, s.any() ? "Holding " + describe(s) + "." : "Released the held buttons."};
}

ActionResult ActionSet::runMacro(const Macro& m, const Json& d) {
    unsigned times = 1;
    if (m.maxTimes > 1 && d.has("times") && !wholeIn(d["times"], 1, m.maxTimes, times))
        return {false, "\"times\" must be a whole number from 1 to " + std::to_string(m.maxTimes) + "."};
    std::vector<Step> steps;
    for (unsigned i = 0; i < times; i++) {
        steps.insert(steps.end(), m.steps.begin(), m.steps.end());
        if (m.gap && i + 1 < times) steps.push_back(Step{ControlSet(), m.gap});
    }
    return queue(steps, times > 1 ? m.name + " x" + std::to_string(times) + "." : m.name + ".");
}

}  // namespace neuro
}  // namespace vj
