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
board had — **and the screen reveals at 13:00.** That is what the design always
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
| Full refresh (draw + push) | **15-30 s** per the vendor; this tree once measured ~1.96 s, which was an artefact — see below |
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

That last line sat directly under a table claiming the refresh was 1.96 s, in
this file, for days. A cold boot cannot take 17.6 s and be "essentially all"
of a 1.96 s refresh. The contradiction was on one screen and went unread.

This corrects an earlier claim in this file and in commit a61f361 that
`M5.begin()` costs 52.7 s inherently. It does not; it costs 464 ms, and the
rest was a clear nobody needed.

The interaction decision has two supports, and both hold: the panel is far too
slow to answer a button press, and the answer is withheld until 13:00 anyway,
so a redraw would repaint the whole panel to show nothing new. The guess gets
an LED and a chirp instead.

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
in `core/` computes the next 06:30 or 13:00 local with DST and is host-tested.
M5Unified drives the RX8130CE directly, so there is no RTC driver to write.

**Wake cause is checked before `M5.begin()`**, because the short path decides
whether to bring the display up at all. A button wake never touches it: record
the guess, fire the LED and chirp, sleep again. Not because init is expensive —
it is 464 ms — but because the refresh is 15-30 s and the answer is withheld
until the reveal regardless. The screen catches up at the next scheduled wake.

Verified on hardware: cold boot → page drawn → `next wake 13:00 local, in 25498
s` → alarm read back → sleep, and it stays asleep.

**One bug worth remembering.** The first version cleared the RTC interrupt
before a 30-second grace delay, so the board woke the instant it slept — an
endless cycle that looks like a crash and flattens a battery. `wake_sleep()`
now clears immediately before sleeping and logs the actual level of G7 first,
because EXT1 wakes on low and sleeping with the line already low is a busy loop.

## Feedback: LED and chirp

`board/feedback.cpp`. A guess is acknowledged by the RGB LED (WS2812 on G21,
over RMT) and a note through the ES8311 codec — never by the screen, which
needs 15-30 s for a full refresh, has no partial update, and has nothing new to
show until the 13:00 reveal anyway.

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

Verified on hardware, once: the card mounted at 14910 MB and a missing
`wifi.json` was reported as exactly that. It has not mounted since — see below.

## The card reader never worked, so setup moved to the phone

This board's microSD path does not complete a data-block transfer. Both modes
fail at the same step:

    sdmmc_init_sd_ssr: sdmmc_send_cmd returned 0x107   (SPI, 3 attempts, power-cycled between)
    sdmmc_init_sd_ssr: sdmmc_send_cmd returned 0x107   (SDMMC 1-line, same pins)

That step is late. The card has already answered CMD0, CMD8 and ACMD41 and
handed over its CID and CSD, so **commands work and data blocks do not**. An
absent card fails far earlier. Reformatting the card FAT32 changed nothing, and
could not have: the failure is in card init, before any filesystem is read.

The card carried the two most personal pieces of content — the kids' names and
the school timetable — which left them with no route onto the device at all.
So they moved to the setup page:

**Hold any button and press reset.** The board publishes
`Morning-Riddle-Setup`, and `192.168.4.1` serves one form: WiFi, four children
with birthdays, and seven days of subjects. It is prefilled with whatever the
board currently holds, so fixing one subject does not mean retyping the week.

It has to be a **cold boot with a button held**, not a button wake — a button
wake is a child's guess, and turning that into a setup screen would be a bad
joke.

**Fields, not JSON.** Each day is one box of comma-separated subjects, which is
exactly what `schedule_t` stores after parsing. So this path needs no parser
and no serialiser, and the form renders straight from the structs. The card
import still works and still wins when a card is readable; it is now the
fallback rather than the plan.

`core/formdata.c` is the one fiddly part and is host-tested, because both bugs
it has had are invisible from a phone:

- **An unanchored search.** `strstr(body, "d1=")` also matches the tail of
  `k0d1=`. Harmless with two fields; the form now has twenty-one, and the
  symptom is one box reading another box's value.
- **A half-decoded character.** Truncation counts *urlencoded* bytes and one
  Hebrew letter is nine of them (`%D7%9E`), so a long line gets cut mid-letter.
  What survived was a lead byte with no continuation, and `hebrew.cpp` indexes
  on the decoded codepoint — it would draw the wrong glyph, not stop early.

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

**But do not reach for that explanation first.** A later day of download-mode
boots looked exactly like this and was NOT a driven strapping pin — it was the
sampled-state latch described below, where GPIO0 is physically free and the
chip is remembering a stale reading. I spent hours on a G45/G46 theory, shipped
a commit for it, and it was irrelevant. Check the latch first: it is one
command to rule out.

## The board latches into DOWNLOAD mode, and only a watchdog reset clears it

**Read this before concluding a board is dead. It cost most of a day.**

The symptom: every reset lands in

    rst:0x15 (USB_UART_CHIP_RESET),boot:0x21 (DOWNLOAD(USB/UART0))
    waiting for download

and the application never runs. Flashing still works perfectly, which makes it
look like a board that accepts firmware and refuses to execute it.

**The cause is documented by Espressif**, in the esptool troubleshooting page
for the ESP32-S3:

> the USB-Serial/JTAG peripheral can only trigger a core reset, which does not
> re-sample the state of the boot strapping pin. As a result, the state of the
> boot pin remains sampled as LOW, even if it is physically released, and the
> chip stays in download mode.

So GPIO0 is **not** being held down. The pin is free; the chip is remembering a
stale sample of it. Enter download mode once — by holding a button across a
reset, or by any host that asserts DTR — and every subsequent core reset
inherits the latch.

**The cure is one command:**

    esptool --port /dev/cu.usbmodemXXXX --before no_reset --after watchdog_reset run

`watchdog_reset` forces a full system reset, which re-samples the straps.
`hard_reset` does not, and neither does `idf.py flash`, `idf.py monitor`, or an
RTS pulse — they are all core resets, so **every attempt to recover or observe
the board re-armed the trap.**

**Why power cycling does not fix it.** It should, and on a bare board it does.
This one has a battery: unplugging USB does not remove power from the RTC
domain. Espressif's own issue tracker puts it plainly — a device with no
external reset button and a permanently attached battery cannot leave the
bootloader unaided.

**Every diagnostic on this board lies about it.** Opening the serial port
asserts DTR, which resets the chip; that is why every download-mode log
captured here reads `rst:0x15 (USB_UART_CHIP_RESET)` — the tooling, not the
fault. `idf.py monitor --no-reset` avoids the reset but does not survive the
USB re-enumeration a reset causes.

**How to tell whether the application is running, without serial.**
`state_bump_boot_count()` writes a counter to NVS before anything else in
`app_main`. Dump the partition, power-cycle, dump it again:

    esptool --before no_reset read_flash 0x9000 0x6000 nvs.bin

Byte-identical means the firmware never ran, and that conclusion depends on no
display, no LED, and no serial port. Conversely, `esptool` failing to sync with
`--before no_reset` means the ROM bootloader is gone and the app **is**
running — a connection error is the good outcome.

**eFuses are worth ruling out once**, and take a second:

    espefuse --port /dev/cu.usbmodemXXXX --before no_reset summary

All-default here: no secure boot, no `DIS_FORCE_DOWNLOAD`, no JTAG lock. Had
one of those been set the board would have been permanently locked rather than
merely latched.

## WiFi, verified

Portal → credentials in NVS → connect → NTP → RTC:

    net: got 10.100.102.120
    net: RTC set to 2026-08-28 19:42:00 UTC

That also retires the `day=20464` problem: the state machine had been
self-consistent against a clock that had never been set.

## Riddles

`core/riddle_batch.c` parses the batch — pure, host-tested. On the Waveshare
board this logic sat inside `page_riddle.cc` with `ESP_LOG` threaded through
it, so none of its rules could be checked without hardware. Two are
load-bearing:

- **Choices that do not contain the answer make the game unwinnable** — every
  guess would be marked wrong. Falls back to a plain reveal.
- **One bad entry costs that entry, not the batch.** A missing answer among
  thirty riddles should not cost the month.

`board/batch.cpp` fetches over HTTPS and caches in NVS. The cache is the point:
the board wakes twice a day, so re-fetching every wake would make the page
depend on the network being up at 06:30 — and a morning with no riddle is the
failure this design exists to prevent.

It carries the 4096-byte header buffer from the outset, with the reason in a
comment. GitHub's release redirect has a signed URL whose X-Amz signature alone
overflows the 512-byte default; it fails as "Out of buffer" then a transport
error, which reads as a network fault and is not one.

**Verified on hardware:** `fetched 6790 bytes`, `batch holds 30 riddle(s)`,
`showing riddle 0 of 30, choices=1`.

**One bug worth remembering.** `riddle_batch_t` is ~19KB and the main task
stack was 10KB, so a local variable overflowed it and corrupted the heap —
surfacing as `assert failed: heap_caps_realloc_base ... realloc() pointer is
outside heap areas` from inside cJSON, which points nowhere near the cause.
The structures are static now and the stack is 24KB.

## Selection and the reveal

`riddle_decide()` owns which riddle is shown. `begin_day()` advances `idx` once
per day and wraps at the batch size — a repeat is a mild disappointment, a
blank wall reads as a broken device — with a double-fire guard so a second wake
on the same day redraws rather than consuming another riddle.

The board now passes it the **real** batch count and uses the index it returns,
along with the real date and streak. An alarm wake picks its slot from the
clock; anything else is treated as a morning, which is idempotent within a day
because of that guard.

**`ACT_NONE` skips the draw entirely.** When the state machine says the panel is
already correct, not refreshing saves a full ~17 s waveform and the power with
it. On a panel this slow, not redrawing is a feature.

At 13:00 the answer replaces the choices rather than sitting under them: a
child reading after school wants the answer, and leaving three unpressable
options invites a guess the board will refuse.

Verified on hardware: `day=20693` (the real date via NTP), `idx=0/30`, and a
same-day reboot correctly kept `idx` rather than advancing.

## The refresh is ~17 seconds, and my 2-second measurement is the bug

**This section was wrong twice and is being corrected against outside
evidence, not against another measurement of my own.**

What the rest of the world reports for this exact panel:

| source | figure |
|---|---|
| [M5Stack's own PaperColor docs](https://docs.m5stack.com/en/core/PaperColor) | 15-30 s, by colour complexity |
| [PaperSatColor](https://github.com/prstoetzer/PaperSatColor), a dashboard on this board | "roughly 15-19 seconds" per redraw |
| Hackster, LinuxGizmos on the Spectra 6 launch | double-digit seconds |

**So the original ~17100 ms measurements were right.** They agreed with the
vendor, with an independent project on the same hardware, and with the physics
of a six-ink panel. I recorded them, then later measured ~1959 ms, and
concluded the ~17 s figure was "unexplained" and ~2 s was "the number to design
against." That was backwards: I treated my own anomalous reading as ground
truth and the correct one as a mystery.

What was actually measured at ~1959 ms:

| trial | time |
|---|---|
| all white (from the page) | 1959 ms |
| all black (from white) | 1939 ms |
| all white (from black) | 1959 ms |
| all white again (no change) | 1959 ms |
| full page (from white) | 1959 ms |
| first refresh after a full power cycle | 1964 ms |

Note what that table actually shows: **the time does not vary with content at
all.** An all-white push, an all-black push, and a full page cost the same to
within 20 ms, and a no-op redraw costs the same as a real one. A panel that is
genuinely driving ink cannot be indifferent to how much ink moves; the vendor's
own figure varies by colour complexity for exactly that reason. A constant
~1959 ms is the signature of a call that returns without waiting for the
waveform — a push that never reaches the glass.

**That is very likely the same fault as the blank redraw.** The board currently
draws a page, logs `FULL REFRESH: draw+push 1958 ms`, and leaves the panel
showing a previous day's image. One fault explains both: the push is completing
in software and not on the panel. The two were filed as separate mysteries for
most of a day because the fast number had been written down as a success.

**What this means for the design.** LED-and-chirp was originally justified by
"the screen cannot answer a button press" at ~17 s. That argument is correct
after all, and this file spent a commit dismantling it on bad data. The
reveal-at-13:00 decision has two independent supports and always did: the panel
genuinely is too slow to acknowledge a press, *and* the answer is deliberately
withheld until the reveal, so a redraw would repaint the same question at full
cost. Nothing about the interaction needs to change; only the reasoning in the
comments, which is now corrected in `board/feedback.hpp`, `board/wake.hpp`,
`ui/page_daily.hpp` and `main.cpp`.

**Method note, since this cost real time.** Two of the three wrong turns here
came from trusting a single self-produced number over an obvious sanity check.
A refresh time that does not change with content, and a board that reports a
successful draw while showing yesterday's screen, were each enough to falsify
the ~2 s reading on the spot. Neither was checked against anything outside this
repository until a search of the last thirty days of M5Paper discussion turned
up the vendor's own figure in about a minute.

## What is NOT done yet

- **The guess path has never executed.** It only runs on a battery-powered
  button wake, and USB serial is gone in exactly that case — so the one
  interaction a child actually performs is the one piece of this that has never
  been observed running. It needs a deliberate approach: record the outcome to
  NVS and read it back on the next cabled boot.
- **The setup form has not been round-tripped from a phone yet.** It builds,
  its parsing is host-tested, and the gesture opens it; nobody has typed a
  Hebrew timetable into it and watched the page redraw.
- **Two cross-artifact checks were dropped, deliberately, not silently.** The
  Waveshare tree had `make icons` (every `wmo_icon()` name has a matching
  `.bmp` under `Weather_img/`) and `make buttons` (every code the classifier
  matches is really emitted by `button_bsp.c`, and unambiguously). Both pin
  Waveshare-specific artefacts that do not exist here. Both caught real bugs
  and both need equivalents once the M5GFX drawing and M5Unified button paths
  exist. Until then `wmo_icon()` is tested but unused.
- **The panel is not actually refreshing.** The firmware reports
  `FULL REFRESH: draw+push ~1958 ms` and the screen keeps a previous day's
  image. ~1958 ms is about a ninth of this panel's real refresh time and does
  not vary with content, so the push is almost certainly returning without
  driving the waveform. This is the top open bug.
- **RESOLVED: the board would not execute the application.** It was the
  USB-Serial/JTAG download-mode latch, not hardware and not firmware — see
  "The board latches into DOWNLOAD mode" above. One `--after watchdog_reset`
  cleared it. The investigation below is left in place because the *method* is
  what was worth keeping.

  It was measured rather than inferred.

  The three obvious witnesses all lie on this board. **Serial** cannot be
  trusted: opening the port asserts DTR, which forces the ROM bootloader, so
  every download-mode log captured reads `rst:0x15 (USB_UART_CHIP_RESET)` —
  my own tooling, every time. **The panel** cannot be trusted: a push that
  returns in ~1958 ms is probably never reaching the glass, so an unchanged
  screen says nothing. **The LED** cannot be trusted either: it only ever fires
  on the guess path, which has never executed, so it has never been observed
  working.

  So the test avoids all three. `state_bump_boot_count()` writes a counter to
  NVS before anything else in `app_main`. Dump the partition with esptool,
  power-cycle the board, dump it again:

      esptool --before no_reset read_flash 0x9000 0x6000 nvs.bin

  Across a full power-off with the SD card removed, the 24 KB partition is
  **byte-identical** and no `boots` key exists. The firmware does not run.

  That reasoning was right up to its last step. GPIO0 *was* sampled low — but
  nothing physical was holding it, and I concluded it was a stuck button on a
  board whose buttons were fine.

- **RESOLVED: the panel rendered nothing.** It was this repository after all,
  and the cause was `sdcard.cpp` power-cycling PM1 register `0x11` bit 3
  between mount attempts. That bit is **PM1's GPIO3 output, which M5Unified
  uses to enable the ES8311 codec** (`M5Unified.cpp:506`/`:513`) -- not the TF
  rail. The mapping was invented and never checked.

  Once the card began failing, the "recovery" ran four times per boot and left
  the display unable to render: it initialised, cleared, reported successful
  pushes, and showed nothing. The same binary that had rendered days earlier
  now did not, which made the code look innocent and sent the investigation
  through strapping pins, DMA heaps, autodetect caches, and a suspected dead
  panel. M5Stack's factory firmware rendering perfectly is what turned it back
  into a software bug.

  **The lesson worth keeping:** do not power-cycle a rail you have not
  confirmed from a datasheet, and treat "the same binary behaves differently"
  as evidence that something stateful changed underneath -- not as evidence
  that the code is innocent.
