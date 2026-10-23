# Third-party notices

Code from others that is compiled into the Rattlebox firmware, with its license. This is a
live document: a change that adds, removes or upgrades such a component updates this file
in the same commit.

Rattlebox does not publish firmware binaries yet. Several of these licenses attach
conditions to binary distribution, listed in the last column. A release must meet them
before it ships, and `LEDGER.md` tracks that work.

| Component | Version | Where | License | On binary distribution |
|---|---|---|---|---|
| Bosch Sensortec BMI270 config file | Sensor API v2.113.0 | `src/hal/boards/cardputer/bmi270_config.h` (Cardputer build only) | BSD-3-Clause | Reproduce the notice below in the documentation |
| LovyanGFX | 1.2.19 | PlatformIO library | FreeBSD (BSD-2-Clause) for LovyanGFX, plus MIT and BSD notices for the Adafruit and TFT_eSPI code it derives from (its `license.txt`) | Reproduce each notice in the documentation |
| Arduino core for ESP32 | 3.3.7 | PlatformIO framework | LGPL-2.1-or-later | Let the recipient relink the firmware against a modified core, for example by publishing the source |
| ESP-IDF (inside the Arduino core) | 5.5.0 | PlatformIO framework libraries | Apache-2.0, with components under their own licenses | Include the license and any NOTICE files |

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
