// Morning Riddle: the paper's name.
//
// IT LIVES IN CORE SO IT CAN BE MEASURED. The nameplate shares one 372px line
// with the dateline, and the two are drawn from opposite ends -- the name
// right-to-left from the right margin, the dateline anchored at the left. Get
// the widths wrong and they overlap, silently, on some future Wednesday with a
// three-digit issue number.
//
// That is not hypothetical. "חידת הבוקר" measured 149px against a 225px
// dateline, which put the nameplate's left edge TWO PIXELS inside the
// dateline's right edge. Invisible at issue 1 and a collision at issue 100.
// tests/run_tests.c measures both against the real font blobs so the next
// name cannot reintroduce it.
//
// WHY "הבוקר" AND NOT "חידת הבוקר". Beyond the two pixels: the paper carries
// riddles, jokes and words of the day now, so a name meaning "the morning
// riddle" is narrower than its own contents -- the kicker above the lead
// exists precisely to say "this one is not a riddle", which is a nameplate
// problem leaking into the body. And it is how papers are actually named:
// הארץ, דבר, מעריב are single evocative nouns, not descriptions of contents.
// הבוקר was itself an Israeli daily from 1935 to 1965.

#ifndef MASTHEAD_H
#define MASTHEAD_H

// "the morning"
#define MASTHEAD_NAME "\xd7\x94\xd7\x91\xd7\x95\xd7\xa7\xd7\xa8"

// The gap the nameplate must keep from the dateline, at the widest dateline
// the page can produce. Not zero: two glyphs one pixel apart read as one word.
#define MASTHEAD_MIN_GAP 24

#endif // MASTHEAD_H
