# Third-Party Notices

PinballFXHeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

PinballFXHeadTracking's own code is MIT licensed, Copyright (c) 2026
itsloopyo / CameraUnlock; that licence is in `LICENSE` and does not extend to
any of the components below.

This repository contains no Pinball FX code, no extracted game assets and no
game data files, and redistributes no part of the game. The one piece of
material it carries that it does not own is the README demo clip, recorded from
the game running with this mod; it is covered below and ships in neither
release ZIP.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| Ultimate ASI Loader | v9.7.2 | MIT | Bundled verbatim in the installer ZIP |
| MinHook | v1.3.3 (`9fbd087`) | BSD-2-Clause | Statically linked into `PinballFXHeadTracking.asi` |
| cameraunlock-core | 371b136584d90163755f0df43e55ed6612952486 | MIT | Statically linked into `PinballFXHeadTracking.asi` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |

---

## Ultimate ASI Loader

- **Version:** `v9.7.2` (commit `ab722befd52581a34449b603926cfab476e66b05`)
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Loads `PinballFXHeadTracking.asi` into the game process, deployed as the `winmm.dll` proxy beside the shipping EXE.
- **Bundled:** yes. Vendored at `vendor/ultimate-asi-loader/`, bundled in the release ZIP and used as the install-time source.

Taken from the upstream release asset untouched; the upstream licence file ships
beside it at `vendor/ultimate-asi-loader/LICENSE`. The vendored
`dinput8.dll` has SHA-256
`22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c`.

```
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## MinHook

- **Version:** `v1.3.3` (commit `9fbd087432700d73fc571118d6a9697a36443d88`)
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Provides the trampoline hook on the engine view-point function the mod reads the clean camera from.
- **Bundled:** yes. Fetched from upstream at configure time and statically linked into `PinballFXHeadTracking.asi`, which ships in the release ZIP.

MinHook carries two copyright holders: Tsuda Kageyu for MinHook itself, and
Vyacheslav Patkov for the Hacker Disassembler Engine that `src/hde/` is built
from. Both notices appear below exactly as upstream ships them.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## cameraunlock-core

- **Version:** commit `371b136584d90163755f0df43e55ed6612952486`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared UDP pose receiver, interpolation, smoothing and hook management.
- **Bundled:** yes. Git submodule at `cameraunlock-core/`, statically linked into `PinballFXHeadTracking.asi`, which ships in the release ZIP.

Our own code, MIT licensed, reproduced here so the notices are complete.

```
MIT License

Copyright (c) 2026 CameraUnlock

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

- **Version:** n/a (wire protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** The mod speaks OpenTrack's UDP pose datagram layout so OpenTrack and compatible trackers can drive it.
- **Bundled:** no. No OpenTrack code, headers or binaries are copied, linked or redistributed.

Because nothing of OpenTrack's is redistributed, its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## Pinball FX footage

- **Files:** `assets/readme-clip.gif`
- **Rights holder:** Zen Studios, together with the rights holders of any
  third-party marks visible in frame.
- **Usage:** recorded from the game running with this mod, captured on a
  legitimately purchased copy, shown so a reader can see what the mod does
  before installing it.
- **Bundled:** kept in this repository only. The packaging scripts ship no part
  of `assets/`, so it is in neither release ZIP nor anything the launcher
  deploys. The README embeds it by absolute URL, so it still resolves for
  someone reading the copy inside a ZIP.
- **Licence:** none is granted or implied by this repository. This material is
  not covered by the MIT licence in `LICENSE`, and nothing here permits reuse
  of it. Rights holders who would rather it were not published: open an issue
  or reach us on Discord and it comes down.

---

## Pinball FX

Pinball FX and all related names, logos, characters and marks are trademarks
of their respective owners. They are used here only to identify the game this
mod applies to, which is nominative use and not a claim of any right in them.
This project is an unofficial, fan-made modification. It is not affiliated
with, endorsed by, or sponsored by the game's developers, its publishers, its
engine vendor, or any other rights holder. It redistributes no game code, no
extracted game assets and no proprietary DLLs, and it requires a legitimately
purchased copy of the game. Any engine structure offsets, function addresses and vtable
slot numbers referenced in the source were derived by the authors through
independent analysis of a legitimately owned copy, and are recorded as the
factual measurements they are. The discovery scripts under `scripts/` search for
generic compiler-emitted call and forwarder shapes, which are properties of
the x86-64 calling convention rather than of the game. No decompiled game
code, no reproduced function bodies and no byte patterns copied out of the
game are stored here; decompiler output stays in a gitignored working
directory and is never committed.
