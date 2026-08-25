# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.0] - 2026-08-23

### Added

- Added head tracking for Pinball FX (UE 4.27) as an Ultimate ASI Loader
  plugin. The head pose is injected only for the render-path caller, so audio,
  culling and every other consumer of the view point keep the camera the game
  intended.
- Added 6DOF support. Rotation and position are mapped 1:1 from the tracker,
  and the camera follows the head as far as it goes. The offset is built in a
  horizon-locked basis, so the 90 degree display roll of the cabinet views
  cannot turn a lean sideways into vertical camera motion. The mod adds no pose
  shaping of its own.
- Added `[Camera] OffsetForward`, `OffsetUp` and `OffsetRight`, which move the
  camera the game placed, in centimetres, while a table is in play. Paired with
  `FovOffset` this is a dolly-zoom: the cabinet and portrait views frame the
  table from about four table-lengths back behind a 15 degree lens, and
  widening the lens while dollying in keeps the table the same size on screen
  while restoring the perspective depth that distance flattens out.
- Added in-game tuning for the four framing values, one Ctrl+Shift chord pair
  each (`Q/A`, `W/S`, `E/D`, `R/F`). `Ctrl+Shift+Z` goes back to the camera the
  game placed and `Ctrl+Shift+X` to the last saved set, so the two flip between
  the stock shot and a tuned one; `Ctrl+Shift+M` saves into the INI. Every
  change is logged as a complete `[Camera]` block.
- Added `Ctrl+Shift+V`, which hands the lean the player is holding over to the
  framing offsets: lean to where the table looks right, press it, sit back up,
  and the view stays there. The lean is built in a horizon-locked basis and the
  framing in a pitched one, so the offset is projected between the two rather
  than copied, and the camera does not move at the moment of capture.
- Added position diagnostics at three stages: what the tracker sent (`rawPos`,
  metres), what survived smoothing and the sensitivities (`procPos`), and the world
  vector applied to the camera (`posOff`). Too much or too little movement can
  now be traced to the tracker profile or to the mod without guessing.
- Added a build profile for the Steam 2026-05-21 build (PE 0x6A0E94F2). The mod
  fingerprints the EXE at startup and stays fully dormant on any build it does
  not recognise, so a patch leaves the game running vanilla.
- Added `pixi run check-fingerprint` and a daily patch-watch workflow.
