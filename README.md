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
| `M5.begin()` | **52.7 s** — Spectra 6 panel initialisation |
| Full refresh (draw + push) | **17.1 s** |
| `display()` after `endWrite()` | 0 ms — the push already happened |
| Board autodetect | `M5GFX: [Autodetect] board_M5PaperColor` at 933 ms |

Two things follow.

**Batch every draw.** Without `startWrite`/`endWrite` each primitive can push
its own full-panel waveform. Six bars and two strings then take minutes: the
bars appear one at a time and the code after them looks hung — observed as four
minutes with no panic and no watchdog.

**Cold boot costs about 70 seconds** before anything is on screen: 52.7 s of
panel init plus 17.1 s of refresh. That is the dominant number in this project
and the twice-daily wake design has not accounted for it. Deep sleep that
retains the panel may be worth far more than the 92.5 µA figure suggests, since
it is the *init* rather than the refresh that dominates.

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

## What is NOT done yet

- **No board code at all.** No display, RTC, buttons, power, or `main`.
- **Two cross-artifact checks were dropped, deliberately, not silently.** The
  Waveshare tree had `make icons` (every `wmo_icon()` name has a matching
  `.bmp` under `Weather_img/`) and `make buttons` (every code the classifier
  matches is really emitted by `button_bsp.c`, and unambiguously). Both pin
  Waveshare-specific artefacts that do not exist here. Both caught real bugs
  and both need equivalents once the M5GFX drawing and M5Unified button paths
  exist. Until then `wmo_icon()` is tested but unused.
