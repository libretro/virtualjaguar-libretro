// Controller navigation (#1021): which pad button means which menu intent.
//
// A pad's menu buttons come from its FAMILY (Xbox / PlayStation / Switch / generic, from SDL_GetGamepadType): the
// Switch family swaps Confirm and Back to match its A/B, everything else uses the Xbox positions that
// tv::intentForButton has always used.  On top of that a controller MODEL (its GUID) may carry a custom layer in the
// config -- `input.padguid.<guid>.nav.family` and one `input.padguid.<guid>.nav.<intent>` per intent that differs from
// the family -- so a pad whose type and labels disagree, or a player who wants Back on the other button, is covered.
//
// Pure: no device and no window.  The desktop library and TV mode translate a pad event through resolveNavProfile()
// of the pad it came from; the controls screen edits the layer through setNavBinding() and friends.  The D-pad always
// moves and the left stick is read by NavInput, so neither can be bound to an intent.
#pragma once

#include "config.h"
#include "remap.h"
#include "tv/tv_nav.h"

struct SDL_Gamepad;

#include <array>
#include <string>
#include <vector>

namespace vj {

enum class PadFamily { Xbox, PlayStation, Switch, Generic };

PadFamily familyForType(int sdlGamepadType);   // an SDL_GamepadType; anything unrecognised is Generic
const char* familyKey(PadFamily f);            // the stored spelling: "xbox" "playstation" "switch" "generic"
bool familyFromKey(const std::string& key, PadFamily& out);   // false (out untouched) for "auto" or anything else
const char* familyName(PadFamily f);           // for the UI

// The intents a pad can rebind, in the order the editor lists them.
constexpr tv::Intent kNavIntents[10] = {
    tv::Intent::Accept,      tv::Intent::Back,     tv::Intent::Alt,      tv::Intent::Aux,      tv::Intent::Menu,
    tv::Intent::SectionPrev, tv::Intent::SectionNext, tv::Intent::PagePrev, tv::Intent::PageNext, tv::Intent::Search};
const char* navIntentKey(tv::Intent in);     // the config spelling ("confirm" "back" ...); "" for any other intent
const char* navIntentLabel(tv::Intent in);   // for the UI ("Confirm" "Back" "Tile menu" ...); "" for any other intent

struct NavProfile {
    PadFamily family = PadFamily::Xbox;
    bool familyOverridden = false;            // the family came from the custom layer, not from the pad's type
    std::array<Binding, 10> binds{};          // parallel to kNavIntents: a PadButton, or a PadAxis (+1) on a trigger

    // D-pad buttons are always directions; everything else goes through binds.  None when unmapped.
    tv::Intent intentForButton(int sdlButton) const;
    // A trigger axis maps only through a PadAxis binding with dir +1 on LEFT_TRIGGER / RIGHT_TRIGGER.
    tv::Intent intentForTrigger(int sdlAxis) const;
    Binding binding(tv::Intent in) const;     // the binding of one intent; kind None for a non-nav intent
};

NavProfile familyNavProfile(PadFamily f);   // the built-in map, no custom layer

// "input.padguid.<guid>.nav." -- every key of one model's layer lives under it.
std::string navKeyPrefix(const std::string& guid);

// The family (the layer's, else familyForType) and then each stored binding that parses as exactly one usable
// binding.  An empty guid, or a guid with no keys, is the family default.
NavProfile resolveNavProfile(const Config& cfg, const std::string& guid, int sdlGamepadType);

enum class NavSet { Ok, Swapped, Reserved };
// Binds `in` to `b` for the model.  One button per intent and one intent per button: if another intent holds `b`, the
// two swap.  Reserved (nothing written): a D-pad button, an axis other than a trigger pushed in (the sticks move
// and scroll), a keyboard binding, a non-nav intent, or anything in `reserved` (the pad's C_TVMENU and C_HELP bindings; the
// hold flag does not matter).  A key that ends up equal to the family default is erased, not written.
NavSet setNavBinding(Config& cfg, const std::string& guid, int sdlGamepadType, tv::Intent in, const Binding& b,
                     const std::vector<Binding>& reserved);

// "auto" (or anything that is not a family key) erases the override.  Either way the model's per-intent overrides are
// erased too: a family change means that family's defaults.
void setNavFamily(Config& cfg, const std::string& guid, const std::string& keyOrAuto);
void clearNavProfile(Config& cfg, const std::string& guid);   // erases every key under navKeyPrefix(guid)

// A button as the family prints it: face buttons (Xbox A/B/X/Y, PlayStation Cross/Circle/Square/Triangle, Switch by SDL's
// label), shoulders, triggers, START and BACK.  Anything else is Binding::display().
std::string navButtonName(PadFamily f, const Binding& b);

// A TV-menu binding as the family's pad names the button, " (hold)" when it is a long press (the first-use hint): SDL's MISC1 is the
// Xbox Share button, the DualSense microphone button and the Switch Pro Capture button; Guide is PS / Home on those two.
// Anything else is navButtonName().
std::string tvMenuButtonName(PadFamily f, const Binding& b);

// The caption for the button `p` gives `in`: a face button as SDL labels it on the open pad `g` (an 8BitDo in Switch mode says
// A/B/X/Y, a DualSense Cross/Circle/...), else navButtonName() for the family; "" when the profile leaves the intent unbound.
// `g` may be null (the pad is gone, or no handle is known).
std::string padButtonLabel(SDL_Gamepad* g, const NavProfile& p, tv::Intent in);

}  // namespace vj
