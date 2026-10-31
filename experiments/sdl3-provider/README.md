# SDL3 provider experiment (option 2)

This folder contains a separate 32-bit GInput SDL3 provider and a rebuilt MISI
bridge. The normal GInput source, build, and existing compiled ASI are preserved.
All experimental builds, downloaded dependencies, and test outputs stay here.

With the packaged configuration, the arrangement is:

```text
Controller -> SDL3 -> GInputSDL3.asi -> MISI controller callbacks
                                            |
                                      twin_stick.lua
                                            |
                               menus, movement, aiming, driving

Game events -> GInputSDL3Hooks.dll (vibration only) -> SDL3 rumble
```

TwinStick and MISI retain their game patches and input handling. GInput's input
hooks remain disabled. The optional DLL observes game state for vibration
without writing player controls or sending keyboard input. Set both feature
toggles to `0` for a provider with no game callbacks at all.

## Independent input and vibration toggles

`GInputSDL3.ini`, next to the provider:

```ini
[SDL3]
EnableGameInputHooks=0
EnableGameVibration=1
DeadZonePercent=20
TriggerThresholdPercent=25
AllowBackgroundInput=0
```

| Input hooks | Game vibration | Behavior |
| --- | --- | --- |
| `0` | `0` | Provider only; `GInputSDL3Hooks.dll` is not loaded. |
| `0` | `1` | TwinStick owns controls; the DLL observes game events and sends SDL3 rumble. Packaged default. |
| `1` | `0` | GInput classic controls, with game vibration disabled. Disable TwinStick's script. |
| `1` | `1` | GInput classic controls and the independent vibration observer. Disable TwinStick's script. |

Restart the game after changing either toggle. `EnableGameInputHooks=0` prevents
installation of GInput's control hooks, even when the DLL is loaded for vibration.
These are startup settings, not live switches that remove patches mid-game.
It does not disable hooks belonging to MISI, TwinStick, or another loaded mod.

`GInputSDL3Hooks.dll` now has two independently enabled features. Its classic
adapter reuses this repository's original control code,
generated into `build/classic/`, with SDL3 polling and rumble substituted for
XInput. Its layout settings are in `GInputSDL3Hooks.ini`. The provider
INI supplies the stick deadzone for all controllers in this experiment; the
older per-pad deadzone settings do not override it. `EnableGameVibration` controls
game vibration in either mode; the old `Vibration` setting is no longer used.

Vibration observes gunfire, health loss, vehicle impacts, driving over grass,
and the on-foot special action. Effects use SDL3 rumble on the same selected
controller as TwinStick, including native DualSense input. Shot and injury pulses
last 100 ms; vehicle/special feedback uses 50 ms pulses. Weaker continuous effects
do not prolong a stronger gunshot effect indefinitely. Player/controller changes,
death, and pause reset effect history. This is regular controller rumble.

Existing INIs without `EnableGameVibration` leave it disabled. Add the setting
or copy the new INI to enable it. Remove the old XInput-only vibration ASI when
using the new observer so two plugins do not send competing vibration commands.

## Build

Requirements: Windows, Python 3, Visual Studio 2022 C++ desktop build tools and
a Windows SDK. For the optional game adapter (input or vibration), set `PLUGIN_SDK_DIR` to a built
Plugin-SDK checkout with the compatibility patch documented in the repository's
main README. The default SDK location is the sibling `plugin-sdk` folder.

From this folder in PowerShell:

```powershell
.\fetch-dependencies.ps1
.\build.ps1
```

The dependency download script pins and verifies SHA-256 hashes for:

- [SDL3 3.4.18 VC development package](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18).
- [Dege's modified MISI v0.5.0 source archive](https://gtamp.com/forum/download/file.php?id=1891),
  published in the [TwinStick release thread](https://gtamp.com/forum/viewtopic.php?t=1150).
  It includes the Lua 5.3.4 runtime used to build the bridge.

`prepare.py` creates build inputs without changing downloaded source files or
the original GInput repository files. MISI's three XInput callback definitions
are replaced by `src/MisiBridge.cpp`; their Lua names stay the same. A generated
`build/misi-sdl3.patch` records that change. Its native angle, speed, and other
game functions remain those of the original MISI archive.

Build outputs are in `dist/`. `dist/package/` contains only the installable
components, licenses, documentation, source patch, and a build manifest.
The full `dist/` also contains debug symbols, libraries, `provider-tests.exe`, and
`vibration-tests.exe`.
Dependencies, generated inputs, and binaries are ignored by Git.

## Try it with TwinStick

Use a separate GTA2 v11.44 test installation, or retain backups of the existing
MISI ASI and game configuration so the experiment can be reversed.

1. Use a 32-bit [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
   that calls the exported `InitializeASI` function outside `DllMain`. Its
   `DontLoadFromDllMain` setting must remain `1` (the default). SDL initialization
   needs that main-thread startup callback; loading the DLL alone does not start it.
2. Temporarily move the original `GInputII.asi` out of every scanned mod folder.
   Also remove `GInputII.vibration.only.II.asi` if installed. Their behavior is
   independent of the experimental toggles.
3. Replace the installed `MISI.ASI` with `dist/package/MISI.ASI`, retaining the
   original elsewhere. Put `GInputSDL3.asi`, `GInputSDL3.ini`, `GInputSDL3Hooks.dll`, and `SDL3.dll` next
   to `gta2.exe` as well. Keep only one copy of each ASI loaded.
4. Keep the current `scripts/twin_stick.lua`. No Lua edits are required. This
   rebuilt MISI still exposes `GetXInputAxes`, `IsXInputKeyPress`, and
   `GetXInputConnectedDevicesNumber`, but they now read the SDL3 provider.
5. Leave `EnableGameInputHooks=0` and `EnableGameVibration=1`. The matching
   `GInputSDL3Hooks.dll` is required for vibration even though input hooks are
   disabled. `GInputSDL3Hooks.ini` is needed only for classic controls.

The rebuilt MISI retains its original v11.44 executable checks. This experiment
does not extend MISI/TwinStick to 9.6 or to other executable builds. A working
original GInput on v11.44 does not establish compatibility for the rebuilt MISI.

`GInputSDL3.log` is written next to the provider when the loader calls
`InitializeASI`. It should report `Initialized=1`, `EnableGameInputHooks=0`, and
`EnableGameVibration=1`. Runtime SDL rumble errors are appended to this log.
An absent log suggests the startup callback was not called, the DLL/dependency
failed to load, or the directory could not be written. Check `MISI.log` too.

`GInputSDL3Hooks.log` records installation of the vibration observer, the first
player callback, the selected controller, observer state changes, and occasional
rumble results. After entering a level, expect `Player callback reached` and
`Observer=tracking player`; firing a gun should produce `Rumble=sent`.

The observer samples the player update call at `0x004A7683`, the point used by
the working vibration-only ASI. The earlier prototype's generic frame callbacks
at `0x0045C3E9`/`0x0045C3F2` were bypassed by Seuko in the development game.
The replacement still only observes controls and player state; it does not write
input, movement, or aiming. Ammo decreases are retained until the weapon's
firing-timer phase so timing differences cannot silently swallow a gunshot.

To try classic controls instead, disable/move `twin_stick.lua`, add the optional
`GInputSDL3Hooks.dll` and `GInputSDL3Hooks.ini` next to the provider, and set the
input toggle to `1`. Either feature of this DLL inherits the original
GInput/SDK address requirements. Always update the provider ASI and helper DLL
together; an older helper is rejected rather than enabling unintended controls.

## Input behavior and API

### Seuko compatibility checks

The original development installation used Seuko v1.2.0. A live process inspection
confirmed that it replaced both generic frame callbacks and called the original
game tick directly, bypassing the first vibration observer implementation.

The official [Seuko v1.6.0 ASI release](https://github.com/izawartka/gta2-seuko-mod/releases/tag/v1.6.0)
has been installed and tested in the development game. Its archive matches the
SHA-256 published in GitHub's release metadata:
`26d1f7d12af3745c046f2e92656d5b8ecff28de48a601db4457c5260467389da`.

Its [game-tick hook](https://github.com/izawartka/gta2-seuko-mod/blob/v1.6.0/src/events/game-tick.cpp)
still uses `0x0045C3E9`/`0x0045C3F2`, and its
[multi-call hook installer](https://github.com/izawartka/gta2-seuko-mod/blob/v1.6.0/src/hook-types/function-multi-call-hook.h)
still replaces calls without chaining an existing callback. The corrected
vibration observer uses the separate `0x004A7683` call. The v1.6.0 binary does not
contain that hook address. Live process inspection confirmed v1.6.0 active at
the frame callbacks and `GInputSDL3Hooks.dll` active at `0x004A7683`. The user
confirmed Seuko's F11 menu, TwinStick movement/aiming, and vibration all working
together. The corrected vibration callback remains necessary with this release.

The disabled v1.2.0 ASI remains in the test game's scripts folder as `.asi_`;
the existing `seukomod.dat` was backed up as `.before-seuko-1.6.0` before installing
the new release. Seuko is not bundled with the SDL3 provider package.

### Provider behavior

- Up to four gamepads have stable slots until disconnected. The MISI bridge
  uses the first connected slot for both axes and buttons. It does not combine
  different controllers into one player as the old MISI functions did.
- Axes use `-1..1` with up positive, matching TwinStick. The per-axis deadzone
  zeros small inputs without rescaling the remaining magnitude, matching the
  original MISI behavior. Triggers retain analog values and also expose buttons
  at the configured threshold. Button positions follow SDL's standard layout.
- Input becomes neutral while another process is in the foreground, unless
  `AllowBackgroundInput=1`. Connected status remains available.
- No polling worker or game input hook is needed for TwinStick. MISI asks
  the provider to update through its existing controller callbacks. SDL is
  initialized by the loader's main-thread `InitializeASI` callback, and polling
  and snapshot reads are serialized for MISI's script threads.
- With vibration enabled, the optional DLL subscribes to engine initialization,
  engine shutdown, and the player input update event. It only reads player state and
  requests rumble; it does not register GInput's menu/button/movement handlers.
- The provider owns SDL controller polling in this prototype. It disables SDL
  joystick/gamepad events while active so an unused event queue cannot fill up,
  and restores their previous settings on explicit shutdown.
- `include/GInputSDL3.h` defines a versioned C API for another native consumer:
  initialize on the main thread, call `Update`, then read snapshots/counts or
  request rumble. Check return values and `GetLastError`. Getters alone do not poll.
  The new API does not implement the original `GInputII` C++ API.
- This is a process-lifetime plugin. The diagnostic host explicitly shuts SDL
  down on its initialization thread. The input/vibration adapter also shuts it down at
  engine shutdown. Pure provider mode does no SDL cleanup under the loader lock on
  process exit, and hot-unloading it while MISI is running is unsupported.

The implementation follows SDL's
[main-thread initialization requirement](https://wiki.libsdl.org/SDL3/SDL_InitSubSystem)
and [thread-safe gamepad polling API](https://wiki.libsdl.org/SDL3/SDL_UpdateGamepads).

For troubleshooting, `python inspect_game.py` prints loaded ASI modules, callback
targets, and player state from an already running GTA2 process. It only reads
memory. If the game runs with administrator privileges, run this inspection from
an equally privileged terminal. It does not inject code or change game memory.

## Validation and rollback

`build.ps1` builds all components and runs `provider-tests.exe` using SDL virtual
controllers and MISI's Lua runtime. It checks provider-only startup outside GTA2,
adapter isolation and independent feature requests, axis normalization/Y inversion, button/trigger mapping,
deadzones, Lua callbacks, concurrent polling, disconnection cleanup, stable slots,
shutdown, and actual rumble dispatch to virtual controllers, including routing
after controller disconnection. `vibration-tests.exe` checks game-effect
detection, effect mixing/expiry, and history resets without loading game code.
When real controllers occupy the first slot, pad-specific Lua
assertions are skipped; native virtual-controller assertions still run.

The provider, rebuilt MISI, and optional input/vibration adapter compile successfully.
Virtual-controller and effect tests pass. The user has confirmed TwinStick input
and in-game vibration working with the SDL3 prototype after the Seuko callback
conflict was fixed. The observer log also confirms player callbacks and successful
SDL3 rumble requests with input hooks disabled. **Enabled classic controls have
not yet been tested in GTA2.** Check menu navigation, independent
movement/aiming, gunfire/damage vibration, driving, pause, disconnect/reconnect,
and focus changes in-game. Physical DualSense rumble still needs confirmation.
The bridge rebuilds an older MISI archive, so regressions compared with a different
installed MISI binary remain possible.

To roll back, close the game, remove the experimental files from the test game,
restore the original `MISI.ASI`, and restore the original input/vibration ASIs
if previously used.
Your TwinStick Lua and the original GInput sources/build remain available. The
SDL3 provider API can also serve a future native TwinStick mode (option 3).
