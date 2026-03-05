# Midterm Demo: Rehearsal and Recording Checklist

Use this checklist before and during rehearsal, then again on recording day. The team should rehearse at least once with a timer and fix any timing or flow issues before the final take.

---

## Before Rehearsal: Content Readiness

- **Subsystem/block diagram** finalized and exported for slides or overlay (see [visuals.md](visuals.md)).
- **Three engineering specs** defined with targets (see [engineering-specifications.md](engineering-specifications.md)).
- **10 runs per spec** completed; data recorded in [spec-test-data-template.md](spec-test-data-template.md); mean, std dev, min, max computed.
- **Summary tables/figures** for Part 3 created (use tables in visuals.md or export from spreadsheet).
- **Script** read through and adjusted for your team’s wording (see [demo-script.md](demo-script.md)).
- **Measurement setup for Spec 3** (I2C rise time) defined and documented; 10 captures taken and summarized.

---

## Before Rehearsal: Demo Readiness

- **Firmware** built and flashed on Brain Block; Wi‑Fi and server IP/port correct.
- **Companion app** runs and listens on port 41233; IP shown in app or known.
- **Child blocks** (at least 2) connected on I2C and responding; addresses known.
- **Spec 1 measurement** method working (LED Color Flash preview latency: scope/logic analyzer probing numpad key + LED data pin preferred; high-fps video acceptable for midterm).
- **Spec 2 measurement** method ready (stopwatch or app-side timestamp for “config received”).
- **Spec 3** measurement method for I2C rise time verified (scope or equivalent) and comfortable capturing 10 runs + 3 live in recording.
- **Three live runs per spec** can be performed reliably (no one-off hacks that might fail on camera).

---

## Rehearsal: Run-Through

- Assign **who speaks** for each part (Part 1, 2, 3, 4).
- **Time each part** with a stopwatch; total must be 10–15 minutes.
  - Part 1: _____ min  
  - Part 2: _____ min  
  - Part 3: _____ min  
  - Part 4: _____ min  
  - **Total: _____ min**
- If over 15 min: trim script (especially Part 1 or 2) or shorten live demos.
- If under 10 min: add a sentence or two of context, or show one more validation example.
- **Part 2**: Confirm add/remove block demo works and app updates within expected time.
- **Part 3**: Perform at least one full cycle of “summary table → 3 live runs” per spec; note any glitches.
- **Slides/overlays**: Confirm all diagrams and tables display correctly and are readable on camera.

---

## Recording Day: Setup

- **Camera**: Position for hardware close-ups and screen capture (or separate screen recording).
- **Audio**: Test mic levels; minimize room noise and fan/AC.
- **Lighting**: Enough light on hardware and presenters; no glare on screens.
- **Desk**: Hardware, cables, and blocks arranged so add/remove for Spec 2 is easy and visible.
- **Serial monitor / instruments**: Visible on screen for Spec 1 (and Spec 2 if using timestamps); font size readable.
- **App window**: Visible and large enough to show block list and validation messages.

---

## Recording Day: Final Checks

- Brain Block and app **reconnected** and stable.
- **Slides/overlays** open and ready (title card, diagram, spec summary table, etc.).
- **Script** or cue cards in view (off-camera or on second monitor).
- **Timer** ready to keep track during recording.
- **Spec test data** (10-run summaries) and “3 live runs” procedure clear to the person doing Part 3.

---

## During Recording

- **Part 1**: Show title card and diagram; speak to script; stay within 2–3 min.
- **Part 2**: Start app, then power Brain; show connection and block list; do add/remove and show validation; stay within 4–6 min.
- **Part 3**: Intro the three specs; for each spec: show summary table, then perform 3 live runs and call out each result; state whether target is met. Stay within 4–6 min.
- **Part 4**: One-sentence summary, remaining work, thanks; 30–60 s.
- **Total**: Confirm final duration is between 10 and 15 minutes.

---

## After Recording

- Review playback: audio clear, all key steps visible, spec numbers readable.
- If breadboard/dev board was used, confirm you stated it (e.g. in Part 1 or 2).
- Export and submit in the format required by your course (e.g. single video file, link, or upload).

---

## Quick Reference: File Locations


| Item                      | File                                                                         |
| ------------------------- | ---------------------------------------------------------------------------- |
| Spec definitions          | [engineering-specifications.md](engineering-specifications.md)               |
| Data collection (10 runs) | [spec-test-data-template.md](spec-test-data-template.md)                     |
| Diagrams and tables       | [visuals.md](visuals.md)                                                     |
| Narration script          | [demo-script.md](demo-script.md)                                             |
| This checklist            | [rehearsal-and-recording-checklist.md](rehearsal-and-recording-checklist.md) |


