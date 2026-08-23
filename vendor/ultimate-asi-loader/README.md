# ultimate-asi-loader (vendored)

This directory contains a bundled copy of the upstream mod loader. It is the install-time
source of truth: install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Asset: `Ultimate-ASI-Loader_x64.zip`
- Tag: `v9.7.2`
- Commit: `ab722befd52581a34449b603926cfab476e66b05`
- Upstream URL: https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.7.2/Ultimate-ASI-Loader_x64.zip
- SHA-256: `1c1f9ebf3996df4a4bcf9be0785b9de8f934ef6497f73145602488fc7d8c2909`
- Fetched at: 2026-08-23T20:33:12.3466680+01:00
- Source: github

Do not edit this directory by hand. Run ``pixi run package`` (or CI release) to refresh.

## Committed artifact

Only `dinput8.dll` is committed; the upstream zip is a download intermediate
and is deleted after extraction.

- File: `dinput8.dll` (extracted from the asset above, unmodified)
- SHA-256: `22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c`

It is deployed to `<game>/PinballFX/Binaries/Win64/winmm.dll` as the ASI hook
slot: that is the directory holding the real shipping EXE (the install root only
carries a BootstrapPackagedGame shim), and PinballFX-Win64-Shipping.exe imports
WINMM.dll directly, so the proxy loads without any launch-option changes.
