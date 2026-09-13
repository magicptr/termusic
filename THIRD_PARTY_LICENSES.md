# Third-party notices

termusic itself is free software under the **GNU General Public License, version
3 or later** (see [`LICENSE`](LICENSE)). It is built on the components below.
For each one this file records the version actually linked, its license, HOW it
is linked, and what has to accompany a distributed termusic binary.

## Summary

| component | version | license | linkage | notice material |
|---|---|---|---|---|
| FTXUI | 7.0.3 | MIT | **static** -- its code is inside the termusic binary | the MIT notice below MUST accompany the artifact |
| libmpdclient | 2.26 | `BSD-2-Clause` and `BSD-3-Clause` (per-file SPDX headers upstream; **both** notices below) | **static** -- its code is inside the termusic binary | the copyright line and the BSD notices below MUST accompany the artifact |
| kissfft | 131.2.0 | `BSD-3-Clause` | **static** -- its code is inside the termusic binary | the copyright line and the BSD-3-Clause notice below MUST accompany the artifact |
| C++ / C runtime (`libstdc++`, `libgcc_s`, `libm`, `libc`) | the build toolchain's | the compiler runtime's own licenses | dynamic | system runtime, not redistributed |

**Evidence for the linkage column** (from this build):

```
$ readelf -d build/termusic | grep NEEDED
        libstdc++.so.6, libm.so.6, libmvec.so.1, libgcc_s.so.1, libc.so.6
        (the platform runtime only -- no ftxui, no libmpdclient, no libfftw3f)
$ ldd build/termusic | grep -c -e ftxui -e mpdclient -e fftw
        0
$ nm -C build/termusic | grep -c 'ftxui::'
        2741
$ nm build/termusic | grep -c ' T mpd_\| T kiss_fftr'
        392
```

FTXUI, libmpdclient and kissfft are **statically linked**: their code is part of
the executable in this archive, and nothing else from them is needed at run
time. MIT and the BSD licenses both require their copyright and permission
notices to be included in copies or substantial portions of the software, which
is why those texts are reproduced in full below. FFTW was used until the
portable-build phase replaced it with kissfft; it is no longer built, linked or
distributed in any form, so its notice is not reproduced.

## FTXUI 7.0.3 -- MIT

```
The MIT License

Copyright (c) 2019 Arthur Sonzogni.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## libmpdclient 2.26 -- BSD-2-Clause and BSD-3-Clause

libmpdclient is the MPD client library termusic talks to MPD through. It is not
taken from a distribution package any more: the build fetches the pinned
upstream release, builds it with its own (Meson) build system and links the
static library into the executable, so its code is part of every termusic
binary.

* upstream: https://github.com/MusicPlayerDaemon/libmpdclient (tag `v2.26`)
* copyright: **Copyright The Music Player Daemon Project** (the line every
  source file carries; upstream's `LICENSES/` files use the standard
  `<year> <owner>` placeholder, which is why the holder is named here)
* license: the project is released under the revised BSD License; its files mix
  `SPDX-License-Identifier: BSD-2-Clause` and `BSD-3-Clause`, so both notices
  are reproduced.

BSD-2-Clause:

```
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

BSD-3-Clause:

```
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## kissfft 131.2.0 -- BSD-3-Clause

kissfft performs the 2048-point real FFT behind the visualizer's spectrum. It
is compiled into the termusic binary, so its notice must accompany the
artifact.

* upstream: https://github.com/mborgerding/kissfft (tag `131.2.0`)
* built as: static library, single precision (`kiss_fftr`), no upstream tools,
  tests or pkg-config file

```
Copyright (c) 2003-2010 Mark Borgerding . All rights reserved.

KISS FFT is provided under:

  SPDX-License-Identifier: BSD-3-Clause

Being under the terms of the BSD 3-clause "New" or "Revised" License,
according with:

  LICENSES/BSD-3-Clause
```

## Where the sources come from

| component | upstream |
|---|---|
| FTXUI | https://github.com/ArthurSonzogni/FTXUI (version 7.0.3, taken from a system package or fetched from this upstream repository at build time) |
| libmpdclient | https://github.com/MusicPlayerDaemon/libmpdclient (version 2.26, fetched from this upstream repository at build time and linked statically) |
| kissfft | https://github.com/mborgerding/kissfft (version 131.2.0, fetched from this upstream repository at build time and linked statically) |

termusic's complete corresponding source is the project this archive is built
from, and every fetched dependency is identified above by version and upstream
repository, so the exact source of everything linked into the binary is
reproducible from those public repositories. The licenses of the three
statically linked libraries (MIT for FTXUI, BSD for libmpdclient and kissfft)
require their notices to travel with the binary, which is what this file does;
they do not require termusic to provide their sources.
