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
  with the offset built in the clean camera basis and the asymmetric
  forward/back lean limits (0.40m in, 0.10m back) acting as safety stops rather
  than scaling. The mod adds no pose shaping of its own.
- Added position diagnostics at three stages: what the tracker sent (`rawPos`,
  metres), what survived smoothing and the limits (`procPos`), and the world
  vector applied to the camera (`posOff`). Too much or too little movement can
  now be traced to the tracker profile or to the mod without guessing.
- Added a build profile for the Steam 2026-05-21 build (PE 0x6A0E94F2). The mod
  fingerprints the EXE at startup and stays fully dormant on any build it does
  not recognise, so a patch leaves the game running vanilla.
- Added `pixi run check-fingerprint` and a daily patch-watch workflow.
