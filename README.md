# Pinball FX Head Tracking

![Pinball FX running with this mod](https://raw.githubusercontent.com/itsloopyo/pinball-fx-headtracking/main/assets/readme-clip.gif)

Head tracking for Pinball FX: move your head and the camera moves with it, so you can look around naturally in game, on a normal monitor, no VR headset required.

## Features

- **6DOF head tracking** - look around and lean for the parallax to see past a ramp or under an upper playfield

## Requirements

- [Pinball FX](https://store.steampowered.com/app/2328760/) on Steam.
- A head tracker that speaks the [OpenTrack](https://github.com/opentrack/opentrack) UDP protocol: OpenTrack itself with any of its inputs (webcam, TrackIR, Tobii, SteamVR), or a phone app such as [Headcam](https://headcam.app), which turns any phone you already own into a tracker, for free.
- Windows 10 or 11, 64-bit. No Visual C++ redistributable to install, the mod is statically linked.

## Installation

1. Download `PinballFXHeadTracking-v<version>-installer.zip` from the [Releases page](https://github.com/itsloopyo/pinball-fx-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds your Steam install, drops the ASI loader and the mod next to the game EXE, and writes a default `HeadTracking.ini`.
4. In OpenTrack, set the output to UDP over network, address `127.0.0.1`, port `4242`.
5. Launch the game.

Or install it in one click with [Lopari](https://lopari.app).

If the installer cannot find your game, point it at the install root yourself, either way round:

```powershell
# positional argument
install.cmd "D:\Games\Steam\steamapps\common\Pinball FX"

# or an environment variable
$env:PINBALL_FX_PATH = "D:\Games\Steam\steamapps\common\Pinball FX"
```

### Manual Installation

The mod lives beside the real shipping EXE, not in the install root. The root `PinballFX.exe` is only a bootstrap shim. The Nexus ZIP is already laid out this way, so extracting it into the game folder places the mod for you, leaving only the loader to copy.

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` into `<game>\PinballFX\Binaries\Win64\`, renamed to `winmm.dll`. That is the ASI loader; `PinballFX-Win64-Shipping.exe` imports `WINMM.dll` directly, so it loads with no launch options.
2. Copy `plugins\PinballFXHeadTracking.asi` into the same folder.

## Setting Up OpenTrack

In OpenTrack, set **Output** to **UDP over network**, address `127.0.0.1`, port `4242`. Any sample rate works; the mod estimates the incoming rate per stream and interpolates it up to your frame rate.

### VR Headset Setup

1. Connect the headset to the PC over Air Link or Virtual Desktop.
2. Start SteamVR so the headset is tracked.
3. In OpenTrack, set **Input** to **SteamVR**, and **Output** to UDP on `127.0.0.1:4242`.
4. Start the game

### Webcam Setup

1. In OpenTrack, set **Input** to **neuralnet tracker**, which tracks your face from a plain webcam with no markers.
2. Set **Output** to UDP on `127.0.0.1:4242`.
3. Sit at your normal playing distance and center the tracker with OpenTrack's Center bind.
4. Start the game

### Phone App Setup

Any phone app that speaks the OpenTrack UDP protocol works here, and the mod cannot tell which one you are using. I wrote [Headcam](https://headcam.app) so that decent tracking was free for anybody with a phone already in their pocket: no cost, no ads, no account, nothing to buy inside it.

- If the app filters its own signal on the phone, point it straight at your PC's LAN IP on port `4242`. Headcam filters on-device, so it can send direct.
- If the app sends a raw or lightly filtered feed, or you want OpenTrack's curve mapping and filters, have the app send to OpenTrack on another port (5252, say) and let OpenTrack output UDP to `127.0.0.1:4242`.

Not sure which yours is? Try direct first, then hold your head still and watch the view. If it drifts or shakes, route it through OpenTrack.

A phone on WiFi is classed as a remote connection and uses `RemoteSmoothing`. Only a tracker sending to `127.0.0.1` counts as local: the classifier sees the transport, not the machine, so OpenTrack running on this PC but pointed at your own LAN address is treated as remote and gets `RemoteSmoothing` too.

## Controls

Two equivalent binding sets. Use whichever your keyboard has; the chords exist for keyboards without a nav cluster.

| Action                        | Nav-cluster | Chord          |
|-------------------------------|-------------|----------------|
| Toggle tracking               | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode           | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode (world/local) | `Page Down` | `Ctrl+Shift+H` |

Cycling the tracking mode steps through: normal head tracking, rotation only, position only, and back to normal.

### Tuning the camera framing in game

The `[Camera]` values can be moved while you play, so you can find them by eye instead of by restarting. Two rows under your left hand, one column per value: the top row raises, the row under it lowers.

| Value           | Up             | Down           | Step |
|-----------------|----------------|----------------|------|
| `FovOffset`     | `Ctrl+Shift+Q` | `Ctrl+Shift+A` | 1 deg |
| `OffsetForward` | `Ctrl+Shift+W` | `Ctrl+Shift+S` | 1cm |
| `OffsetUp`      | `Ctrl+Shift+E` | `Ctrl+Shift+D` | 1cm |
| `OffsetRight`   | `Ctrl+Shift+R` | `Ctrl+Shift+F` | 1cm |

`Ctrl+Shift+X` saves the four values into `HeadTracking.ini`, so they come back
next launch. `Ctrl+Shift+Z` puts them back to whatever is saved there. Nothing is
written until you press `Ctrl+Shift+X`, so an experiment costs nothing.

Every change is also logged, whole set at a time, so you can read a set back out
of `HeadTracking.log` if you would rather type it in yourself:

```
[19:14:02.881] framing: OffsetForward -> 80 cm | [Camera] FovOffset=10 OffsetForward=80 OffsetUp=-15 OffsetRight=0
```

There is no recenter key. The mod applies the pose your tracker sends as-is, so center it in the tracker app: OpenTrack's Center bind, or the CENTER button in Headcam.

## Configuration

`HeadTracking.ini` is written on first launch into `<game>\PinballFX\Binaries\Win64\`, next to `PinballFX-Win64-Shipping.exe`. Edit it and restart the game to apply. Any key you leave out keeps its default, so a partial file is valid.

```ini
[Network]
UdpPort=4242

[General]
EnableOnStartup=1
; Yaw mode: 1 = horizon-locked yaw about the world up-axis (default),
; 0 = yaw about the camera's own up-axis. Toggle in-game with Page Down.
WorldSpaceYaw=1

[Hotkeys]
; Virtual-key code for the yaw-mode toggle. 0x22 is Page Down.
YawModeKey=0x22

[Rotation]
; Sensitivities exist for a physically necessary correction, not for taste.
; Shape the pose in your tracker so one profile behaves the same in every game.
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
InvertYaw=0
InvertPitch=0
InvertRoll=0
; Smoothing for a tracker sending to 127.0.0.1. 0.0 = none.
LocalSmoothing=0.0
; Smoothing for a tracker arriving over the network, e.g. a phone on WiFi.
RemoteSmoothing=0.15

[Camera]
; Pin every view to one field of view, in degrees. 0 = leave the game's own.
FovOverride=0
; Add this many degrees to whatever the current view asks for, keeping the
; relative framing of each view. FovOverride wins if you set both.
FovOffset=0
; Move the camera the game placed, in centimetres, while a table is in play.
; Forward runs along the line of sight: negative pulls back, positive pushes in.
OffsetForward=0
OffsetUp=0
OffsetRight=0

[GameState]
; Track only during a table actually in play. The front end, table select,
; loading, the table guide and the pause screen hold the camera still.
GameplayOnly=1
; Hold the camera still during the table intro fly-in and mid-game cut-ins.
SuppressDuringCameraSequences=1

[Position]
Enabled=1
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; There are no lean limits: the camera follows your head as far as you take it,
; including back out of the cabinet.
```

The pose is mapped 1:1 and nothing is clamped: the camera turns exactly as far as your head turned and moves exactly as far as your head moved, however far that is. Lean back a metre and the camera comes back out of the cabinet with you. Worth knowing on a pinball table specifically, the camera sits under a metre from the playfield with a fairly narrow field of view, so a given amount of head movement shifts the picture far more than the same movement would in a first-person game. Widening the FOV with `FovOffset` calms that down without touching the 1:1 mapping.

Tracking suppressed by `[GameState]` is held, not reset, so the view picks up where your head is when play resumes rather than lurching.

### Cabinet and portrait mode

The cabinet cameras are a long lens. Read straight off the game's own view
info: a desktop table view rendered at 26 degrees of field of view, a cabinet
view at 15. Fifteen degrees is a telephoto lens, and a telephoto lens is what
flattens a table into something close to an orthographic projection and crops
the ends off the longer ones. That is the game composing those shots, and its
own settings offer nothing but the tilt adjustment.

The mod cannot redesign those cameras, but it can change the lens on the one
the game placed and move it. Those are the two halves of a dolly-zoom:

```ini
[Camera]
FovOffset=10       ; wider lens - more perspective, table shrinks in frame
OffsetForward=80   ; ...and dolly in to put the size back
```

Widen and dolly in together and the table keeps roughly the size it had while
the perspective deepens. Widen alone and you simply see more table, smaller,
which on a cropped table is the fix on its own. Start around `FovOffset=6` to
`12`, then move `OffsetForward` until the framing looks right. Both take effect
on the next launch, or tune them live with the chords above.

`OffsetUp` and `OffsetRight` shift the camera across the view: if the cabinet
camera sits higher than you want to look from, `OffsetUp=-15` drops it.

All three apply only while a table is in play. Menus, the table intro fly-in
and the mid-game cut-ins keep the camera the game composed.

Head movement is unaffected by any of this: the mod builds its own axes from
where the camera faces and nothing else, so a lean is a lean whether or not the
game has rolled the view 90 degrees for a rotated screen.

## Troubleshooting

Everything the mod does is written to `HeadTracking.log`, next to the game EXE in `<game>\PinballFX\Binaries\Win64\`. The previous session is kept as `HeadTracking.prev.log`.

**Mod not loading.**

- No log file at all means the ASI loader is not loading. Check that `winmm.dll` and `PinballFXHeadTracking.asi` are both in `PinballFX\Binaries\Win64\` and not in the install root.
- A log saying `staying dormant; game runs vanilla` means the game was patched and this mod version does not know the new build. That is the failsafe working, and the game is untouched. Check the Releases page for an update.

**No tracking response.**

- `udpData=NO` in the log means nothing is arriving on port 4242. Check the tracker is running and outputting UDP, and that a firewall is not blocking it for a phone on WiFi.
- A log line about the UDP port being busy means another app is already listening on 4242, usually another game with a head tracking mod left running, or a second copy of OpenTrack. The mod retries the bind twice a second, so it takes over within about half a second of that app closing.
- If tracking is off when you expect it on, the `gate:` line names which of `GameplayOnly` or `SuppressDuringCameraSequences` stopped it.

**Jittery or unstable tracking.**

- Over WiFi, raise `RemoteSmoothing`; lower it if a clean link feels laggy. `0.15` is the default.
- On a same-machine tracker, `LocalSmoothing` defaults to `0.0`. Raise it slightly if your tracker output is noisy.

**Wrong rotation axis, or the table appears to tilt.**

- Toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked is the default and is horizon-stable; camera-local follows the camera's current up-axis, which leans the view on a steeply pitched table.
- If the view sits off-center, center it in your tracker app, not in the game. The mod deliberately keeps no center of its own so there is only ever one place to do it.

## Updating

Download the new release and run `install.cmd` again, or use Lopari. Your `HeadTracking.ini` is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files. The ASI loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Needs CMake and Visual Studio with the C++ toolchain. The build never needs the game installed.

```powershell
git clone --recursive https://github.com/itsloopyo/pinball-fx-headtracking.git
cd pinball-fx-headtracking
pixi run build              # -> build/Release/PinballFXHeadTracking.asi
pixi run test               # unit tests
pixi run package            # release ZIPs
pixi run check-fingerprint  # compare an installed EXE against the known build profiles
```

The camera hook is pinned to RVAs derived from a specific shipped EXE, so each supported build gets its own append-only profile in `src/builds/steam_offsets.cpp`, selected at runtime by PE fingerprint.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - my free Windows launcher, one-click install and launch for head-tracking mods
- [Headcam](https://headcam.app) - my free phone head-tracker app

## License

MIT License - see [LICENSE](LICENSE) for details.

Bundled and statically linked third-party components keep their own licenses, which the MIT grant does not extend to. They are listed with their full license text in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Pinball FX by [Zen Studios](https://store.steampowered.com/app/2328760/).
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, the loader shim this mod ships with.
- [OpenTrack](https://github.com/opentrack/opentrack) for the head tracking wire protocol.
- [MinHook](https://github.com/TsudaKageyu/minhook) for runtime function hooking.
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) for the shared tracking pipeline.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Zen Studios. Use at your own risk.
