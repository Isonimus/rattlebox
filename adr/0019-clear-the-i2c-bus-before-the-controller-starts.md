---
id: '0019'
title: "Each board clears its I2C bus with nine clocks and a STOP before the controller starts"
type: architecture
status: accepted
date: 2026-11-22
supersedes: []
superseded_by: []
---

## Context

The StickC Plus 2 failed to boot twice with its IMU unreachable. On 2026-10-23 a handheld boot
failed `imu FAILED: no ACK at 0x68`, ESP-IDF error 259; it did not recur in over 300 desk resets
or two handheld runs. On 2026-11-22 the first reset `verify:roll` made right after a flash printed
`i2c_master_transmit_receive failed: [259] ESP_ERR_INVALID_STATE` at 308 ms and no `[boot] imu OK`;
the next reset booted and rolled 100 times. The cause was unknown (slice 0003's ledger line).

Three facts put the cause on the bus:

- **A reset lands mid-read.** The product reads the IMU before every physics step and between the
  display's pushes, up to 1.3 ms a read on the StickC, so a reset at a random moment often stops
  the ESP32 mid-transfer.
- **The reset does not reach the IMU.** EN resets the ESP32 only. The MPU6886 stays powered and
  waits for the rest of its transfer, sending its byte at the next clocks.
- **Nothing clears the bus at boot.** In ESP-IDF 5.5.1's `libesp_driver_i2c.a`, the GPIO bus clear
  (`gpio_set_level`, `gpio_get_level`, `esp_rom_delay_us`) lives only in `s_i2c_hw_fsm_reset`.
  `i2c_new_master_bus`, which `Wire.begin` calls, never calls it. Only the transaction functions do,
  once a transfer has already failed, and `i2c_master_bus_reset`. So the boot's first transfer
  runs into the chip, and its failure is what frees the bus, as the next boot showed.

  > **Correction, 2026-11-22:** the ESP-IDF inside Arduino core 3.3.7 is 5.5.2-729 (`IDF_VER`,
  > and `release/v5.5` at `87912cd291` in the libraries' `versions.txt`), not 5.5.1. The
  > disassembly was of that build's `libesp_driver_i2c.a`, so the finding stands (ADR-0020).

A probe (`src/probes/i2c_clear/`, env `m5stickc-plus2-i2c-clear-probe`) put the bus in that state
on purpose. It bit-banged a read of WHO_AM_I (0x19, `00011001`) and let go of the bus 0 to 8 clocks
into the data byte, as the ESP32's pins float at reset. It then ran the product's `Mpu6886::begin`,
five times at each stopping point, each followed by a control start on an idle bus:

| Clocks into the byte | 0 (ack) | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| SDA held low when released | yes | yes | yes | yes | no | no | yes | yes | no |
| Starts failed, before this decision | 0/5 | 0/5 | 0/5 | 0/5 | 0/5 | **5/5** | 0/5 | 0/5 | 0/5 |
| Starts failed, after it | 0/5 | 0/5 | 0/5 | 0/5 | 0/5 | 0/5 | 0/5 | 0/5 | 0/5 |

All 90 control starts passed. The failing start printed the field failure's exact lines: `[259]
ESP_ERR_INVALID_STATE` and `no ACK at 0x68`. Only some stopping points break the start, which fits
a failure that hundreds of resets did not show. The one that does is a bit that holds nothing: the
chip was sending a 1, with two 0 bits to come into the controller's address byte.

## Decision

Before `Wire.begin`, each board's bus is cleared through its pins as open-drain GPIOs
(`src/hal/i2c_bus_clear.h`, `src/hal/gpio_i2c_lines.h`). The clear follows the I2C specification's
bus clear (UM10204, section 3.1.16):

1. Release both lines; SCL held low then fails the start loudly, since clocks cannot free it.
2. Send nine clocks with SDA released, every time, not only while SDA is low. Stopping once SDA
   rose would leave a chip like the probe's at point 5 sending; with SDA released, every device
   cut off mid-read reaches its acknowledge slot, reads a NACK and stops sending.
3. Send a STOP, and send it once more if a device acknowledges in the STOP's own first clock. A
   device cut off while being written to, one bit before its acknowledge slot, is back there after
   nine clocks; that clock ends the slot, so the second STOP takes.
4. Either line still low fails the start with the reason.

The StickC's MPU6886 driver runs it before its `Wire.begin`. The Cardputer's shared bus runs it in
`cardputer_i2c::begin`, which now returns why it failed instead of `false`.

Rejected:

- **`i2c_master_bus_reset` after `Wire.begin`.** It needs Arduino's internal bus handle, and the
  compiler has rewritten `s_i2c_hw_fsm_reset`'s arguments, so the binary does not show that its
  bus clear runs on that path. It also reports nothing, and cannot be tested on the host.
- **Retrying the IMU's start when it fails.** It hides the bus state rather than ending it, and a
  bus that never frees would retry forever or give up, with no reason either way.

## Consequences

- `test/test_i2c_bus_clear` runs the clear against a device modelled bit by bit, cut off at every
  bit of every byte value while sending and at every bit while receiving, with SCL high or low at
  the reset. The device must end idle with no START sent; a stuck line must fail. The receiving
  sweep failed until the second STOP was added.
- After the change, each board booted `[boot] imu OK` on 30 of 30 resets at random moments of the
  running product, and `verify:roll` passed on each: 100 rolls locked and both types passed
  chi-square. Every one of those clears, and the 90 in the probe's run after it on the StickC, read both lines high
  half a 100 kHz clock after releasing them, so the boards' pull-ups raise the lines within the
  5 µs the clear allows.
- The Cardputer had its own unexplained boot: one of 40 under ADR-0005 showed no `[boot] imu OK`
  within 5 s, its log not kept. Its bus is started the same way, so a reset mid-read fits. With the
  clear, it booted `[boot] imu OK` on 100 more resets, 130 of 130 in all; at the old rate of one in
  40, a run that clean comes by chance about 4% of the time, so the ledger's audit is closed here.
- Writes can be cut off too: clocking a device being written to with SDA released feeds it 1 bits,
  and can finish a register write with them. Every boot configures each device after the clear
  (the MPU6886 is reset first), so such a write does not outlive the boot.
- Any new I2C bus, on either board, runs the clear before its controller starts (`CLAUDE.md` §4,
  invariant #5).
