// Controller navigation (#1021): the pad-family menu-button profiles and the per-controller-model custom layer.
//
//   vj_pad_profile_test
//
// Pure logic: no window, no device, no core.  The custom layer lives in a Config (a temp file for the round trip).
#include "pad_profile.h"

#include <cstdio>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#include <filesystem>
#include <string>
#include <vector>

using namespace vj;
using tv::Intent;

static int g_fail = 0, g_checks = 0;

#define CHECK(c)                                                              \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(c)) {                                                           \
            g_fail++;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

static const std::string GUID = "030000005e040000ff02000000007200";
static const std::string OTHER = "03000000deadbeef00000000000000aa";
static const int XBOX = SDL_GAMEPAD_TYPE_XBOXONE;
static const int SWITCH_PRO = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;

static const int SOUTH = SDL_GAMEPAD_BUTTON_SOUTH, EAST = SDL_GAMEPAD_BUTTON_EAST, WEST = SDL_GAMEPAD_BUTTON_WEST,
                 NORTH = SDL_GAMEPAD_BUTTON_NORTH;

static const std::vector<Binding> kNoReserved;

static size_t keyCount(const Config& c, const std::string& guid) { return c.withPrefix(navKeyPrefix(guid)).size(); }

static void testFamilyForEveryType() {
    // Every SDL_GamepadType (UNKNOWN .. COUNT-1) and one out of range.
    CHECK(familyForType(SDL_GAMEPAD_TYPE_UNKNOWN) == PadFamily::Generic);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_STANDARD) == PadFamily::Generic);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_XBOX360) == PadFamily::Xbox);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_XBOXONE) == PadFamily::Xbox);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_PS3) == PadFamily::PlayStation);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_PS4) == PadFamily::PlayStation);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_PS5) == PadFamily::PlayStation);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO) == PadFamily::Switch);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT) == PadFamily::Switch);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT) == PadFamily::Switch);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR) == PadFamily::Switch);
    CHECK(familyForType(SDL_GAMEPAD_TYPE_GAMECUBE) == PadFamily::Switch);
    CHECK(SDL_GAMEPAD_TYPE_GAMECUBE == SDL_GAMEPAD_TYPE_COUNT - 1);   // the list above is every type this SDL has
    CHECK(familyForType(SDL_GAMEPAD_TYPE_COUNT) == PadFamily::Generic);
    CHECK(familyForType(-1) == PadFamily::Generic);

    for (PadFamily f : {PadFamily::Xbox, PadFamily::PlayStation, PadFamily::Switch, PadFamily::Generic}) {
        PadFamily back = PadFamily::Xbox;
        CHECK(familyFromKey(familyKey(f), back) && back == f);
        CHECK(std::string(familyName(f)).size() > 0);
    }
    PadFamily x = PadFamily::Switch;
    CHECK(!familyFromKey("auto", x) && x == PadFamily::Switch);
    CHECK(std::string(familyName(PadFamily::Switch)) == "Nintendo Switch");
    CHECK(std::string(familyName(PadFamily::Generic)) == "MFi / generic");
}

static void testIntentNames() {
    const char* keys[10] = {"confirm", "back", "menu", "favorite", "start", "tabprev", "tabnext", "pageprev", "pagenext", "search"};
    const char* labels[10] = {"Confirm", "Back", "Tile menu", "Favorite", "Start", "Previous tab", "Next tab", "Previous letter", "Next letter", "Search"};
    for (int i = 0; i < 10; i++) {
        CHECK(std::string(navIntentKey(kNavIntents[i])) == keys[i]);
        CHECK(std::string(navIntentLabel(kNavIntents[i])) == labels[i]);
    }
    CHECK(std::string(navIntentKey(Intent::Up)).empty());
    CHECK(std::string(navIntentLabel(Intent::AcceptLong)).empty());
}

static void testXboxMatchesIntentForButton() {
    const NavProfile p = familyNavProfile(PadFamily::Xbox);
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        Intent want = tv::intentForButton(b);
        if (b == SDL_GAMEPAD_BUTTON_BACK) want = Intent::Search;   // the one stated exception: the TV table leaves BACK unmapped
        CHECK(p.intentForButton(b) == want);
    }
    // The generic and PlayStation families map the same buttons.
    for (PadFamily f : {PadFamily::PlayStation, PadFamily::Generic}) {
        const NavProfile q = familyNavProfile(f);
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) CHECK(q.intentForButton(b) == p.intentForButton(b));
    }
}

static void testSwitchSwapsConfirmBack() {
    const NavProfile p = familyNavProfile(PadFamily::Switch);
    CHECK(p.intentForButton(EAST) == Intent::Accept);
    CHECK(p.intentForButton(SOUTH) == Intent::Back);
    CHECK(p.intentForButton(WEST) == Intent::Alt);
    CHECK(p.intentForButton(NORTH) == Intent::Aux);
    CHECK(p.intentForButton(SDL_GAMEPAD_BUTTON_START) == Intent::Menu);
    CHECK(p.intentForButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) == Intent::SectionPrev);
    CHECK(p.intentForButton(SDL_GAMEPAD_BUTTON_BACK) == Intent::Search);
}

static void testTriggersPage() {
    const NavProfile p = familyNavProfile(PadFamily::Xbox);
    CHECK(p.intentForTrigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == Intent::PagePrev);
    CHECK(p.intentForTrigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == Intent::PageNext);
    CHECK(p.intentForTrigger(SDL_GAMEPAD_AXIS_LEFTX) == Intent::None);
    CHECK(p.intentForTrigger(SDL_GAMEPAD_AXIS_RIGHTY) == Intent::None);
    CHECK(p.intentForButton(SDL_GAMEPAD_BUTTON_MISC1) == Intent::None);
    CHECK(p.binding(Intent::PagePrev) == Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, +1));
    CHECK(p.binding(Intent::Search) == Binding::button(SDL_GAMEPAD_BUTTON_BACK));
    CHECK(p.binding(Intent::Up).kind == Binding::None);   // directions are not bindings
    // The D-pad is never rebindable away: even a profile that bound the same button elsewhere keeps the direction.
    NavProfile q = p;
    q.binds[0] = Binding::button(SDL_GAMEPAD_BUTTON_DPAD_UP);
    CHECK(q.intentForButton(SDL_GAMEPAD_BUTTON_DPAD_UP) == Intent::Up);
    CHECK(q.intentForButton(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) == Intent::Right);
}

static void testResolveFamilyOverride() {
    Config c;
    NavProfile p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.family == PadFamily::Xbox && !p.familyOverridden);
    CHECK(p.intentForButton(SOUTH) == Intent::Accept);

    c.set(navKeyPrefix(GUID) + "family", "switch");
    p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.family == PadFamily::Switch && p.familyOverridden);
    CHECK(p.intentForButton(EAST) == Intent::Accept);
    CHECK(p.intentForButton(SOUTH) == Intent::Back);

    c.set(navKeyPrefix(GUID) + "family", "bogus");   // an unreadable family falls back to the pad's type
    p = resolveNavProfile(c, GUID, SWITCH_PRO);
    CHECK(p.family == PadFamily::Switch && !p.familyOverridden);

    // setNavFamily: a key writes, "auto" (or anything else) erases.
    Config d;
    setNavFamily(d, GUID, "playstation");
    CHECK(d.get(navKeyPrefix(GUID) + "family") == "playstation");
    CHECK(resolveNavProfile(d, GUID, XBOX).family == PadFamily::PlayStation);
    setNavFamily(d, GUID, "auto");
    CHECK(!d.has(navKeyPrefix(GUID) + "family"));
}

static void testSetSwaps() {
    Config c;
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(NORTH), kNoReserved) == NavSet::Swapped);
    NavProfile p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.binding(Intent::Accept) == Binding::button(NORTH));
    CHECK(p.binding(Intent::Aux) == Binding::button(SOUTH));
    CHECK(p.intentForButton(NORTH) == Intent::Accept);
    CHECK(p.intentForButton(SOUTH) == Intent::Aux);
    CHECK(p.intentForButton(EAST) == Intent::Back);   // untouched
    CHECK(c.has(navKeyPrefix(GUID) + "confirm") && c.has(navKeyPrefix(GUID) + "favorite"));
    CHECK(keyCount(c, GUID) == 2);
    // Every intent stays bound to a distinct input.
    for (size_t i = 0; i < 10; i++)
        for (size_t j = i + 1; j < 10; j++) CHECK(!(p.binds[i] == p.binds[j]));
    // A button nobody uses: just a move, no swap.
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), kNoReserved) == NavSet::Ok);
    CHECK(resolveNavProfile(c, GUID, XBOX).intentForButton(SDL_GAMEPAD_BUTTON_MISC1) == Intent::Search);
    CHECK(resolveNavProfile(c, GUID, XBOX).intentForButton(SDL_GAMEPAD_BUTTON_BACK) == Intent::None);
    // Setting what is already there changes nothing.
    const size_t n = keyCount(c, GUID);
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), kNoReserved) == NavSet::Ok);
    CHECK(keyCount(c, GUID) == n);
}

static void testSetDefaultErases() {
    Config c;
    // The family default is never written.
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(SOUTH), kNoReserved) == NavSet::Ok);
    CHECK(keyCount(c, GUID) == 0);
    // A swap that lands both intents back on their defaults erases both keys.
    setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(NORTH), kNoReserved);
    CHECK(keyCount(c, GUID) == 2);
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(SOUTH), kNoReserved) == NavSet::Swapped);
    CHECK(keyCount(c, GUID) == 0);
    // "Default" is the family's: on a Switch pad Accept=EAST is the default and is not stored.
    CHECK(setNavBinding(c, GUID, SWITCH_PRO, Intent::Accept, Binding::button(EAST), kNoReserved) == NavSet::Ok);
    CHECK(keyCount(c, GUID) == 0);
    // ... while the same choice on an Xbox pad is a swap and is stored.
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(EAST), kNoReserved) == NavSet::Swapped);
    CHECK(c.get(navKeyPrefix(GUID) + "confirm") == Binding::button(EAST).serialize());
    // Defaults are measured against the family in force (an override), not the pad's type.
    Config d;
    setNavFamily(d, GUID, "switch");
    CHECK(setNavBinding(d, GUID, XBOX, Intent::Accept, Binding::button(EAST), kNoReserved) == NavSet::Ok);
    CHECK(!d.has(navKeyPrefix(GUID) + "confirm"));
}

static void testSetReservedRefused() {
    Config c;
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(SDL_GAMEPAD_BUTTON_DPAD_UP), kNoReserved) == NavSet::Reserved);
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), kNoReserved) == NavSet::Reserved);
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Back, Binding::axis(SDL_GAMEPAD_AXIS_LEFTX, +1), kNoReserved) == NavSet::Reserved);
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Back, Binding::axis(SDL_GAMEPAD_AXIS_LEFTY, -1), kNoReserved) == NavSet::Reserved);
    // The pad's C_TVMENU bindings, hold flag or not.
    const std::vector<Binding> reserved = {Binding::button(SDL_GAMEPAD_BUTTON_MISC1).withHold()};
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), reserved) == NavSet::Reserved);
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1).withHold(), reserved) == NavSet::Reserved);
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC2), reserved) == NavSet::Ok);
    // A keyboard key and a non-nav intent are not for a pad profile either.
    CHECK(setNavBinding(c, OTHER, XBOX, Intent::Accept, Binding::key(SDL_SCANCODE_Z), kNoReserved) == NavSet::Reserved);
    CHECK(setNavBinding(c, OTHER, XBOX, Intent::Up, Binding::button(SDL_GAMEPAD_BUTTON_MISC3), kNoReserved) == NavSet::Reserved);
    CHECK(keyCount(c, OTHER) == 0);
    CHECK(keyCount(c, GUID) == 1);   // only the one accepted Search binding
}

static void testTriggerToButtonAndBack() {
    Config c;
    // Page prev from the left trigger to a face-less button: the trigger stops paging.
    CHECK(setNavBinding(c, GUID, XBOX, Intent::PagePrev, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_STICK), kNoReserved) == NavSet::Ok);
    NavProfile p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.intentForButton(SDL_GAMEPAD_BUTTON_LEFT_STICK) == Intent::PagePrev);
    CHECK(p.intentForTrigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == Intent::None);
    CHECK(p.intentForTrigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == Intent::PageNext);
    // ... and back: the default is erased, not stored.
    CHECK(setNavBinding(c, GUID, XBOX, Intent::PagePrev, Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, +1), kNoReserved) == NavSet::Ok);
    CHECK(keyCount(c, GUID) == 0);
    p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.intentForTrigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == Intent::PagePrev);
    // A button onto the right trigger swaps with Page next, which takes the button.
    setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, +1), kNoReserved);
    p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.intentForTrigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == Intent::Accept);
    CHECK(p.intentForButton(SOUTH) == Intent::PageNext);
    // The stored form is a trigger binding: only +1 on a trigger is usable.
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Alt, Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -1), kNoReserved) == NavSet::Reserved);
    CHECK(setNavBinding(c, GUID, XBOX, Intent::Alt, Binding::axis(SDL_GAMEPAD_AXIS_RIGHTX, +1), kNoReserved) == NavSet::Reserved);
}

static void testClearRemovesAll() {
    Config c;
    setNavFamily(c, GUID, "switch");
    setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(NORTH), kNoReserved);
    setNavBinding(c, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), kNoReserved);
    setNavBinding(c, OTHER, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), kNoReserved);
    c.set("input.padguid." + GUID + ".A", "bsouth");   // a game binding of the same model is not ours
    CHECK(keyCount(c, GUID) >= 3);
    clearNavProfile(c, GUID);
    CHECK(keyCount(c, GUID) == 0);
    CHECK(c.has("input.padguid." + GUID + ".A"));
    CHECK(keyCount(c, OTHER) == 1);
    const NavProfile p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.family == PadFamily::Xbox && !p.familyOverridden);
    CHECK(p.intentForButton(SOUTH) == Intent::Accept);
}

static void testUnknownGuidDefaults() {
    Config c;
    setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(NORTH), kNoReserved);
    setNavFamily(c, GUID, "switch");
    const NavProfile p = resolveNavProfile(c, OTHER, SDL_GAMEPAD_TYPE_PS5);   // a model with no keys: its family default
    CHECK(p.family == PadFamily::PlayStation && !p.familyOverridden);
    for (size_t i = 0; i < 10; i++) CHECK(p.binds[i] == familyNavProfile(PadFamily::PlayStation).binds[i]);
    const NavProfile e = resolveNavProfile(c, "", XBOX);   // no GUID at all
    CHECK(e.family == PadFamily::Xbox && e.intentForButton(SOUTH) == Intent::Accept);
    Config d;   // and nothing can be stored without a model to store it for
    CHECK(setNavBinding(d, "", XBOX, Intent::Accept, Binding::button(NORTH), kNoReserved) == NavSet::Reserved);
    setNavFamily(d, "", "switch");
    CHECK(d.withPrefix("").empty());
}

static void testStoredGarbageIgnored() {
    Config c;
    const std::string pre = navKeyPrefix(GUID);
    c.set(pre + "confirm", "dpup");        // a D-pad button
    c.set(pre + "back", "not a binding");
    c.set(pre + "favorite", "k44");        // a keyboard key
    c.set(pre + "start", "bmisc1~hold");   // the hold flag is dropped
    c.set(pre + "search", "a+leftx");      // a stick
    const NavProfile p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.intentForButton(SOUTH) == Intent::Accept);
    CHECK(p.intentForButton(EAST) == Intent::Back);
    CHECK(p.intentForButton(NORTH) == Intent::Aux);
    CHECK(p.intentForButton(SDL_GAMEPAD_BUTTON_BACK) == Intent::Search);
    CHECK(p.binding(Intent::Menu) == Binding::button(SDL_GAMEPAD_BUTTON_MISC1));
    CHECK(p.intentForButton(SDL_GAMEPAD_BUTTON_DPAD_UP) == Intent::Up);
}

static void testRoundTrip() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("vj_pad_profile_test_" + std::to_string((long long)getpid()));
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "config.ini").string();
    std::filesystem::remove(path);
    {
        Config c;
        c.setPath(path);
        setNavFamily(c, GUID, "switch");
        setNavBinding(c, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), kNoReserved);
        setNavBinding(c, GUID, XBOX, Intent::PagePrev, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_STICK), kNoReserved);
        setNavBinding(c, GUID, XBOX, Intent::PageNext, Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, +1), kNoReserved);   // swaps
        CHECK(c.save());
    }
    Config before;
    {
        // The in-memory result the file must reproduce.
        setNavFamily(before, GUID, "switch");
        setNavBinding(before, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), kNoReserved);
        setNavBinding(before, GUID, XBOX, Intent::PagePrev, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_STICK), kNoReserved);
        setNavBinding(before, GUID, XBOX, Intent::PageNext, Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, +1), kNoReserved);
    }
    Config c;
    c.load(path);
    const NavProfile a = resolveNavProfile(before, GUID, XBOX), b = resolveNavProfile(c, GUID, XBOX);
    CHECK(b.family == PadFamily::Switch && b.familyOverridden);
    for (size_t i = 0; i < 10; i++) CHECK(a.binds[i] == b.binds[i]);
    CHECK(b.intentForButton(SDL_GAMEPAD_BUTTON_MISC1) == Intent::Search);
    CHECK(b.intentForTrigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == Intent::PageNext);
    CHECK(b.intentForButton(SDL_GAMEPAD_BUTTON_LEFT_STICK) == Intent::PagePrev);
    CHECK(b.intentForButton(EAST) == Intent::Accept);   // the Switch family under the layer
    std::filesystem::remove(path);
    std::filesystem::remove(dir);
}

static void testFamilyChangeClearsOverrides() {
    // Customise on an Xbox pad, then every way of changing the family drops the overrides and gives pure defaults.
    const char* targets[] = {"switch", "playstation", "generic", "xbox", "auto"};
    for (const char* target : targets) {
        Config c;
        setNavBinding(c, GUID, XBOX, Intent::Accept, Binding::button(NORTH), kNoReserved);
        setNavBinding(c, GUID, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), kNoReserved);
        setNavBinding(c, GUID, XBOX, Intent::PagePrev, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_STICK), kNoReserved);
        CHECK(keyCount(c, GUID) >= 4);
        setNavFamily(c, GUID, target);
        PadFamily want = familyForType(XBOX);
        familyFromKey(target, want);   // "auto" leaves the pad's own family
        const NavProfile p = resolveNavProfile(c, GUID, XBOX), d = familyNavProfile(want);
        CHECK(p.family == want);
        for (size_t i = 0; i < 10; i++) CHECK(p.binds[i] == d.binds[i]);
        CHECK(keyCount(c, GUID) == (std::string(target) == "auto" ? 0u : 1u));   // only the family key remains
    }
    // From an explicit family back to auto: overrides made under the explicit family go too.
    Config c;
    setNavFamily(c, GUID, "switch");
    setNavBinding(c, GUID, XBOX, Intent::Back, Binding::button(NORTH), kNoReserved);
    CHECK(keyCount(c, GUID) >= 3);
    setNavFamily(c, GUID, "auto");
    CHECK(keyCount(c, GUID) == 0);
    const NavProfile p = resolveNavProfile(c, GUID, XBOX);
    CHECK(p.family == PadFamily::Xbox && !p.familyOverridden);
    for (size_t i = 0; i < 10; i++) CHECK(p.binds[i] == familyNavProfile(PadFamily::Xbox).binds[i]);
    // Another model's layer and this model's game bindings are untouched.
    Config d;
    setNavBinding(d, OTHER, XBOX, Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_MISC1), kNoReserved);
    d.set("input.padguid." + GUID + ".A", "bsouth");
    setNavFamily(d, GUID, "switch");
    CHECK(keyCount(d, OTHER) == 1 && d.has("input.padguid." + GUID + ".A"));
}

static void testButtonNames() {
    CHECK(navButtonName(PadFamily::Switch, Binding::button(EAST)) == "A");
    CHECK(navButtonName(PadFamily::Switch, Binding::button(SOUTH)) == "B");
    CHECK(navButtonName(PadFamily::Switch, Binding::button(WEST)) == "Y");
    CHECK(navButtonName(PadFamily::Switch, Binding::button(NORTH)) == "X");
    CHECK(navButtonName(PadFamily::PlayStation, Binding::button(SOUTH)) == "Cross");
    CHECK(navButtonName(PadFamily::PlayStation, Binding::button(EAST)) == "Circle");
    CHECK(navButtonName(PadFamily::PlayStation, Binding::button(WEST)) == "Square");
    CHECK(navButtonName(PadFamily::PlayStation, Binding::button(NORTH)) == "Triangle");
    CHECK(navButtonName(PadFamily::Xbox, Binding::button(SOUTH)) == "A");
    CHECK(navButtonName(PadFamily::Xbox, Binding::button(NORTH)) == "Y");
    CHECK(navButtonName(PadFamily::Generic, Binding::button(WEST)) == "X");
    CHECK(navButtonName(PadFamily::Switch, Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, +1)) == "ZL");
    CHECK(navButtonName(PadFamily::Switch, Binding::axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, +1)) == "ZR");
    CHECK(navButtonName(PadFamily::PlayStation, Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, +1)) == "L2");
    CHECK(navButtonName(PadFamily::Xbox, Binding::axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, +1)) == "RT");
    CHECK(navButtonName(PadFamily::Xbox, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) == "LB");
    CHECK(navButtonName(PadFamily::PlayStation, Binding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) == "R1");
    CHECK(navButtonName(PadFamily::Switch, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) == "L");
    CHECK(navButtonName(PadFamily::Xbox, Binding::button(SDL_GAMEPAD_BUTTON_START)) == "Menu");
    CHECK(navButtonName(PadFamily::PlayStation, Binding::button(SDL_GAMEPAD_BUTTON_START)) == "Options");
    CHECK(navButtonName(PadFamily::Switch, Binding::button(SDL_GAMEPAD_BUTTON_START)) == "+");
    CHECK(navButtonName(PadFamily::Xbox, Binding::button(SDL_GAMEPAD_BUTTON_BACK)) == "View");
    CHECK(navButtonName(PadFamily::PlayStation, Binding::button(SDL_GAMEPAD_BUTTON_BACK)) == "Create");
    CHECK(navButtonName(PadFamily::Switch, Binding::button(SDL_GAMEPAD_BUTTON_BACK)) == "-");
    // Anything else is Binding::display().
    const Binding m = Binding::button(SDL_GAMEPAD_BUTTON_MISC1);
    CHECK(navButtonName(PadFamily::Xbox, m) == m.display());
    const Binding ls = Binding::button(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    CHECK(navButtonName(PadFamily::Switch, ls) == ls.display());
}

static void testButtonLabelNoHandle() {
    // No open pad (null handle): the family's names; an intent the profile leaves unbound has no caption.
    CHECK(padButtonLabel(nullptr, familyNavProfile(PadFamily::Switch), tv::Intent::Accept) == "A");   // Switch confirms on its right-hand A
    CHECK(padButtonLabel(nullptr, familyNavProfile(PadFamily::PlayStation), tv::Intent::Accept) == "Cross");
    CHECK(padButtonLabel(nullptr, familyNavProfile(PadFamily::Xbox), tv::Intent::SectionNext) == "RB");
    CHECK(padButtonLabel(nullptr, familyNavProfile(PadFamily::Xbox), tv::Intent::Up).empty());   // directions are not rebindable
    CHECK(padButtonLabel(nullptr, familyNavProfile(PadFamily::Xbox), tv::Intent::None).empty());
}

int main() {
    testFamilyForEveryType();
    testIntentNames();
    testXboxMatchesIntentForButton();
    testSwitchSwapsConfirmBack();
    testTriggersPage();
    testResolveFamilyOverride();
    testSetSwaps();
    testSetDefaultErases();
    testSetReservedRefused();
    testTriggerToButtonAndBack();
    testClearRemovesAll();
    testUnknownGuidDefaults();
    testStoredGarbageIgnored();
    testRoundTrip();
    testFamilyChangeClearsOverrides();
    testButtonNames();
    testButtonLabelNoHandle();
    std::printf("pad_profile: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
