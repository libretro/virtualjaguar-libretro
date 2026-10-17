// The game profiles compiled into the app (neuro_actions.h).  A file of the same game_name in <data>/neuro/
// replaces one of these; docs/neuro.md describes the format.
//
// Tempest 2000: the controls are the Jaguar manual's (left / right turn the claw around the rim, B fires,
// C is the Superzapper, A the jump once the Jump power-up is collected).  No RAM watches yet: the score and
// lives addresses have not been mapped (Tools > Cheat scanner finds them; add them to a "watch" list).
#include "neuro_actions.h"

namespace vj {
namespace neuro {

namespace {

const char* const kTempest2000 = R"JSON({
  "game_name": "Tempest 2000",
  "titles": ["Tempest 2000"],
  "about": "Tempest 2000 on the Atari Jaguar. You steer a claw-shaped ship around the near rim of a 3D tube or web. Enemies (flippers, tankers, spikers, fuseballs, pulsars...) climb up the lanes toward you; shoot them before they reach the rim. Left and right move the claw one way or the other around the rim, B fires down the lane you are on, C fires the Superzapper (kills everything on screen, once per level; a second use kills one enemy), A jumps off the rim once you have collected the Jump power-up. Collect the green power-ups that float up for a particle laser, jump, and an A.I. droid. When a level is cleared you fly down the tube to the next one: steer to avoid the spikes. Option and pause bring up the game's menus; at the title screen B starts and left/right pick the game mode.",
  "actions": [
    {"name": "fire", "description": "Fire down the current lane (B). times = number of shots.",
     "steps": [{"buttons": ["b"], "frames": 3}], "gap": 3, "max_times": 30},
    {"name": "move_left", "description": "Move the claw to the left around the rim. times = how far (about one lane each).",
     "steps": [{"buttons": ["left"], "frames": 4}], "max_times": 16},
    {"name": "move_right", "description": "Move the claw to the right around the rim. times = how far (about one lane each).",
     "steps": [{"buttons": ["right"], "frames": 4}], "max_times": 16},
    {"name": "fire_moving_left", "description": "Fire while moving left around the rim. times = how far.",
     "steps": [{"buttons": ["left", "b"], "frames": 3}, {"buttons": ["left"], "frames": 2}], "max_times": 16},
    {"name": "fire_moving_right", "description": "Fire while moving right around the rim. times = how far.",
     "steps": [{"buttons": ["right", "b"], "frames": 3}, {"buttons": ["right"], "frames": 2}], "max_times": 16},
    {"name": "superzapper", "description": "Fire the Superzapper (C): destroys every enemy on screen. Once per level at full power.",
     "steps": [{"buttons": ["c"], "frames": 4}]},
    {"name": "jump", "description": "Jump off the rim (A). Only works after you collected the Jump power-up.",
     "steps": [{"buttons": ["a"], "frames": 4}]}
  ]
})JSON";

}  // namespace

const std::vector<std::string>& builtinProfileSources() {
    static const std::vector<std::string> v = {kTempest2000};
    return v;
}

}  // namespace neuro
}  // namespace vj
