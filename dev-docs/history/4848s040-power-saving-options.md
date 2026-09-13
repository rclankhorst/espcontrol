<!-- DEV-DOC-STATUS: historical -->

# 4848S040 Power and Battery Saving Options Investigation

Investigation date: 2026-09-13

> Historical record: this page captures a desk study of power-reduction options
> for EspControl, written from the repository and from Espressif/ESPHome source
> at the versions pinned on this date. No physical device, power supply or
> current meter was available, so every figure below is derived or estimated and
> none of it is a measurement. Device YAML under `devices/` and the shared
> addons under `common/addon/` remain the source of truth for current behaviour.

## Question

What can EspControl do to reduce power draw, how much would each option
actually save, and how would a user turn each one on and off?

The Guition ESP32-S3-4848S040 is the device of interest because it is the only
supported panel that might plausibly run from a LiPo. The other supported
panels are ESP32-P4 wall units that are mains powered by construction, so for
them the same levers are about heat and idle draw, not run time.

"Runs on battery" is still a hypothesis. The companion battery-tracking
feasibility record (on branch `claude/battery-tracking-4848s040-sxh96t`) could
not establish that the 4848S040 has battery hardware at all without the
physical board. Nothing below assumes it does.

## Outcome

The honest headline is that **an RGB panel with a continuously scanning
framebuffer and WiFi associated is not a low-power device, and no configuration
change makes it one.** The floor is set by hardware that cannot be turned off
while the product is doing its job.

Within that floor, three levers are real and one of them is already shipped:

| Lever | Verdict | Rough saving | Main risk |
| --- | --- | --- | --- |
| Backlight duty / screensaver / schedule | **Already implemented**, and the largest lever by a wide margin | Dominant; scales roughly linearly with duty | None new; already tuned |
| WiFi `power_save_mode: light` | **Plausible**, best untapped lever | Largest remaining single win when idle | Downlink latency rises to one DTIM interval; reconnect and OTA behaviour need retesting |
| Static CPU downclock to 160 MHz | **Plausible but risky**, needs device testing | Modest | Directly touches the RGB bounce-buffer bandwidth margin that `device.yaml` already documents as fragile |
| ESP-IDF power management (`CONFIG_PM_*`, DFS) | **Dead end as configured** | None available | The CPU would drop to 80 MHz while the panel scans, with no driver lock to prevent it |
| Automatic light sleep | **Hard blocked** | n/a | The RGB panel driver holds a no-light-sleep lock for its entire lifetime |
| Deep sleep for power saving | **Architecturally incompatible** | n/a | No wake source, and waking costs a full cold boot |
| Touch poll interval, timer tick load, cover art | **Marginal** | Small | Responsiveness regressions for very little gain |

## What Already Exists

A large amount of what would be called "battery saving" on another product is
already shipped here under display names. Before proposing anything new, this
is the inventory.

### The display lifecycle controller

`dev-docs/display-lifecycle.md` describes a single controller that resolves one
presentation mode from a priority list. The modes that reduce power are
`DIMMED`, `CLOCK`, `SETUP_DIMMED` and `DISPLAY_OFF`. The relevant wiring lives
in `common/addon/backlight.yaml` and `common/addon/backlight_schedule.yaml`.

What a user can already configure today, all persisted across reboots:

- **Screensaver timeout** - `screensaver_timeout`, 10 to 3600 seconds, default
  1800, in `common/config/display.yaml`.
- **Screensaver action** - `screensaver_action`, one of Display Off, Screen
  Dimmed or Clock, default Display Off.
- **Presence-driven sleep** - an optional Home Assistant presence entity that
  puts the panel to sleep on absence and wakes it on presence.
- **Night schedule** - `schedule_enabled` plus `schedule_mode`, one of Screen
  off, Screen Dimmed, Always On or Clock, with its own brightness values and a
  temporary wake timer for off-hours touches.
- **Day/night and sun-driven brightness** - `brightness_mode` with separate day,
  night, dimmed, clock and scheduled-clock brightness numbers.
- **Manual sleep** - a long press turns the panel off until the next touch.
- **Media-driven behaviour** - cover art takeover, and an option to keep the
  panel awake while a media player is playing.

### What DISPLAY_OFF actually turns off

This matters for estimating what is left to save. In `backlight.yaml`, the
`backlight_pause_display_off` script does three things: it forces the LEDC
backlight output off, writes zero to the PWM, and then calls `lvgl.pause`.

So in the off state the S3 already stops the backlight **and** stops LVGL
rendering. The two P4 panels `guition-esp32-p4-jc8012p4a1` and
`guition-esp32-p4-jc8012p4a1-v2` opt out of the LVGL pause with the
`ESPCONTROL_KEEP_LVGL_ACTIVE_ON_DISPLAY_OFF=1` build flag (documented in
`dev-docs/devices-and-builds.md`), so on those two the LVGL saving is
deliberately given up to keep wake paths reliable. The S3 takes the saving.

### What DISPLAY_OFF does not turn off

- **The RGB panel keeps scanning.** `components/mipi_rgb/mipi_rgb.cpp` sets
  `fb_in_psram = 1` with `num_fbs = 1` and a bounce buffer of 20 rows. The
  panel's DMA reads the framebuffer out of PSRAM continuously, at the pixel
  clock, regardless of backlight state and regardless of whether LVGL is
  paused. At the configured 10 MHz pixel clock and two bytes per pixel that is
  roughly **20 MB/s of PSRAM reads that never stop**. The vendored driver has
  no sleep, standby or panel-off path at all.
- **WiFi stays associated** with power saving explicitly disabled.
- **Touch keeps polling** at 16 ms.
- **The periodic scripts keep running.** Across `common/`, with the display
  off, the panel still services timers at 250 ms (image card refresh), 500 ms
  (clock-bar refresh, and the schedule's force-off guard), 1 s (display mode
  reconcile, clock keep-on-top, cover-art progress, and the artwork endpoint
  status text sensor), 5 s, 30 s, 60 s and 1 h.

### Other things already in place

- `common/addon/memory_diagnostics.yaml` and `common/addon/network.yaml` use
  60 s update intervals rather than the ESPHome defaults.
- The automatic firmware update check in `common/addon/firmware_update.yaml`
  runs on a 1 h tick and returns immediately unless the auto-update switch is
  on and the chosen frequency is due.
- The S3 profile disables the weather forecast card and uses low-heap media and
  cover-art paths via build flags, which cuts both memory and network work.

## Why `power_save_mode: none` Is There

Both `common/addon/connectivity.yaml` and
`common/addon/connectivity_deployed.yaml` set `power_save_mode: none`.

Git history gives the reason by context rather than by comment. The setting was
introduced by commit `209f2f4c8` ("Improve WiFi reconnect resilience",
2026-05-10) in the same hunk that changed `reboot_timeout` from `0s` to `30min`
and `ap_timeout` from `0s` to `90s`, added the delayed
`wifi_reconnect_flow` script, and set `sdio_frequency: 20MHz` on the P4 hosted
radios. The user-facing docs changed in the same commit are all about
reconnection reliability and how long the setup hotspot takes to appear. The
deployed variant simply inherited the value when it was split out in
`084b6fc90`.

So it is a **stability setting adopted during a reconnection-reliability pass**,
not a latency decision that was measured against `light` and `high`. There is no
comment in either file explaining it and no benchmark in the repository behind
it. That is worth stating plainly: the current value is defensible but it was
never compared against the alternatives.

There is a second, real reason to keep it on a mains-powered wall panel. In
ESP-IDF the WiFi driver holds the `ESP_PM_APB_FREQ_MAX` power-management lock
between `esp_wifi_start` and `esp_wifi_stop`, and only releases it during
modem-sleep windows. With power saving off, the bus clock is pinned, which is
one fewer variable in a display stack that is already sensitive to PSRAM
timing.

### What `light` and `high` would actually do

On ESP-IDF, ESPHome maps the three values to `esp_wifi_set_ps`:

- `none` maps to `WIFI_PS_NONE` - the receiver runs continuously.
- `light` maps to `WIFI_PS_MIN_MODEM` - the radio sleeps between DTIM beacons
  and wakes for each one.
- `high` maps to `WIFI_PS_MAX_MODEM` - the radio also skips beacons according to
  a listen interval.

`light` is the interesting one. Going from a continuously powered receiver to
one that wakes only at the access point's DTIM period is the **largest
remaining single saving available on this board** once the backlight is off,
because with the screen dark the radio is the biggest thing still running at
full rate. The cost is bounded and predictable: a downlink packet can wait up to
one DTIM interval, typically 100-300 ms on a domestic access point. For a wall
panel that means a Home Assistant state change can take up to a few hundred
milliseconds longer to appear. A button press on the panel is uplink and is
unaffected.

`high` adds latency proportional to the listen interval for a much smaller
additional saving, and is a poor trade for an interactive panel. It should not
be offered.

**These numbers are not measured.** No current measurement was possible in this
investigation. The claim is about the shape of the saving, not its magnitude.

## Candidate Levers in Detail

### 1. Backlight duty - the dominant lever, already exposed

On a 4 inch IPS panel the LED backlight is normally the largest single consumer
in the whole system, ahead of the SoC and the radio. The firmware drives it
through an LEDC PWM output on GPIO38 at 150 Hz with `gamma_correct: 1.0`, so
the light entity's brightness maps directly to duty cycle and average LED
current scales with it more or less linearly.

That means the existing settings are not a nice-to-have. They are the power
policy:

- Dropping normal brightness from 100% to 50% roughly halves the dominant load
  while the panel is awake.
- Shortening the screensaver timeout, or using presence, moves the panel into
  the off state sooner.
- The night schedule removes the backlight entirely for the hours the panel is
  not looked at.

**Nothing new is needed here.** If the goal is battery run time, the first
recommendation to a user is to turn existing settings down, not to ship a new
feature. Any "power saving" mode that got added should be described honestly as
a convenience preset over controls that already exist.

The one genuine gap: there is no single control that says "be frugal". A user
who wants that today has to find and adjust five separate settings across two
web-configurator sections.

### 2. WiFi power save mode - the best untapped lever

Covered above. `light` is worth trying; `high` is not.

The implementation question is whether it should be fixed at build time or
switchable at runtime, and this is where ESPHome's current API is awkward.

ESPHome 2026.8.2 does have runtime control, but it is one-directional.
`WiFiComponent::request_high_performance()` and `release_high_performance()`
exist behind the `USE_WIFI_RUNTIME_POWER_SAVE` define, which a component opts
into by calling `wifi.enable_runtime_power_save_control()` during code
generation. The vendored `components/sendspin/__init__.py` already does exactly
this for audio streaming. But `request_high_performance()` only moves the radio
*towards* `NONE`, and `release_high_performance()` only restores the mode
configured in YAML. There is no public call that applies a *lower* power mode at
runtime: `set_power_save_mode()` writes the member variable but does not apply
it, and the function that applies it is private.

Two workable shapes follow from that:

- **Build-time baseline of `light`, with high-performance requests around
  latency-critical work.** This is the idiomatic ESPHome shape and matches what
  sendspin already does. A YAML change plus `request_high_performance()` around
  OTA and cover-art downloads.
- **A direct `esp_wifi_set_ps()` call from a lambda.** Simple and immediate, but
  see the footgun below - ESPHome re-applies its own configured mode on WiFi
  station start, so a runtime override is silently reverted on every reconnect
  unless it is re-applied from the `wifi.on_connect` trigger.

### 3. CPU frequency - a correction and a lever

**Correction to a common assumption:** the S3 does not run at 160 MHz. ESPHome's
esp32 component defaults `cpu_frequency` to the **maximum** supported frequency
for the variant when the key is absent, which for the ESP32-S3 is 240 MHz. So
the S3 is already at its top clock, and the P4 devices' explicit
`cpu_frequency: 360MHZ` is the same value they would get by default on an
engineering sample. The P4 line is documentation, not a performance uplift.

That makes a **static** downclock to `cpu_frequency: 160MHZ` a real, testable
option on the S3. It is a one-line change, it has no runtime complexity, and
CPU core power scales strongly with clock frequency.

The risk is specific and serious, and `devices/guition-esp32-s3-4848s040/device/device.yaml`
already documents it from the other direction. The panel is driven at a
deliberately conservative 10 MHz pixel clock because "higher clocks have caused
screen shifts and pixel breakup when WiFi, flash, and web traffic compete for
PSRAM". The 20-row bounce buffer exists because "album-art redraws are
full-screen and PSRAM-heavy" and the DMA needs headroom. Both comments describe
the same fragile margin: the CPU must refill the bounce buffer from PSRAM fast
enough to stay ahead of the panel's scan. Cutting the clock by a third cuts into
exactly that margin, and the same file records that the visible failure mode is
a vertically shifted or broken display rather than a clean error.

It is also worth noting that this build sets `CONFIG_SPIRAM_FETCH_INSTRUCTIONS`
and `CONFIG_SPIRAM_RODATA`, so code and constants are fetched from PSRAM
through the cache. The CPU already stalls on PSRAM more than a typical build,
which makes the clock reduction less linear in real work than the raw number
suggests.

Verdict: plausible, cheap to try, but it must be validated on hardware with a
full-screen cover-art redraw while WiFi is busy, and it should not ship without
that. This is a device-test item, not a desk decision.

### 4. ESP-IDF power management and DFS - a dead end as configured

This one looks attractive and is not. The evidence is in ESP-IDF itself.

On the ESP32-S3, with `CONFIG_PM_ENABLE` on and a max CPU frequency of 240 MHz,
Espressif's own DFS table says that when the APB lock is held but the CPU lock
is not, the chip runs **CPU at 80 MHz, APB at 80 MHz**. WiFi holds the APB lock
continuously while the radio is started. So enabling DFS on this firmware puts
the CPU at 80 MHz for most of its life.

Now look at who holds a CPU-frequency lock. In ESP-IDF's RGB panel driver
(`esp_lcd_panel_rgb.c`, release/v5.5), the lock the driver creates is:

```c
#if CONFIG_PM_ENABLE
    // clock sources like PLL and XTAL will be turned off in light sleep, so basically a NO_LIGHT_SLEEP lock is sufficient
    esp_pm_lock_type_t lock_type = ESP_PM_NO_LIGHT_SLEEP;
#if CONFIG_IDF_TARGET_ESP32P4
    // use CPU_MAX lock to ensure PSRAM bandwidth and usability during DFS
    lock_type = ESP_PM_CPU_FREQ_MAX;
#endif
```

On the P4, Espressif explicitly upgrades the lock to `ESP_PM_CPU_FREQ_MAX`
because PSRAM bandwidth during DFS is not sufficient otherwise. On the S3 they
did not. So on this board the RGB driver would allow the CPU to fall to 80 MHz
while the panel is scanning, with nothing holding it up, in a firmware whose own
comments say the PSRAM bandwidth margin is already tight at 240 MHz.

That is a much worse version of the static-downclock risk, and it is dynamic, so
the failure would be intermittent and correlated with load - the hardest kind to
diagnose. `CONFIG_PM_ENABLE` also adds interrupt latency and code size across
the whole build.

**Recommendation: do not enable `CONFIG_PM_*` on the S3.** If CPU frequency is
to be reduced, do it statically where the behaviour is constant and testable.

### 5. Automatic light sleep - hard blocked

Same source, same lines. The RGB panel driver creates an
`ESP_PM_NO_LIGHT_SLEEP` lock and, in its own words, holds "the lock during the
whole lifecycle of RGB panel". Automatic light sleep therefore cannot engage at
all while the display is initialised, no matter what else is configured. This is
not a tuning problem; it is a driver invariant.

The only way around it would be to delete and re-create the panel object around
sleep windows, which would mean a full display re-initialisation on every wake.
On this particular board that is worse than it sounds - see the next section.

### 6. Deep sleep - architecturally incompatible

The 4848S040 `device.yaml` does call `esp_deep_sleep_start()`, four times, but
never for power. It is a reset workaround: the S3 with octal PSRAM and an RGB
panel cannot reinitialise its peripherals after a software reset, so the boot
handler and both OTA `on_end` hooks arm a 100 ms timer wakeup and deep sleep to
force a hardware reset equivalent to a power cycle. That is the opposite of a
power feature and should not be confused with one.

Deep sleep as a power-saving mode fails on three independent grounds:

1. **There is no wake source.** The touchscreen is polled over I2C and has no
   interrupt pin configured on this device - unlike four of the six P4 panels,
   which set `interrupt_pin`. The only free GPIOs are 41 and 42, and ESP-IDF's S3
   capabilities header sets `SOC_RTCIO_PIN_COUNT` to 22, meaning only GPIO0 to
   GPIO21 are RTC-capable. GPIO41 and GPIO42 cannot serve as an `ext0` or
   `ext1` wake source. A timer wake is the only option, which does not give you
   wake-on-touch.
2. **Waking is a cold boot.** The device's own reset workaround proves this:
   deep-sleep wake is a full hardware reset by design. That means re-running
   panel init, PSRAM setup, LVGL construction, WiFi association, the Home
   Assistant API handshake and the whole staged reconnect sequence in
   `common/device/core_infra.yaml`, which spreads refresh work over 1 s, 2 s,
   8 s, 20 s and 25 s delays. The user-facing docs tell people to allow up to
   60 seconds on first boot.
3. **The product would not work.** A wall panel that is not reachable from Home
   Assistant and takes tens of seconds to respond to a touch is not the product.

**Deep sleep should be stated as a non-option, not listed as a possibility.**

### 7. Touch polling, timer load and cover art - marginal

- **Touch polling at 16 ms.** This was set deliberately in commit `fd683135d`
  "to enhance responsiveness", alongside elastic scrolling and scroll snapping.
  It is 62.5 I2C transactions per second, forever, because the S3 has no free
  pin for the GT911 interrupt line. Four of the six P4 panels use
  `interrupt_pin` instead, and the two that do not
  (`guition-esp32-p4-jc4880p443` and `esp32-p4-86`) simply take ESPHome's
  default interval, so the S3 is the only device asking for a faster one. Raising
  the S3 interval to 50 ms during `DISPLAY_OFF` only would be defensible, but
  the saving is small against a 20 MB/s framebuffer scan, and it directly
  lengthens the time to the first wake touch. **Not worth it.**
- **Periodic scripts.** The 250 ms and 500 ms timers listed earlier are each
  cheap, but they are the reason the CPU never idles for long. Gating them on
  display mode would be a real but modest saving, at the cost of touching the
  display lifecycle contract, which has strict generation-ordering invariants
  and requires physical testing on two panels for any behavioural change. **The
  regression risk is out of proportion to the gain.**
- **Cover art downloads.** Already the most expensive network activity on the
  panel, and already heavily constrained on the S3: `ESPCONTROL_LOW_HEAP_COVER_ART`,
  a 320-pixel decode target, and `cover_art_live_image_updates` set to false in
  the device profile. A user who wants to save power can already turn cover art
  off in the screensaver settings. **No change needed.**
- **The web configurator's event stream.** ESPHome's web server v3 streams state
  events over SSE, and `common/device/core_infra.yaml` sets
  `include_internal: true`, so a browser left open on the setup page holds a
  connection and keeps the radio transmitting. This is a documentation point,
  not a code change: leaving the setup page open costs power.

### 8. One that is easy to forget: the relays

The 4848S040C variant has three relays on GPIO40, GPIO2 and GPIO1. An energised
mechanical relay coil typically draws in the same range as the entire SoC. On a
battery-powered panel, a single relay left latched on would be a first-order
term in run time. This is worth a sentence in any user-facing power
documentation even though there is nothing to implement.

## Enable and Disable UX

### What the existing patterns are

The repository has two distinct, well-established mechanisms, and a proposal
should pick one rather than invent a third.

**Pattern A - an ESPHome template entity.** A `switch`, `select`, `number` or
`text` template platform with `internal: true`, `optimistic: true`,
`restore_value: true` (or `restore_mode: RESTORE_DEFAULT_OFF`),
`entity_category: config`, an icon, and a `web_server.sorting_group_id`. Because
`core_infra.yaml` sets `include_internal: true` on the web server while
`internal: true` hides the entity from the native API, these settings appear in
the device's own web UI and **not** in Home Assistant. The sorting groups are
declared in `common/device/core_infra.yaml`: `sg_button`, `sg_display`,
`sg_brightness`, `sg_screensaver`, `sg_connectivity` and `sg_firmware`.
Persistence is free - `restore_value: true` writes to ESPHome preferences.

This is how every screensaver, brightness and schedule setting is done today.
It costs one YAML block.

**Pattern B - a first-class web configurator control.** A named entity in
`product/v2/entity_names.json`, regenerated into
`src/webserver/generated/entity_catalog.ts`, plus an SSE alias in
`src/webserver/state/event_aliases.ts`, a field in the settings model, a POST
helper, a state loader entry, and a card in
`src/webserver/application/settings_page.ts`. This is what `screensaver_timeout`
does. It gives a designed control with live state, but it is roughly six files
plus a generator run plus three web checks.

**The per-device gate.** There is an exact precedent for "this feature only
exists on some panels", and it is the battery feature. In
`scripts/device_profiles.py`, `web_features()` derives
`features["battery"] = True` purely from the presence of a `battery` key in the
profile's `extraPackages`. Today only `product/v2/devices/guition-esp32-p4-jc8012p4a1.json`
has one, pointing at `devices/guition-esp32-p4-jc8012p4a1/device/battery.yaml`.
The settings page then guards its card on `layout.config.features.battery`. The
device YAML itself contains an `internal: true` switch
(`battery_status_enabled`) with `restore_mode: RESTORE_DEFAULT_OFF`, and
`docs/features/battery.md` explains that the feature is off by default because
the hardware assumption is unconfirmed.

That is precisely the situation a power-saving feature on the 4848S040 would be
in, and it is worth noting that the battery toggle is deliberately **not** in
the web backup contract - a device-local, hardware-dependent toggle does not
have to round-trip through backups.

### Recommendation

**Stage one - do the WiFi lever properly, and do nothing else.**

Add a `select` to a new device-scoped package, wired the same way as
`battery.yaml`:

- Options: `Balanced` (default, preserves today's `none`) and `Low Power`
  (applies `WIFI_PS_MIN_MODEM`). Two options, not three - `high` should not be
  offered, for the latency reasons above.
- `internal: true`, `restore_value: true`, `entity_category: config`,
  `sorting_group_id: sg_connectivity`.
- Applied on change and re-applied from the `wifi.on_connect` trigger.
- Gated per device via an `extraPackages` entry so it only appears where it has
  been tested.

This is Pattern A: one YAML file, one profile line, no web bundle changes, no
generator run, no backup contract change. It persists across reboots for free.
It can ship, be tested on a real board, and be reverted by a user who sees
Home Assistant responsiveness they do not like.

**Where it belongs: the on-device setup page, not Home Assistant.** Every
comparable setting is `internal: true` for a good reason - a WiFi power setting
that is only reachable over the network it affects is a bad failure mode, and
the device's own web page is served from the device. If a Home Assistant
diagnostic is wanted later, expose a read-only text sensor of the active mode
rather than a writable control.

**Stage two, only if stage one measures well on hardware:** consider a single
"Power Saving" preset in the settings page (Pattern B) that bundles the WiFi
mode with tighter screensaver and brightness defaults. That is a convenience
layer over controls that already exist, and it should be built only once there
is a measured reason to believe the bundle is worth a user's attention.

**Do not build:** a CPU frequency selector. Static clock is a build-time
property, it interacts with display stability, and it is not something a user
can evaluate. If 160 MHz proves safe on hardware, set it in `device.yaml` and
leave it alone.

## Interactions and Footguns

1. **WiFi power save re-application.** ESPHome calls its internal apply function
   on the `WIFI_EVENT_STA_START` event. Any runtime `esp_wifi_set_ps()` call
   will be silently reverted on the next reconnect. Re-apply from
   `wifi.on_connect` or the setting will appear to work and then quietly stop.
2. **Power save versus OTA.** Both OTA platforms on this device end with a deep
   sleep to force a hardware reset, and the ESPHome OTA path is already
   documented as memory-constrained here - `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`
   exists specifically so HTTPS firmware downloads can connect. Modem sleep adds
   packet latency during a long transfer. Requesting high-performance mode
   around OTA (`request_high_performance()` in the OTA `on_begin` hook) is the
   safe pattern and matches how sendspin uses the same API.
3. **Power save versus the reconnect flow.** `power_save_mode: none` arrived in
   a reconnection-reliability commit. `wifi_reconnect_flow` waits 60 s, then
   30 s, before falling back to the setup hotspot, and `ap_timeout` is 90 s.
   Changing radio behaviour is exactly the kind of change that could shift those
   timings. Any test of `light` must include forced disconnects, access point
   reboots and a password change, not just steady-state operation.
4. **CPU scaling versus RGB DMA.** Covered above, and it is the single most
   important constraint in this document. The relevant comments are in
   `devices/guition-esp32-s3-4848s040/device/device.yaml`: the conservative
   pixel clock, `CONFIG_ESP32S3_DATA_CACHE_LINE_64B` being required for
   bounce-buffer scan-out, and `CONFIG_LCD_RGB_RESTART_IN_VSYNC` being disabled
   because over-eager restarts shift the framebuffer. Every one of those is a
   scar from a timing failure. Anything that reduces PSRAM read throughput or
   CPU availability risks reopening them.
5. **`lvgl.pause` and rotation.** `backlight_pause_display_off` passes
   `show_snow: true` only for 0 and 180 degree rotations. Any change to the
   display-off path has to preserve that branch.
6. **The display lifecycle contract is strict.** `dev-docs/display-lifecycle.md`
   requires host tests for every priority pair and physical testing on both the
   10 inch P4 and the 4 inch S3 for any behaviour-changing PR. A power feature
   that gates existing timers or modes on display state is a
   behaviour-changing PR under that contract.
7. **Adding entities costs RAM on the S3.** The panel is documented throughout
   as close to its internal RAM limit: the API pool is capped at 3 connections,
   `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` is reduced to 8, and allocations over
   4 KiB are pushed to PSRAM. The historical baseline recorded 123,416 bytes of
   static RAM for this device. A single select entity is small, but a settings
   feature that adds a dozen entities is not free here.

## What Generalises to the P4 Panels

- **Backlight and screensaver behaviour**: fully shared, same addons, same
  entities. The two JC8012P4A1 variants keep LVGL running during display-off by
  build flag, so they give up that part of the saving deliberately.
- **WiFi power save**: the P4 panels have no radio of their own. They use an
  ESP32-C6 over SDIO via `esp32_hosted`, so the same YAML key takes a different
  path through hosted firmware with its own SDIO link power behaviour. Results
  from the S3 do not transfer, and the P4-86 additionally runs sendspin, which
  already requests high-performance networking for audio streaming and would
  fight a low-power default.
- **CPU frequency and DFS**: the P4 case is materially better, because ESP-IDF's
  RGB driver takes a `ESP_PM_CPU_FREQ_MAX` lock on P4 specifically. But the P4
  panels use MIPI DSI rather than RGB, so that particular driver is not even in
  play, and the analysis would have to be redone against the DSI driver.
- **Deep sleep**: equally inapplicable, and equally pointless on a mains-powered
  wall unit.
- **Touch polling**: not applicable. Every P4 panel already uses an interrupt
  pin.

In short: the display-side savings are shared, and everything radio- or
clock-related is device-specific and must be evaluated per panel.

## What Needs the Physical Device

None of the following can be settled from the repository.

1. **Does the 4848S040 have battery hardware at all?** Unresolved by the earlier
   feasibility study and still unresolved. Everything here is contingent on it.
2. **The actual power split.** The claim that the backlight dominates is
   standard for panels of this class but is not measured on this board. A single
   bench measurement at 100%, 50% and 0% backlight with WiFi associated would
   settle the whole prioritisation in about ten minutes and is by far the
   highest-value next step.
3. **What `power_save_mode: light` actually saves here**, and whether it
   degrades Home Assistant responsiveness enough to notice in use.
4. **Whether 160 MHz is safe**, tested with a full-screen cover-art redraw while
   WiFi and the web server are busy, watching for the vertical-shift and
   pixel-breakup failure modes already documented in `device.yaml`.
5. **Whether the GT911 interrupt line is routed to GPIO41 or GPIO42** on this
   board. If it is, interrupt-driven touch becomes possible; if not, polling is
   forced. This needs a continuity check, not a firmware build.

## Verification

Documentation-only change. `npm run check:dev-docs` passes. No firmware was
compiled: ESPHome is not installed in the environment this record was written
in, and no build or device claim in this page should be read as verified.
