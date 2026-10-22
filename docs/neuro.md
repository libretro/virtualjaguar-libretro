# Neuro API: let an AI play

The desktop app can be played by [Neuro-sama](https://www.twitch.tv/vedal987), or by any AI that speaks
the [Neuro game API](https://github.com/VedalAI/neuro-sdk) (the SDK's test bots Randy, Tony and Gary,
or a backend of your own). The app connects to the API server as a game client, offers the Jaguar
controller as **actions**, and presses what the model chooses as **Player 1**. Your own controls keep
working at the same time.

A Jaguar game knows nothing about Neuro, so the API's game client is the emulator itself, with game
**profiles** that name the moves of particular games ("fire", "superzapper") and describe their
controls to the model.

## Turning it on

- **Settings > Neuro:** tick *Connect to Neuro* and set the server (`ws://localhost:8000` by default,
  which is where Randy listens). The connection retries every few seconds until the server is up.
- **Command line, one run:** `virtualjaguar --neuro ws://HOST:PORT game.j64`. Nothing is saved.
- The default server comes from `NEURO_SDK_WS_URL` when it is set, like the official SDKs.

Only `ws://` is supported (no TLS); Neuro API servers normally run on the same machine or the LAN.
`ws://` is plaintext: Settings > Neuro warns when the server (also one from `NEURO_SDK_WS_URL`) is not
on this machine, and the privacy page (`site/pages/privacy.html`) lists exactly what is sent: the game's
title, the actions, the held buttons, the profile's RAM watch values and the pause state.

**RetroAchievements hardcore blocks Neuro**, as it does cheats and save-state loads: an AI is not a
human player. While hardcore is active the app does not connect (or drops the link and lets go of every
button), Settings > Neuro says why, and a toast appears when it gets blocked. When hardcore ends,
Neuro connects again by itself if it is ticked.

**Emulation thread:** Neuro works with Settings > Interface > *Emulation thread* on or off, and plays the same either
way. The UI thread keeps the connection and the list of accepted moves and hands that program to the emulation thread
through the input snapshot; the core's input poll plays it one step per emulated frame, so `press_buttons frames=3` is
three emulated frames at any display rate and under fast-forward, never three UI frames. Game profile values (`watch`)
are copied from RAM by the emulation thread after each frame (about 10 times a second) and the state text reads that
copy four times a second, so a value can be a third of a second old; a profile whose watches lie more than 64 KiB apart
makes the app copy the whole 2 MB of RAM at that rate while Neuro is connected. Menus, a mouse on Player 1, hardcore and
a dropped connection let go of every button within one UI frame (with the emulation thread on, the release reaches the
core with the next publish, which under fast-forward can be a few emulated frames later).

You can tell the AI is playing: a toast on connect and disconnect, and while it is connected with a game
running a small "AI plays P1" chip in the top right corner of the picture (menus and windows cover it,
like the FPS chip).

Config keys (`virtualjaguar.cfg`): `neuro.enabled`, `neuro.url`, `neuro.auto_force` (default on),
`neuro.force_interval` (seconds, default 1), `neuro.game_name` (overrides the API game name).

## What she gets

On connecting the app sends `startup`, registers the actions, and sends the profile's `about` text as
silent context. The API's `game` name is the profile's `game_name` ("Tempest 2000"), else the game's
title from the library, else "Atari Jaguar". Loading a different game reconnects under its name, since
the API fixes the name for a session.

With *Ask for the next move* on, the app sends `actions/force` whenever her last input has played out
and the game is running (never while paused or halted in the debugger, and only one force at a time).
The force carries a short state text: the title, paused or running, the held buttons, and the profile's
RAM watches. A move she makes outside a force is queued too. Pausing and resuming are sent as silent
context.

Results are sent once the action is validated, before its input plays out, as the API asks. An invalid
action (an unknown button, too many frames) fails with a message saying why; Neuro retries a failed
forced action.

### Controller actions

Every game has these unless its profile turns them off (`"generic_actions": false`). Button names:
`up down left right a b c option pause 0`-`9 star hash` (`*` and `#` work too). A frame is 1/60 s.

| action | parameters | does |
|---|---|---|
| `press_buttons` | `buttons`, `frames` (1-600) | hold the buttons together, then let go |
| `button_sequence` | `steps: [{buttons, frames}]` (1-20 steps of 1-90 frames) | steps back to back; empty `buttons` = a pause |
| `hold_buttons` | `buttons` | keep them held until `release_all` or another `hold_buttons` |
| `release_all` | | let go of everything and drop what is queued |

At most 30 s of input can be queued at once. A dropped connection, turning Neuro off, or hardcore
starting lets go of every held button.

Actions fail (and Neuro is told why) while Player 1's buttons would not reach the game: the in-game menu
is open (under netplay the game keeps running behind it), or Player 1 is set to a mouse (the core drops
the pad on a port whose mouse is in use). What is queued at that moment is dropped, and no new moves are asked for until input is back.

## Game profiles

A profile is a JSON file in `<data>/neuro/` (`<data>` is the app's data folder; Settings > Storage
shows it). Files there are tried before the built-in ones, and one with the same `game_name` replaces
a built-in profile. Problems are printed to the log at start-up as `[neuro] profile skipped: ...`.

```json
{
  "game_name": "Tempest 2000",
  "titles": ["Tempest 2000"],
  "crc32": ["0x1234ABCD"],
  "about": "What the game is and how to play it, sent as context when it starts.",
  "generic_actions": true,
  "watch": [
    {"name": "score", "address": "0x01A2B4", "size": 4, "format": "bcd"},
    {"name": "lives", "address": "0x01A2B8", "size": 1, "note": "(the game starts with 3)"}
  ],
  "actions": [
    {"name": "fire", "description": "Fire down the current lane.",
     "steps": [{"buttons": ["b"], "frames": 3}], "gap": 3, "max_times": 30}
  ]
}
```

- `titles` match the start of the game's title, ignoring case; `crc32` matches the library's CRC of
  the file (the one without a copier header when it has one). A profile needs one or the other.
- `watch` reads big-endian values from main RAM (0x000000-0x1FFFFF) into the state text: `size` 1, 2
  or 4, `format` `dec`, `hex` or `bcd`. Tools > Cheat scanner finds the addresses. The values are read
  four times a second (see the emulation thread note above for how old one can be).
- `actions` are button macros. Names are `lowercase_with_underscores` and need a description. With
  `max_times` above 1 the action takes `{"times": N}` and repeats the steps N times, with `gap` empty
  frames between repeats.

### Tempest 2000 (built in)

Its actions are `fire`, `move_left`, `move_right`, `fire_moving_left`, `fire_moving_right`,
`superzapper` and `jump`, plus the controller actions for menus. The button roles come from the game's
manual; the frame counts are a first guess and have not been tuned against the game, and it has no RAM
watches yet. Contributions with tested timings and the score/lives addresses are welcome.

## Testing with Randy

Randy (in the SDK repo) answers every force with a random valid action:

```sh
cd neuro-sdk/Randy && npm install && npm start      # ws://localhost:8000
virtualjaguar --neuro ws://localhost:8000 game.j64
```

`--headless --frames N` works with `--neuro` too; the frames then run in real time so the server can
keep up.

The client is tested by `ctest -R neuro`: `neuro_unit`, `neuro_loopback` and `neuro_loopback_threaded` (a WebSocket
server on 127.0.0.1; the second polls the buttons from another thread), `neuro_core_e2e` (the real core's joypad matrix,
inline and threaded), `neuro_app_inline` / `neuro_on_threaded` (the app headless against that server) and
`neuro_watch_*` (a profile watch read back from RAM).

The code is in `app/src/neuro/`. It is built by default; configure with `-DVJ_APP_NEURO=OFF` to leave it
out (no Settings > Neuro tab, no `--neuro`, no `neuro_*` tests).
