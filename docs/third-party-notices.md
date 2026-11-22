# Third-party notices

Code from others that is compiled into the Rattlebox firmware, with its license. This is a
live document: a change that adds, removes or upgrades such a component updates this file
in the same commit.

Several of these licenses attach conditions to binary distribution, listed in the last column.
Each GitHub release meets them through `NOTICES.md`, which `scripts/release.mjs` writes into the
release from this file, `LICENSE`, LovyanGFX's own `license.txt` and the upstream license texts
vendored in [`licenses/`](licenses/), and which offers the tagged source the release was built
from (ADR-0021).

| Component | Version | Where | License | On binary distribution |
|---|---|---|---|---|
| Bosch Sensortec BMI270 config file | Sensor API v2.113.0 | `src/hal/boards/cardputer/bmi270_config.h` (Cardputer build only) | BSD-3-Clause | Reproduce the notice below in the documentation |
| M5Unified speaker-enable register values | 0.2.27 | `src/hal/boards/cardputer/es8311.cpp` (Cardputer build only) | MIT | Include the notice below with the firmware |
| LovyanGFX | 1.2.19 | PlatformIO library | FreeBSD (BSD-2-Clause) for LovyanGFX, plus MIT and BSD notices for the Adafruit and TFT_eSPI code it derives from (its `license.txt`) | Reproduce each notice in the documentation |
| Arduino core for ESP32 | 3.3.7 | PlatformIO framework | LGPL-2.1-or-later (text in `licenses/`) | Let the recipient relink the firmware against a modified core, for example by publishing the source |
| ESP-IDF (inside the Arduino core) | 5.5.2-729 (`release/v5.5` at `87912cd291`) | PlatformIO framework libraries | Apache-2.0, with components under their own licenses, listed in its `COPYRIGHT.rst` | Include the license and any NOTICE files; ESP-IDF has no NOTICE file, and its license and component list are in `licenses/` |

Code copied from Dragon Radar (ADR-0001) has the same author and is not third-party. Rattlebox's
own code, that copied code included, is under the MIT license in `LICENSE`, the license Dragon
Radar uses, so the copied code keeps the license it came with.

## Bosch Sensortec BMI270 config file

The BMI270 does not measure until this blob is uploaded to it at every boot. It is
extracted verbatim from `bmi270.c` in
[BMI270_SensorAPI](https://github.com/boschsensortec/BMI270_SensorAPI) (release v2.113.0,
commit `41129fcfe39c583ee5462d79195741945d51c1fe`), and also carries this notice in its
header.

```text
Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved.

BSD-3-Clause

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

## M5Unified speaker-enable register values

The ES8311 codec stays silent until these eight register writes power its DAC. They are the values
of `_speaker_enabled_cb_cardputer_adv` in `src/M5Unified.inl` of
[M5Unified](https://github.com/m5stack/M5Unified) (version 0.2.27, read on 2026-10-24), the same
values Dragon Radar copied (esp32-dragon-radar:ADR-0004). M5Unified is under this license:

```text
MIT License

Copyright (c) 2021 M5Stack

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
