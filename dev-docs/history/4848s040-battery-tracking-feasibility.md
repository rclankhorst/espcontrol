<!-- DEV-DOC-STATUS: historical -->

# 4848S040 Battery Tracking Feasibility Investigation

Investigation date: 2026-09-12

> Historical record: this page captures a desk study of whether battery
> statistics can be tracked on the Guition ESP32-S3-4848S040, plus the on-device
> I2C scan that settled it on 2026-09-13. Device YAML under
> `devices/guition-esp32-s3-4848s040/` remains the source of truth for current
> pin assignments.

## Question

Can EspControl report battery level and charging state for the 4848S040, given
how few GPIOs the ESP32-S3 has left once the RGB panel is wired up?

## Outcome

**Not possible on this board without a hardware modification.** Both routes are
closed, for different reasons:

- **Analogue sense (battery voltage divider into an ADC pin): impossible.**
  There is no pin for it, and no pin can be freed without giving up the
  display or a relay. This follows from the pin map alone.
- **Digital sense (an I2C battery gauge or PMIC): costs zero extra pins**, and
  was the only viable route — but an on-device I2C scan on 2026-09-13 found
  nothing on the bus except the touchscreen. There is no battery IC to read.

The pin budget alone did not settle the question; it only narrowed the feature
to an I2C implementation. What closed it was the scan finding no I2C battery
hardware. See "Scan Result" below.

## Pin Budget

Derived from `components/mipi_rgb/models/guition.py` (the `GUITION-4848S040`
model) and `devices/guition-esp32-s3-4848s040/device/device.yaml`.

The ESP32-S3 exposes 45 usable GPIOs (`GPIO0-21` and `GPIO26-48`; `GPIO22-25`
do not exist on this part). On this board 43 of the 45 are already committed:

| Function | GPIOs |
| --- | --- |
| RGB data - red | 0, 11, 12, 13, 14 |
| RGB data - green | 3, 8, 9, 10, 20, 46 |
| RGB data - blue | 4, 5, 6, 7, 15 |
| RGB sync - HSYNC / VSYNC / DE / PCLK | 16, 17, 18, 21 |
| LCD chip select | 39 |
| SPI for the ST7701S init sequence | 47 (MOSI), 48 (CLK) |
| I2C bus - GT911 touchscreen | 19 (SDA), 45 (SCL) |
| Backlight LEDC PWM | 38 |
| Built-in relays 1 / 2 / 3 | 40, 2, 1 |
| UART0 logger (`hardware_uart: UART0`) | 43, 44 |
| SPI flash (SoC-reserved, 16MB) | 26-32 |
| Octal PSRAM (SoC-reserved, `psram.mode: octal`) | 33-37 |

**Free: `GPIO41` and `GPIO42` only.**

### Why that rules out ADC battery sensing

The ESP32-S3 has two ADC blocks, and only one of them is usable here:

- **ADC1 = `GPIO1-GPIO10`.** This is the only ADC that works while WiFi is
  active. All ten channels are taken: `GPIO1`/`GPIO2` by relays 3 and 2, and
  `GPIO3-GPIO10` by RGB data lines.
- **ADC2 = `GPIO11-GPIO20`.** All ten are taken by RGB data, sync and I2C — and
  ADC2 is unavailable whenever the WiFi radio is on regardless, which for this
  firmware is always.

The two free pins, `GPIO41` and `GPIO42`, are digital-only (JTAG MTDI/MTCK);
neither is routed to an ADC channel on the S3. So a voltage divider has
nowhere to land. Freeing an ADC1 pin would mean dropping a relay (`GPIO1`,
`GPIO2`) or a colour bit, and neither is an acceptable trade.

### Why I2C sidesteps the problem entirely

The board already runs an I2C bus on `GPIO19`/`GPIO45` for the GT911
touchscreen. Any I2C battery gauge or PMIC hangs off that same pair and
consumes **no additional GPIOs**. The GT911 sits at `0x5D`/`0x14`, so there is
no address collision with the usual battery ICs (the IP5306 is at `0x75`).

This is the important conclusion: *the pin shortage does not block battery
tracking.* It only forces the implementation to be I2C-based.

## Prior Art in This Fork

Branch `feat-ip5306-battery` (commit `6fa70212`, 2026-07-01) already carries a
first attempt:

- An ip5306 component directory under components/ (branch only) - an I2C
  `PollingComponent` exposing a battery-level sensor and a charging binary
  sensor.
- A battery.yaml addon under common/addon/ (branch only) - a reusable addon
  wiring both Home Assistant entities, with the interval driven by a
  `battery_update_interval` substitution.
- The 4848S040 `device.yaml` adds `ip5306` to `external_components`, and
  `packages.yaml` includes the addon.

The shape of that work is right — I2C, no new pins, addon-per-device. The
register decoding in it is not.

### Register decoding bug in the existing branch

The branch's `ip5306.cpp` reads register `0x78` and interprets the
**low** bits:

```cpp
bool charge_full = (data >> 4) & 0x01;
bool charging    = (data >> 3) & 0x01;
uint8_t leds     = (data >> 1) & 0x03;   // 0/25/50/75 %
```

The IP5306 does not lay the register out that way. On real parts, `0x78`
(`REG_READ4`) encodes the LED bar count in the **upper nibble**, and the
mapping is inverted rather than linear:

| `data & 0xF0` | Battery level |
| --- | --- |
| `0x00` | 100% |
| `0x80` | 75% |
| `0xC0` | 50% |
| `0xE0` | 25% |
| `0xF0` | 0% |

Charging state is not in `0x78` at all — it lives in `REG_READ0` (`0x70`,
bit 3 = charging in progress) and `REG_READ1` (`0x71`, bit 3 = charge
complete).

As written, the driver would report a plausible-looking but incorrect
percentage and a meaningless charging flag. Any revival of this branch needs
the decode rewritten against the upper nibble and the two extra registers
before it is worth testing on hardware.

## UI Plumbing Already Present Upstream

`components/espcontrol/battery_status.h` is already on `main`. It maps a
percentage to an MDI battery glyph across ten tiers, with distinct
unknown/alert icons, and exposes `battery_status_set_icon()`. The MDI battery
glyphs are registered in `components/espcontrol/icons.h`.

It is currently scaffolding: `button_grid.h` includes the header, but
`battery_status_set_icon()` has no callers anywhere in the tree. The clock-bar
right-side icon row (`components/espcontrol/clock_bar.h`) already describes
battery as one of its intended icons and packs icons leftwards by glyph edge,
so a hidden battery icon costs no layout space.

So the display side is roughly half-built and waiting for a data source. A
working sensor would need wiring into that icon row plus, if the icon should
be user-visible per device, a capability flag in
`product/v2/devices/guition-esp32-s3-4848s040.json`.

## Scan Result (2026-09-13)

The board was flashed with `scan: true` added to the existing I2C bus and the
boot log read. The complete result:

```
[C][i2c.idf:092]: I2C Bus:
[C][i2c.idf:093]:   SDA Pin: GPIO19
[C][i2c.idf:093]:   SCL Pin: GPIO45
[C][i2c.idf:093]:   Frequency: 50000 Hz
[C][i2c.idf:103]:   Recovery: bus successfully recovered
[C][i2c.idf:113]: Results from bus scan:
[C][i2c.idf:119]: Found device at address 0x5D
```

`0x5D` is the GT911 touchscreen. It is the only device that answered. There is
no IP5306 at `0x75`, and no other gauge or PMIC at any address.

The bus itself is healthy — it enumerated and recovered cleanly, and the
touchscreen responded — so this is a real absence rather than a failed scan.
The 50 kHz bus clock is well below the IP5306's supported range and would not
prevent it from acknowledging.

### What this means

Battery level and charging state cannot be reported on this board as it ships.
The panel may still have a JST LiPo footprint and a charge/boost part, and may
well run from a battery; what it does not have is any way to *tell the ESP32*
what the battery is doing. There is no I2C telemetry to query and no free ADC
pin to sense with.

One caveat, noted for completeness rather than as a live hope: the scan was run
on USB power. An IP5306 sourcing VIN from USB should enumerate with or without
a cell attached, so a re-scan with a battery plugged in is very unlikely to
change the result. It is a cheap check if certainty matters.

The `feat-ip5306-battery` branch is therefore moot on this hardware. Its
register-decoding bug (documented above) was never the reason it did not work —
there was no chip for it to talk to.

## If the Feature Is Still Wanted

It would take a hardware modification. The pin analysis points to what is and
is not worth attempting:

- **Viable: add an I2C fuel gauge.** A MAX17048, LC709203F or similar wired to
  the battery rail and to the existing `GPIO19`/`GPIO45` bus needs **no free
  GPIOs** and would give a genuine state-of-charge reading — better resolution
  than the IP5306's 25% steps would have offered. An I2C ADC such as an ADS1115
  reading a divider on the battery rail works the same way. Both require
  soldering to the bus and the battery rail, and neither is something the
  project can ship as a firmware change.
- **Not viable: anything needing an ESP32 pin.** A divider into an ADC input
  has nowhere to land, as established in the pin budget above. This does not
  become possible with a different firmware or a cleverer configuration.

Any such modification is per-unit and would need gating behind a device
capability flag so stock panels do not show a permanently unknown battery.

## Recommendation

Close the feature for the stock 4848S040. Do not rebase or fix
`feat-ip5306-battery` — the hardware it targets is not on this board, so the
work has no destination.

`components/espcontrol/battery_status.h` on `main` stays as it is: harmless
scaffolding, with no callers, that a future device with real battery hardware
could use. Nothing needs to be removed.
