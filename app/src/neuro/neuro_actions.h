// What Neuro can do in a Jaguar game, and how it becomes controller input (the Neuro API client, neuro_link.h).
//
// The Neuro API is built around actions: the game registers named actions with a JSON schema, the model picks
// one and sends its parameters, the game validates them and answers with a result.  A Jaguar game knows nothing
// about Neuro, so the actions here are the controller itself:
//
//   press_buttons    hold some buttons together for N frames, then let go          (a tap, a short move)
//   button_sequence  a list of such steps, run back to back                        (menus, combos, codes)
//   hold_buttons     keep buttons held until told otherwise                        ("keep firing and turn left")
//   release_all      let go of everything, and drop what is still queued
//
// plus the actions of a game PROFILE (built in, or a .json file in <data>/neuro/), which names the moves of one
// game ("fire", "superzapper") as button macros, describes its controls to the model, and can read RAM values
// (score, lives) into the state text Neuro is shown.  See docs/neuro.md.
//
// Everything here is pure: no sockets, no SDL, no core.  InputQueue turns accepted actions into one ControlSet per
// emulated frame; the app ORs it into Player 1's controls (InputManager::addAgentControls).
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "input_snapshot.h"
#include "neuro_json.h"
#include "remap.h"

namespace vj {
namespace neuro {

// ---- buttons ----
// "up" "down" "left" "right" "a" "b" "c" "option" "pause" "0".."9" "star" "hash" (also "*" "#").
bool controlByName(const std::string& name, Ctl& out);
std::string controlName(Ctl c);
std::vector<std::string> buttonNames();   // the schema enum, in pad order
std::string describe(ControlSet s);       // "left+b", "nothing"

constexpr unsigned kFps = 60;            // NTSC; PAL games run at 50, and the descriptions say "about"
constexpr unsigned kMaxStepFrames = 600;      // one step: 10 s
constexpr unsigned kMaxQueuedFrames = 1800;   // everything queued: 30 s
constexpr unsigned kMaxSteps = 64;
// button_sequence: whatever its schema allows fits in an empty queue (20 x 90 = kMaxQueuedFrames).
constexpr unsigned kMaxSequenceSteps = 20;
constexpr unsigned kMaxSequenceStepFrames = 90;
static_assert(kMaxSequenceSteps * kMaxSequenceStepFrames <= kMaxQueuedFrames, "a valid sequence must fit");

struct Step {
    ControlSet held;
    unsigned frames = 1;
};

// The per-frame scheduler.  Steps run in order, one frame at a time; the hold set is added to every frame.
class InputQueue {
public:
    // false (and nothing queued) when the steps would push the queue past kMaxQueuedFrames.
    bool enqueue(const std::vector<Step>& steps);
    void setHold(ControlSet s) {
        if (s != hold_) {
            hold_ = s;
            version_++;
        }
    }
    ControlSet hold() const { return hold_; }
    void clear();                      // drops the queue and the hold; starts a new lineage
    ControlSet frame();                // advances one emulated frame; returns what is pressed in it
    ControlSet peek() const;           // what the next frame() would press, without advancing
    bool busy() const { return !steps_.empty(); }
    unsigned queuedFrames() const;

    // The agent channel (input_snapshot.h): the queue as a program the emulation thread replays.
    uint64_t version() const { return version_; }   // bumped by every successful enqueue, every setHold that changes
                                                    // the hold, every clear
    uint64_t lineage() const { return lineage_; }
    uint64_t firstFrame() const { return firstFrame_; }
    void trim(unsigned frames);        // drops the first `frames` frames of the steps (clamped); raises firstFrame
    AgentProgram program(uint64_t gen, bool enabled) const;   // steps + hold + lineage + firstFrame as a program

private:
    std::deque<Step> steps_;
    ControlSet hold_;
    uint64_t version_ = 0, lineage_ = 0, firstFrame_ = 0;
};

// The emulation-thread replica of InputQueue (pure; no sockets, no core).
class AgentPlayer {
public:
    void load(const AgentProgram& p);     // a new lineage restarts played at 0; the same lineage keeps it and skips
                                          // played - firstFrame frames into the new steps (clamped up to firstFrame)
    ControlSet frame();                   // one emulated frame: hold | current step; advances.  !enabled: empty, no advance
    AgentProgress progress() const;
    uint64_t gen() const { return gen_; }

private:
    InputQueue q_;
    bool enabled_ = false;
    uint64_t gen_ = 0, lineage_ = 0, played_ = 0;
    ControlSet last_;
};
// The one function the app's input poll and the tests call: takeAgent + load on a new gen, frame(), reportAgent.
ControlSet pollAgent(InputSnapshot& s, AgentPlayer& p);

// ---- game profiles ----
struct Watch {
    std::string name;            // "score"
    uint32_t address = 0;        // main RAM, 0x000000-0x1FFFFF (big-endian, as the 68000 sees it)
    unsigned size = 1;           // 1, 2 or 4 bytes
    std::string format = "dec";  // dec | hex | bcd
    std::string note;            // shown after the value: "(of 3)"
};

struct Macro {
    std::string name;            // action name, lowercase_with_underscores
    std::string description;
    std::vector<Step> steps;     // one run
    unsigned gap = 0;            // frames of nothing between repeats
    unsigned maxTimes = 1;       // >1: the action takes {"times": 1..maxTimes} and repeats the steps
};

struct Profile {
    std::string id;                    // file stem / built-in key
    std::string gameName;              // "Tempest 2000": the API's game name (what Neuro sees)
    std::vector<std::string> titles;   // matched case-insensitively against the start of the game's title
    std::vector<uint32_t> crcs;        // ... or the content's CRC-32
    std::string about;                 // controls / goal, sent as context when the game starts
    std::vector<Watch> watches;
    std::vector<Macro> macros;
    bool generic = true;               // also offer press_buttons & co.
};

// Reads a profile document (see docs/neuro.md); false + err names the first problem.
bool parseProfile(const std::string& text, Profile& out, std::string& err);
const std::vector<std::string>& builtinProfileSources();   // the JSON of the profiles compiled in
// The first profile (in order) that matches the content; nullptr = none.
const Profile* findProfile(const std::vector<Profile>& all, const std::string& title, uint32_t crc);

// Formats a watch from RAM (`ram` = main RAM, `size` bytes); "?" when out of range.
std::string watchValue(const Watch& w, const uint8_t* ram, size_t size);

// ---- the action set of the current game ----
struct ActionResult {
    bool success = false;
    std::string message;
};

class ActionSet {
public:
    explicit ActionSet(InputQueue& q) : q_(q) {}
    void setProfile(const Profile* p);           // nullptr = generic actions only
    const Profile* profile() const { return profile_; }
    void setActive(bool a) { active_ = a; }      // false (no game running): no actions at all
    bool active() const { return active_; }
    // The game is running but the app is not delivering Player 1's buttons (the in-game menu has the pads, P1 is a
    // mouse): every action but release_all fails with `why` instead of queueing input that would be thrown away,
    // so Neuro is told the truth.  "" = delivering.
    void setBlocked(const std::string& why) { blocked_ = why; }
    const std::string& blocked() const { return blocked_; }

    std::vector<std::string> names() const;
    Json definitions() const;                    // the "actions" array of actions/register
    bool has(const std::string& name) const;
    // Validates `data` (the action's JSON text, possibly empty) and, if valid, queues the input.
    ActionResult execute(const std::string& name, const std::string& data);

private:
    InputQueue& q_;
    const Profile* profile_ = nullptr;
    bool active_ = true;
    std::string blocked_;

    ActionResult pressButtons(const Json& d);
    ActionResult sequence(const Json& d);
    ActionResult holdButtons(const Json& d);
    ActionResult runMacro(const Macro& m, const Json& d);
    ActionResult queue(const std::vector<Step>& steps, const std::string& what);
};

}  // namespace neuro
}  // namespace vj
