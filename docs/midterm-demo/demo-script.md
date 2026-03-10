# Blocks o' Code v3 — Midterm Demo Script

Use this script with the slides (`slides.html`). Replace `[VIDEO: filename]` with your actual video file when presenting. Suggested folder: `docs/midterm-demo/videos/` (create if needed).

**Target: 10–15 min total** · *Approximate total below: ~12 min*

---

## Time summary

| Section | Slide | Talk | Videos | Section total | Cumulative |
|---------|-------|------|--------|---------------|------------|
| Title | 1 | 0:30 | — | 0:30 | 0:30 |
| The Problem | 2 | 0:45 | — | 0:45 | 1:15 |
| Key Requirements | 3 | 0:45 | — | 0:45 | 2:00 |
| The 15 Blocks | 4 | 1:00 | — | 1:00 | 3:00 |
| System Architecture | 5 | 0:45 | — | 0:45 | 3:45 |
| Brain Deep Dive | 6 | 0:45 | — | 0:45 | 4:30 |
| Flutter Validation | 7 | 0:45 | — | 0:45 | 5:15 |
| Demo Intro + 3D enclosure | 8 | 0:30 | ~0:45 | 1:15 | 6:30 |
| Execution Flow | 9 | 0:30 | — | 0:30 | 7:00 |
| Specs Overview | 10 | 0:30 | — | 0:30 | 7:30 |
| Spec 1 + 3 runs | 11 | 0:30 | ~1:15 (3×~0:25) | 1:45 | 9:15 |
| Spec 2 + 3 runs | 12 | 0:30 | ~1:15 (3×~0:25) | 1:45 | 11:00 |
| Spec 3 + 3 runs | 13 | 0:30 | ~1:15 (3×~0:25) | 1:45 | 12:45 |
| Demo Scenario 1 & 2 | — | 0:15 | ~1:30 (2×~0:45) | 1:45 | 14:30 |
| Closing | 14 | 0:45 | — | 0:45 | 15:15 |

*Total ~12–15 min if you keep talk and videos tight. Use short video clips (~25 s per spec run, ~45 s per scenario); show Scenario 1 & 2 right after Slide 8 (same “demo” block) to save time. Buffer ~1 min for transitions.*

---

## 1. Title (Slide 1) — ~0:30

**[Speaker]**  
"Thanks for having us. We're [names] from Blocks o' Code v3 — a physical, block-based programming system. You snap magnetic blocks together to build real programs: no keyboard, no screen. The physical arrangement *is* the code."

*[Advance to Slide 2]*

---

## 2. The Problem (Slide 2) — ~0:45

**[Speaker]**  
"Programming is abstract and inaccessible for many beginners. Our solution is to make it tangible: users snap blocks together, and the system executes with real LEDs, audio, and a touch display. Our midterm goal is a working system where blocks snap together, the Brain reads the arrangement, validates it, and executes — all in real time."

*[Advance to Slide 3]*

---

## 3. Key Requirements (Slide 3) — ~0:45

**[Speaker]**  
"We have functional requirements — detect and validate block order, execute on command, real-time feedback, control flow and output blocks — and three engineering specs we're measuring: LED color preview latency under 50 ms, config-change-to-app under 2 seconds, and I2C rise time under 1000 ns. Each spec has 10-run data plus three live demo recordings."

*[Advance to Slide 4]*

---

## 4. The 15 Blocks (Slide 4) — ~1:00

**[Speaker]**  
"All 15 blocks share the same hardware platform: ESP32, TFT touch display, USB-C, LED matrix, addressable LEDs, and speaker. We have one Brain — I2C master and program executor — six control-flow blocks, one input block (Button), and seven output blocks including LED Color Flash, Note, and Music Sequence. They communicate over a single I2C bus."

*[Advance to Slide 5]*

---

## 5. System Architecture (Slide 5) — ~0:45

**[Speaker]**  
"Child blocks sit on one side; the Brain in the middle does I2C master, config manager, executor, and TCP client; the Flutter app on the right runs the TCP server, validator, and real-time UI. Config flows over Wi-Fi as JSON on port 41233."

*[Advance to Slide 6]*

---

## 6. Brain Block Deep Dive (Slide 6) — ~0:45

**[Speaker]**  
"The Brain runs a scan task over I2C addresses 0x08–0x16, the config manager diffs against the previous state, and on change it sends JSON to the app. It sends commands to child blocks: ping, set LED, execute, reset, get data, matrix brightness."

*[Advance to Slide 7]*

---

## 7. Flutter App — Validation (Slide 7) — ~0:45

**[Speaker]**  
"The app enforces four grammar rules: Brain at index zero, valid If/Then/End If and Loop/End Loop sequences. We show valid and invalid examples here; the app flags errors and blocks execution when the sequence is invalid."

*[Advance to Slide 8]*

---

## 8. Demo Intro — Two Scenarios & 3D Enclosure (Slide 8) — ~1:15 (talk ~0:30 + video ~0:45)

**[Speaker]**  
"For the demo we're showing two scenarios with two blocks each, plus our 3D block enclosure. First we'll show the enclosure we've designed and built, then Scenario 1: Brain plus LED Color Flash — connect, app detects, execute, LEDs flash. Then Scenario 2: Brain plus Music Sequence — connect, app detects, execute, music plays. We'll also show real-time validation and what happens with an invalid config."

**▶ PLAY: 3D Block Enclosure** — *~0:45*  
**[PLAY VIDEO: `3d-enclosure.mp4`]**  
*(Show the physical 3D block enclosure — design and build.)*

**[Speaker]**  
"That's the enclosure that will house our blocks."

*[Advance to Slide 9]*

---

## 9. End-to-End Execution Flow (Slide 9) — ~0:30

**[Speaker]**  
"Walk through the flow: user snaps blocks, Brain scans I2C and sends block_config JSON to the app, app shows valid, user presses Execute on the TFT, Brain sends CMD_EXECUTE to each child block in order, and the app UI updates. Same pipeline for both demo scenarios."

*[Advance to Slide 10]*

---

## 10. Engineering Specs Overview (Slide 10) — ~0:30

**[Speaker]**  
"We have three engineering specs: LED color preview latency — target mean under 50 ms; config-change-to-app — under 2 seconds; and I2C rise time — under 1000 ns. For each we have 10-run data and three live recordings we'll play."

*[Advance to Slide 11]*

---

## 11. Spec 1 — LED Color Preview Latency (Slide 11) — ~1:45 (talk ~0:30 + 3 videos ~1:15)

**[Speaker]**  
"Spec 1: time from TFT touch color selection to LED matrix color change. Target: mean ≤ 50 ms. We measured with a logic analyzer — touch event to WS2812 data transition. Our 10-run data is on the slide; mean is about 13.1 ms, so we meet the spec. Here are three live runs."

**▶ PLAY: Spec 1 — Live recording 1** — *~0:25*  
**[PLAY VIDEO: `spec1-run1.mp4`]**

**▶ PLAY: Spec 1 — Live recording 2** — *~0:25*  
**[PLAY VIDEO: `spec1-run2.mp4`]**

**▶ PLAY: Spec 1 — Live recording 3** — *~0:25*  
**[PLAY VIDEO: `spec1-run3.mp4`]**

**[Speaker]**  
"All three runs show sub-50 ms response. Spec 1 met."

*[Advance to Slide 12]*

---

## 12. Spec 2 — Config-Change-to-App Latency (Slide 12) — ~1:45 (talk ~0:30 + 3 videos ~1:15)

**[Speaker]**  
"Spec 2: from physical block add or remove to the app UI showing the new topology. Target: mean ≤ 2000 ms. This exercises the full pipeline — I2C scan, config diff, JSON, TCP, parse, UI. Our 10-run mean is about 50 ms. Three live runs."

**▶ PLAY: Spec 2 — Live recording 1** — *~0:25*  
**[PLAY VIDEO: `spec2-run1.mp4`]**

**▶ PLAY: Spec 2 — Live recording 2** — *~0:25*  
**[PLAY VIDEO: `spec2-run2.mp4`]**

**▶ PLAY: Spec 2 — Live recording 3** — *~0:25*  
**[PLAY VIDEO: `spec2-run3.mp4`]**

**[Speaker]**  
"Config change to app is well under 2 seconds. Spec 2 met."

*[Advance to Slide 13]*

---

## 13. Spec 3 — I2C Rise Time (Slide 13) — ~1:45 (talk ~0:30 + 3 videos ~1:15)

**[Speaker]**  
"Spec 3: I2C SCL rise time at the Brain block header, 10%–90%. Target: ≤ 1000 ns for standard mode. We measured with an oscilloscope during normal scan. Mean about 289 ns, so we're well within spec. Three live scope recordings."

**▶ PLAY: Spec 3 — Live recording 1** — *~0:25*  
**[PLAY VIDEO: `spec3-run1.mp4`]**

**▶ PLAY: Spec 3 — Live recording 2** — *~0:25*  
**[PLAY VIDEO: `spec3-run2.mp4`]**

**▶ PLAY: Spec 3 — Live recording 3** — *~0:25*  
**[PLAY VIDEO: `spec3-run3.mp4`]**

**[Speaker]**  
"Rise time is under 1000 ns. Spec 3 met."

*[Advance to Slide 14]*

---

## 14. Demo Videos — Scenario 1 & Scenario 2 (after Spec 3 or with Slide 8)

*If you prefer to show the two scenario demos right after the Demo Intro (Slide 8), use this block there. Otherwise you can show them after Spec 3 or in a separate “live demo” segment.*

**[Speaker]**  
"Now the two demo scenarios. First: Brain and LED Color Flash."

**▶ PLAY: Demo — Scenario 1 (Brain → LED Color Flash)**  
**[PLAY VIDEO: `demo-scenario1-led-flash.mp4`]**  
*(Connect Brain + LED Color Flash; show app detecting config and VALID; press Execute on TFT; LEDs flash.)*

**[Speaker]**  
"Second: Brain and Music Sequence."

**▶ PLAY: Demo — Scenario 2 (Brain → Music Sequence)**  
**[PLAY VIDEO: `demo-scenario2-music-sequence.mp4`]**  
*(Connect Brain + Music Sequence; show app detecting config and VALID; press Execute; music plays.)*

**[Speaker]**  
"Same flow for both: snap blocks, app validates, execute on the Brain’s TFT, and the child block runs."

---

## 15. Closing (Slide 14) — ~0:45

**[Speaker]**  
"What we built: the 3D block enclosure, two demo scenarios with two blocks each — Brain to LED Color Flash and Brain to Music Sequence — ESP32 Brain and I2C topology detection, JSON over TCP to the Flutter app, grammar validation, and execution with LEDs and audio. All three engineering specs are met with 10-run data and three live recordings each. Next we’re doing final PCB assembly, full execution for all block types, finalized mechanical enclosure, and app and TFT UI updates. The core idea is unchanged: the physical arrangement of blocks *is* the program — validated, executed, and tangible. Thanks; we’re happy to take questions."

---

## Video checklist

| # | Description | Suggested filename | Est. length |
|---|-------------|--------------------|-------------|
| 1 | 3D block enclosure | `3d-enclosure.mp4` | ~0:45 |
| 2 | Demo — Scenario 1: Brain → LED Color Flash | `demo-scenario1-led-flash.mp4` | ~0:45 |
| 3 | Demo — Scenario 2: Brain → Music Sequence | `demo-scenario2-music-sequence.mp4` | ~0:45 |
| 4 | Spec 1 — LED preview latency, run 1 | `spec1-run1.mp4` | ~0:25 |
| 5 | Spec 1 — LED preview latency, run 2 | `spec1-run2.mp4` | ~0:25 |
| 6 | Spec 1 — LED preview latency, run 3 | `spec1-run3.mp4` | ~0:25 |
| 7 | Spec 2 — Config-to-app latency, run 1 | `spec2-run1.mp4` | ~0:25 |
| 8 | Spec 2 — Config-to-app latency, run 2 | `spec2-run2.mp4` | ~0:25 |
| 9 | Spec 2 — Config-to-app latency, run 3 | `spec2-run3.mp4` | ~0:25 |
| 10 | Spec 3 — I2C rise time, run 1 | `spec3-run1.mp4` | ~0:25 |
| 11 | Spec 3 — I2C rise time, run 2 | `spec3-run2.mp4` | ~0:25 |
| 12 | Spec 3 — I2C rise time, run 3 | `spec3-run3.mp4` | ~0:25 |

**Total: 12 videos** (1 enclosure + 2 demo scenarios + 9 spec recordings) · **Total video time: ~6 min** (keep clips tight for 10–15 min demo)
