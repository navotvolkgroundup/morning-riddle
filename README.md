# Morning Riddle — M5Paper Color port

Port of the Morning Riddle daily page from the Waveshare ESP32-S3-ePaper-3.97
to the M5Stack M5Paper Color (600×400, 4" E Ink Spectra 6).

The Waveshare tree stays where it is, under `vendor-source/`. This is a new
tree rather than a branch because almost none of that scaffolding survives:
the eight-tile menu, `page_weather`, `page_news`, the SSD16xx driver, the
AXP2101 and PCF85063 drivers are all board-specific and none of them apply.

## The constraint that shaped this

**Spectra 6 has no partial refresh, and a full refresh takes 15–30 seconds**,
varying with colour complexity. Colour needs a full-panel waveform; there is
no windowed mode.

That kills guess-and-reveal as the Waveshare version did it. A child pressed a
button and the screen answered in under a second. Here it would answer in
twenty, which is not an interaction.

**Resolution: the guess gets instant feedback that is not the screen** — the
RGB LED on G21 and a chirp through the speaker on G46, neither of which the old
board had — **and the screen reveals at 16:00.** That is what the design always
intended: two draws a day, nobody watching either. The panel's weakness turns
out to match the product.

## Architecture change: deep sleep, not PMIC shutdown

The Waveshare build powered the board fully off through the AXP2101 and relied
on an RTC alarm to switch it back on. That is not needed here. Quoted standby
is 92.5 µA — about 2.2 mAh/day, so the 1250 mAh cell runs well over a year,
comfortably better than the old board's 1.8–2.5 mAh/day on 1000 mAh.

So: ESP32-S3 deep sleep, woken by RX8130CE's interrupt on G7, plus the buttons.

This deletes a whole class of machinery and one genuinely nasty bug with it.
The old design kept a boot mode in NVS, and the vendor's recovery gate powered
the board off whenever it found that mode set with USB attached — so plugging
in a board that was in ambient mode produced one that powered itself off on
every boot and could only be reached by holding BOOT for download mode. Gone:
`save_mode_enable_to_nvs`, the recovery gate, `sched_power_off_if_safe`, and
`page_riddle_menu_escape`, which existed only to escape that state.

## Hardware

| | |
|---|---|
| MCU | ESP32-S3R8, 16 MB flash, 8 MB PSRAM (same silicon as the old board) |
| Panel | 600×400 landscape, Spectra 6, full refresh only |
| Buttons | G10 (A), G9 (B), G1 (C), plus a power key |
| RTC | RX8130CE, I²C on G2/G3, IRQ on G7 |
| microSD | G47 CS, G15 CLK, G13 MOSI, G14 MISO |
| Feedback | RGB LED G21; speaker AW8737A, enable G46 |
| Power | M5PM1 PMIC, 1250 mAh |
| Libraries | M5Unified, M5GFX, M5PM1 |

## What ported untouched

Everything in `main/core/`, with all 2379 host checks passing before a single
line of M5Stack code existed:

`riddle_decide` · `wake_log` · `kids` · `weather` · `schedule` · `sd_json` ·
`daily_layout`

This is the payoff from keeping the decision core IDF-free and board-free on
the old board. It was argued for on testability grounds; it turned out to be
what made a hardware change cheap.

## Layout: 400×600 portrait

**The panel is natively 400×600.** The "600×400" in the product name is the
rotated view — `M5.Display` reports 400×600 before any rotation, confirmed on
hardware.

An earlier version of the geometry was built for landscape, on the reasoning
that height had halved from the Waveshare board's 800 to 400. It hadn't: there
are 600 pixels of height. Three compensations were introduced to survive a
squeeze that did not exist, and all three are gone:

- a horizontal band (schedule beside weather) — back to stacked, since side by
  side gives each 190px on a 400-wide panel, too little for a Hebrew timetable
- a birthday suppressing the callout — they coexist again
- no eye-level floor — `DL_RIDDLE_TOP_MIN` is back, at 200

That last one also revives the **wall measurement**: the floor keeps the riddle
at a child's eye level while the utility band sits at an adult's. Hung low, set
it to 0 and the layout follows the zones directly.

`DL_RIDDLE_MIN_H` is 280 and the worst case is 291. Five mutations confirm the
tests bite: band taking no space, schedule and weather sharing a line, the
callout not moving the riddle, the floor removed, and the banner grown until it
eats the riddle.

## Measured on hardware

| | |
|---|---|
| `M5.begin()` | **464 ms** with `clear_display=false` (was 52.7 s with M5's own clear) |
| Full refresh (draw + push) | **17.1 s** |
| `display()` after `endWrite()` | 0 ms — the push already happened |
| Board autodetect | `M5GFX: [Autodetect] board_M5PaperColor` at 933 ms |

Two things follow.

**Batch every draw.** Without `startWrite`/`endWrite` each primitive can push
its own full-panel waveform. Six bars and two strings then take minutes: the
bars appear one at a time and the code after them looks hung — observed as four
minutes with no panic and no watchdog.

**`cfg.clear_display = false` is worth 52 seconds.** M5's own startup clear is
a full-panel waveform, and the page fills the screen itself — so it was paid
for and then immediately overwritten. Cold boot to a drawn page is about
**17.6 s**, essentially all of it the one refresh that matters.

This corrects an earlier claim in this file and in commit a61f361 that
`M5.begin()` costs 52.7 s inherently. It does not; it costs 464 ms, and the
rest was a clear nobody needed.

The 17.1 s refresh confirms the interaction decision from the other direction:
the screen cannot answer a button press, so the guess gets an LED and a chirp
and the reveal waits for 16:00.

## Hebrew

Ported from the Waveshare `hebrew.inc`, and split the way the rest of this
project is split:

- **`core/he_text.c`** — UTF-8 decoding, advance widths, word breaking. Pure,
  host-tested. On the old board this lived beside the pixel blitting, so the
  wrap logic could only be checked by looking at a panel; here that costs 17
  seconds an attempt.
- **`ui/hebrew.cpp`** — glyph blitting and the RTL pen, against M5GFX. Colour
  is a parameter now rather than a hard-coded `BLACK`.

Two bugs the split surfaced immediately, both inherited:

**Niqqud advanced the pen but drew nothing.** A mark like U+05B4 is inside the
Hebrew block, so `he_is_hebrew` accepted it, but it is outside the glyph range
`0x05D0–0x05EA` and has no bitmap. The old code advanced by `HE_GAP` anyway —
an invisible gap mid-word, the exact failure `schedule.c`'s `drawable()` guard
exists to prevent. `he_is_letter()` now draws the distinction.

**A word wider than the line returned nothing**, which made the caller stop and
silently drop the rest of the text — for a riddle, the question would just end.
It is now emitted whole and left to overhang, which `draw_line_rtl` clips.

Four mutations confirm the tests bite.

## The daily page

`ui/page_daily.cpp` assembles the whole page and pushes it **once** — header,
utility band, birthday banner, name callout, riddle and three choices — with
zone positions from `daily_layout` and text from the Hebrew renderer.

It takes its content as a parameter rather than reading NVS, the SD card or the
network. That lets it be drawn from sample data before any of those exist, and
keeps deciding *what* to show separate from deciding *where it goes* — the
split that has now paid for itself three times on this port.

Colour is used in exactly two places, both meaning rather than decoration: the
birthday banner is red, and a stale weather reading is marked red. Everything
else is black on white, so the page still reads if colour ever misbehaves.

**`PD_BUTTONS_ON_LEFT_EDGE` is unverified.** The choice markers must sit on the
same edge as the buttons they name, or the page tells the reader to press the
wrong one. On the Waveshare board this was guessed wrong first and settled only
by looking at the hardware. It needs the same check here.

## Waking

`board/wake.cpp`. The scheduling arithmetic is not there — `riddle_next_wake()`
in `core/` computes the next 06:30 or 16:00 local with DST and is host-tested.
M5Unified drives the RX8130CE directly, so there is no RTC driver to write.

**The 52-second problem shapes this.** `M5.begin()` costs 52.7s of panel init
and deep sleep does not avoid it — waking restarts the application. Fine twice
a day when nobody is watching; unusable for a button press.

So **wake cause is checked before `M5.begin()`**, and a button wake never
touches the display: record the guess, fire the LED and chirp, sleep again.
Milliseconds. The screen catches up at the next scheduled wake, which is what
the design already decided when it moved the reveal to 16:00.

Verified on hardware: cold boot → page drawn → `next wake 16:00 local, in 25498
s` → alarm read back → sleep, and it stays asleep.

**One bug worth remembering.** The first version cleared the RTC interrupt
before a 30-second grace delay, so the board woke the instant it slept — an
endless cycle that looks like a crash and flattens a battery. `wake_sleep()`
now clears immediately before sleeping and logs the actual level of G7 first,
because EXT1 wakes on low and sleeping with the line already low is a busy loop.

## Feedback: LED and chirp

`board/feedback.cpp`. A guess is acknowledged by the RGB LED (WS2812 on G21,
over RMT) and a note through the ES8311 codec — never by the screen, which
needs 17.1 s and cannot answer a press. The reveal waits for the 16:00 wake.

Colour **and** sound each carry the whole answer, and each choice is distinct:
A blue/G5, B green/B5, C orange/D6, rejection red with a low two-note. A
colour-blind reader or a noisy kitchen still gets it, and a child who presses B
can tell the board heard B rather than merely that it heard something.

Both come from M5Unified — no hand-rolled drivers. That only became possible
once `cfg.clear_display=false` brought `M5.begin()` down to 464 ms and made it
affordable on a button wake; at the apparent 52.7 s it was not, and the plan
had been raw ES8311 register setup over I²S.

`feedback_settle()` waits for the tone and clears the LED before sleeping.
Deep sleep cuts RMT and I²S mid-output, which turns the chirp into a click and
can leave the LED latched on — for hours, on a battery, with nobody watching.

**Verified on hardware:** LED blinked and chirped, all four signals.

## Guess state

`board/state.cpp` persists `riddle_nvs_t` to NVS. `riddle_decide()` cannot
store anything itself — it takes no IDF header by design — so this is the other
half: the part that knows about NVS and nothing about the rules.

The struct is written whole. It is 16 bytes and only meaningful together: a
streak saved without its day, or a guess without the state that makes it valid,
is worse than no save. A blob of the wrong size is ignored rather than read as
the current layout, which would silently scramble the day and streak after a
struct change.

**The button path acknowledges what the state machine decided, not what was
pressed.** A guess after the answer is out, or against yesterday's screen, is a
no-op — and telling a child their guess landed when it did not is worse than
telling them it did not. Accepted guesses get their colour and note; refused
ones get the red rejection.

Verified on hardware: page records `state=1` (question shown), saves, and the
value loads back intact across a reboot.

**The clock is synced before anything asks the date.** `riddle_local_day()`
keys the whole state machine, so a page recorded against an unsynced 1970 and a
guess judged against the real date disagree — and every press is then correctly
refused as "yesterday's screen", which looks exactly like a broken button.

## SD card and network

`board/sdcard.cpp` mounts the microSD on **SPI3** — M5GFX has already claimed
SPI2 for the panel, and asking for it again returns `ESP_ERR_INVALID_STATE`.
`format_if_mount_failed` is deliberately false: the card carries the only copy
of the kids' names and the timetable, and reformatting after a bad read is data
loss, not recovery.

`board/net.cpp` takes **credentials from the card, never from the build**:

    /sdcard/wifi.json   { "ssid": "...", "pass": "..." }

This repository is public. A Kconfig option or a `#define` would put a home
network password into git history where it cannot be taken back out. The SSID
is logged; the password never is, at any level, and both are wiped from memory
once the driver has its own copy. A template is in `docs/sdcard/`.

NTP writes **both** the system clock and the RTC. The system clock dies with
the power and this board deep-sleeps between wakes, so a sync that only set the
system clock would be forgotten before it was ever used.

Every step may fail — no card, no file, no network — and the page still draws
from cache. Visibly stale beats blank.

Verified on hardware: card mounts (14910 MB), and a missing `wifi.json` is
reported as exactly that.

## G46 is a boot strapping pin, and it is the speaker enable

The single worst bug of this port, and it was self-inflicted.

`SPK_EN` on **G46** is also an **ESP32-S3 boot strapping pin**. Once the chirp
enabled the amplifier, G46 stayed driven high — and every subsequent reset
sampled it high and booted to `DOWNLOAD(USB/UART0)` instead of running the
application. Only fully removing power let it settle.

The board therefore appeared bricked for a long stretch: flashes verified
every time, nothing ever ran, and no amount of resetting helped. Two buttons
had genuinely stuck earlier in the day, so a hardware fault was the obvious
story, and it was wrong — twice recommended as a warranty case on a working
board.

The boot line named the mechanism as soon as one was captured cleanly:

    rst:0x3 (RTC_SW_SYS_RST), boot:0x21 (DOWNLOAD(USB/UART0))

versus, after the fix:

    rst:0x15 (USB_UART_CHIP_RESET), boot:0x2b (SPI_FAST_FLASH_BOOT)

`feedback_settle()` now calls `M5.Speaker.end()` and releases G46 to input.

**Worth checking for the same class of bug:** G0, G3, G45 and G46 are all
strapping pins on this chip. Anything this design drives on one of them can
reproduce this in a new disguise.

## WiFi, verified

Portal → credentials in NVS → connect → NTP → RTC:

    net: got 10.100.102.120
    net: RTC set to 2026-08-28 19:42:00 UTC

That also retires the `day=20464` problem: the state machine had been
self-consistent against a clock that had never been set.

## What is NOT done yet

- **No board code at all.** No display, RTC, buttons, power, or `main`.
- **Two cross-artifact checks were dropped, deliberately, not silently.** The
  Waveshare tree had `make icons` (every `wmo_icon()` name has a matching
  `.bmp` under `Weather_img/`) and `make buttons` (every code the classifier
  matches is really emitted by `button_bsp.c`, and unambiguously). Both pin
  Waveshare-specific artefacts that do not exist here. Both caught real bugs
  and both need equivalents once the M5GFX drawing and M5Unified button paths
  exist. Until then `wmo_icon()` is tested but unused.
