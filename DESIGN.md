# DESIGN.md — הבוקר

The design system for a 400×600 six-colour e-paper page that publishes once a
morning and once at 13:00, on a battery, in Hebrew, for children.

Every rule here exists because the hardware or the reader forced it. Where a
rule looks arbitrary, the number is the budget and the comment in the code says
where it came from.

---

## The memorable thing

**A paper that arrives.** Not a screen that is on.

Everything below serves that. The masthead, the reversed dateline, the ruled
lists, the folio and the edition time are all there to say *this was made this
morning*, which is the one thing a display cannot say and a paper says without
trying.

The competing product category — measured, not assumed, via `/last30days
"e-ink family dashboard"`, 110 items across 7 sources on 2026-08-31 — is
entirely one-way. Calendars, weather, chore lists, commercial dashboards with a
year of battery. Not one of them takes input. Three buttons and a chirp are the
rarest thing in this category, so the page is designed around a child answering
it, not reading it.

---

## The medium, and what it forbids

Read this before proposing anything.

**The panel is greyscale.** M5GFX's `Panel_EPD` forces `grayscale_8bit` in
`setColorDepth()` and has no colour concept anywhere in the file — identical at
0.2.28, so it is not a version we are behind on. Its LUT comment says it
outright: sixteen levels, black at one end and white at the other.

**And yet it shows colour.** Spectra 6 has six pigments, and driving them with
a greyscale waveform lands some levels on a coloured particle. So colour is
real and **not addressable by name**. `TFT_RED` does not ask for red; it asks
for a luminance, and whichever pigment that luminance settles on is what
appears. It has been coming out green.

Consequence: every colour in this document is *nominal* until the grey→ink
mapping is measured (`-DPD_CALIBRATE=1` draws the sixteen steps). Do not add a
colour decision expecting the colour you named.

**A full refresh is 15–30 s.** Measured at 17.1 s. There is no partial update.
So: no animation, no progressive disclosure, no interaction that expects the
screen to answer. A button press is acknowledged by an LED and a chirp, never
by the panel.

**Drawing goes through a canvas, not the display.** `M5.Display.display()`
renders and moves no ink. `pushSprite()` from an off-screen `M5Canvas` is the
only path that drives this panel, and the canvas must be in PSRAM
(`setPsram(true)`) or it competes with WiFi for internal DMA RAM and
intermittently fails to allocate.

---

## Type

One family, three sizes, and the sizes are the whole hierarchy — there is no
second weight, no italic, no small caps.

| Role | Face | Cell | Used for |
|---|---|---|---|
| Body | New Peninim MT 40pt | 24×41 | Question, choices, answer, timetable, turn line |
| Small | New Peninim MT 22pt | 16×24 | Dateline, folio, kickers, birthday label, streak |
| Display | Body at scale 2 | 48×82 | The revealed answer, and the drop cap |

Both cuts come from `gen_hebrew_fon.py` — same face, same generator, so they
are one family rather than two fonts. 16×22 was tried and rejected: its stems
break up, and thin stems are the wrong trade on 1-bit e-ink with no
antialiasing to carry them.

**Latin** is M5GFX's built-in 6×8 at `setTextSize(2)`, advance 12px
(`HE_LAT_W`). It renders inside RTL lines as a left-to-right run in a reserved
slot. Reserving 16 and drawing 12 put all the slack on one side and detached
`27C` from the words beside it; the two must stay equal.

**Scale 2 is pixel doubling**, not a second cut. It looks like exactly what it
is up close and is unmistakable across a room, which is the trade the reveal
wants.

**Ink extent is rows 5–37 of the 41px cell.** This is load-bearing: it is why a
body line can be drawn at y=0 and still clear a rule at y=42.

---

## Colour

Nominal until calibrated. Semantic only — the page must read correctly in black
and white, and to a colour-blind child.

| Token | Meaning | Where |
|---|---|---|
| Black | Everything | Body, rules, the reversed bar's ground |
| White | The paper | Reversed type; skipped when blitting a picture |
| Red | The paper's own voice, and the payoff | Nameplate, drop cap, revealed answer, birthday, kickers |
| Blue | Wet or cold | Weather advice, rain and snow marks |
| Yellow | Heat, and sun | Weather advice at 30 °C+, the sun symbol |
| Green | Unused | — |

**Stale outranks tone.** A weather reading that may be wrong is red, never the
colour that says *trust this and take a hat*.

**Colour is always redundant with the words.** `weather_advice_tone()` sits
beside `weather_advice_he()` branch for branch, tested against the same
boundaries, so the two cannot drift.

---

## Rules, and their weights

Three weights, and each means one thing.

| Weight | Meaning | Where |
|---|---|---|
| 3px, reversed bar | The paper's identity ends, its contents begin | Under the nameplate |
| 2px | Major division | Above the lead |
| 1px hairline | Minor division | Band top and bottom, between choices, above the folio |

Rules do the work a box used to. A bordered rectangle around the timetable made
it look like a form to fill in; two hairlines make it look like a column to
read. The **one box on the page** is the weather panel, which is what makes
that box mean something.

---

## The page

```
הבוקר                                    nameplate, body, red, y=0
████ יום שני · 31/08 · גיליון 12 ████    reversed bar, small, white on black
──────────────────────────────────────   hairline
 ┌────────┐  חשבון, אנגלית,              weather panel 126×104 + timetable
 │  ☀☁ 28°│  חינוך גופני                 wrapping in the 232px beside it
 │כובע ומים│
 └────────┘
──────────────────────────────────────   hairline
נגה, זאת בשבילך        5 פעמים ברצף      whose turn, with their run
══════════════════════════════════════   2px, the lead rule
מה יש לו שיניים ואף פעם לא נושך?        the lead, drop cap on the first letter
──────────────────────────────────────
מסרק                                     ruled choices, one per button
──────────────────────────────────────
מזלג
──────────────────────────────────────
מברשת
──────────────────────────────────────
──────────────────────────────────────   hairline
הבוקר · גיליון 12          מהדורת 06:31  folio
```

**Reading order is right to left.** The nameplate, the dateline, the timetable,
the turn line and every choice all start at the right margin. The only
left-anchored things are the deliberate counterweights: the dateline against
the nameplate, the streak against the turn, the edition time against the folio.
That flag-and-nameplate pairing is the page's one repeated gesture.

**The choices are ordered top to bottom to match three physical buttons.** That
ordering is the only affordance for which button to press, and it is why there
are no letters: the page printed A/B/C while the case is silkscreened C/B/A,
so a child who trusted the letters pressed the opposite end of the list.

---

## Layout budget

`DL_CANVAS_H` is 600; `DL_BODY_BOTTOM` is 592.

| Zone | Cost | Notes |
|---|---|---|
| Masthead + reversed bar | 79 | Fixed |
| Facts band | 114 | Weather panel 104 + hairlines and padding |
| Birthday | 74 | Small label + name. **Replaces** the turn line |
| Turn line | 48 | |
| Lead rule + gap | 12 | |
| Folio | 31 | Hairline + gap + one small line |
| **Riddle zone** | what is left | Floor `DL_RIDDLE_MIN_H` = 272 |

The floor guards the **common** case, not the worst one, and always did. Two
lines of question plus three ruled choices is 261. A five-line question plus
the same choices is 366, which no floor on a 600px panel was going to
guarantee — the wrapper caps at five lines and `riddle_gen.py validate` is what
actually keeps questions short.

**The riddle block is measured and centred**, so a short riddle and a long one
both look composed rather than one looking unfinished.

---

## Rules for anything added later

1. **Advice, not data.** The weather line said `19.4C partly cloudy 24/17` —
   four facts and no decision, in a language its reader cannot read. It says
   `19C sweatshirt`. A number a child has to interpret is not information.
2. **Nothing is drawn that has not been measured.** `riddle_gen.py` validates
   every string against the real font blob and the real widths, including the
   answer at double size. `draw_line_rtl` silently returns when it runs out of
   room, so an unmeasured string is a silent truncation.
3. **Colour is redundant.** Remove it and the page must still be correct.
4. **A zone that is nice to have fills slack; it never takes space.** The
   picture band is the model: the riddle block is centred, so a short riddle
   leaves room the band takes and a long one leaves none.
5. **Render before flashing.** A refresh is 17 s on a battery. `paper.py` in
   the scratchpad draws the real font blob through the real layout constants;
   every layout change in this project was verified there first, and the two
   that were not are the two that shipped broken.
6. **A page that cannot be drawn must not be pushed.** An all-zero canvas is
   black. Guard the push on having drawn something.

---

## Anti-patterns, all of them observed here

- **Naming a colour and assuming you got it.** See *The medium*.
- **A comment that promises what the code does not do.** `draw_line_rtl` had no
  size parameter while its comment promised the answer was "larger than the
  question was… readable across a room".
- **A check that verifies the wrong thing.** `wake_arm_next` said "READ IT
  BACK" and then read the clock, not the alarm.
- **Letters that name a physical control.** They contradicted the case.
- **A feature sized so it never fires.** The picture band at 140px; the
  variable headline at any size.
- **Furniture that costs a feature.** The folio's 31px is what put the picture
  band out of reach on an ordinary day. See below.

---

## Open tensions

**The picture band is unreachable on an ordinary day.** Measured:

| Day | Zone | Block | Slack | 56px band |
|---|---|---|---|---|
| timetable + weather + turn | 301 | 261 | 40 | no |
| no weather | 335 | 261 | 74 | fits |
| bare | 422 | 261 | 161 | fits |

The weather panel is the reason: 104px, present essentially every day because
the reading is cached. The density that makes this look like a newspaper has
consumed the room an illustration needs. Resolving it means giving something
up — a shorter weather panel, a tighter choice list, or accepting that the
picture is a light-day feature. Not resolved.

**Colour is unmeasured.** Everything in the colour table is a guess until the
calibration page runs.

**The nameplate is `הבוקר`.** Chosen because the paper outgrew "the morning
riddle" once it carried jokes and words of the day, because papers are named
with single nouns (הארץ, דבר, מעריב), and because `חידת הבוקר` measured two
pixels into the dateline at issue 100. A host test measures the name against
the widest dateline the page can produce so a future name cannot bring the
collision back.
