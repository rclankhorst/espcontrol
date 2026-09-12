<!-- DEV-DOC-STATUS: historical -->

# 4848S040 Battery Tracking Feasibility Investigation

Investigation date: 2026-09-12

> Historical record: this page captures a desk study of whether battery
> statistics can be tracked on the Guition ESP32-S3-4848S040. No physical
> device was available to the investigation, so the hardware-presence question
> at the end is explicitly unresolved. Device YAML under
> `devices/guition-esp32-s3-4848s040/` remains the source of truth for current
> pin assignments.

## Question

Can EspControl report battery level and charging state for the 4848S040, given
how few GPIOs the ESP32-S3 has left once the RGB panel is wired up?

## Outcome

The pin budget is not the blocker, but it does eliminate one of the two
approaches outright:

- **Analogue sense (battery voltage divider into an ADC pin): impossible.**
  There is no pin for it, and no pin can be freed without giving up the
  display or a relay.
- **Digital sense (an I2C battery gauge or PMIC): costs zero extra pins** and
  is the only viable route. Whether it works comes down to a hardware question
  that needs the physical board to answer.

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

## The Unresolved Question

**Does this particular board actually have battery-measurement hardware?**

The 4848S040 is a mains/USB-C powered panel. Multiple community sources
describe a JST LiPo connector on the back of the PCB alongside the speaker and
serial headers, and some report an IP5306 handling charge/boost. Those reports
are inconsistent and several of the higher-ranking search hits are
auto-generated marketing copy rather than hands-on teardowns, so they are not
firm evidence. Board revisions of these Guition panels are known to differ in
which parts are populated.

Three outcomes are possible, and they cannot be distinguished from the repo:

1. **An IP5306 (or similar I2C PMIC) is present and populated.** Battery
   tracking is fully viable — fix the register decoding, wire the sensor into
   the clock bar, done. Note that even in the best case the IP5306 only reports
   in 25% steps, so the clock-bar icon would move in quarters and a percentage
   sensor in Home Assistant would look coarse.
2. **A charger is present but has no I2C telemetry** (a bare TP4056-class part,
   for example). The panel would charge and run from a battery, but there is no
   way to read state of charge: no I2C to query and no ADC pin to sense with.
   Battery tracking would not be possible without hardware modification.
3. **No battery circuitry is populated at all.** The connector may be an
   unpopulated footprint.

### How to settle it

This takes one firmware build and about a minute on the physical device. Add a
scan to the existing bus in
`devices/guition-esp32-s3-4848s040/device/device.yaml`:

```yaml
i2c:
  sda: GPIO19
  scl:
    number: 45
    ignore_strapping_warning: true
  scan: true
```

Then read the boot log. The scan prints every responding address:

- `0x5D` or `0x14` alone - only the GT911 touchscreen answered. No I2C battery
  IC on the bus; outcome 2 or 3 above.
- `0x75` also present - an IP5306 is populated and outcome 1 applies.
- Some other unexpected address - a different gauge or PMIC; identify it before
  writing any driver.

A visual check of the board helps separate outcomes 2 and 3: look for whether
the JST footprint is actually populated with a connector, and whether there is
a charger IC next to it.

## Recommendation

Do not build anything further until the I2C scan result is known. The pin
analysis is settled and needs no device time; the hardware-presence question
needs nothing but device time, and it determines whether there is a feature
here at all.

If the scan does find an IP5306, the follow-up work is well defined: rewrite
the register decoding in the branch's `ip5306.cpp` against the upper
nibble plus registers `0x70`/`0x71`, rebase `feat-ip5306-battery` onto current
`main`, wire the sensor into the clock-bar icon row through
`battery_status_set_icon()`, and gate the icon behind a device capability flag
so panels without the hardware do not show a permanently unknown battery.
