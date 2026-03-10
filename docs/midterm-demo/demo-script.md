# Blocks o' Code v3 — Midterm Demo Script

Use this script with the slides (`slides.html`). Replace `[VIDEO: filename]` with your actual video file when presenting. Suggested folder: `docs/midterm-demo/videos/` (create if needed).

**Target: 10–15 min total** · *Approximate total below: ~12–15 min*

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

*Total ~12–15 min if you keep talk and videos tight. Use short video clips (~25 s per spec run, ~45 s per scenario); show Scenario 1 & 2 right after Slide 8 to save time. Buffer ~1 min for transitions.*

---

# Slide/Topic Role Assignments (Midterm Demo)

> People + strengths  
- **Jordan** — Flutter app, validation, TCP/events, UI updates  
- **Destiny** — displays/peripherals (TFT, LEDs, speaker, demo “what you see/hear”)  
- **Annie** — hardware (and **3D enclosure**)  
- **Camilla** — hardware (I2C / electrical / system integration)

---

## 1. Title (Slide 1) — ~0:30  
**Speaker: Jordan**

**Script**

“Hi everyone, thanks for being here. We’re the Blocks o’ Code v3 team. Our project is a physical, block-based programming system where you literally snap magnetic blocks together to build a program. There’s no keyboard, no traditional IDE—the physical arrangement of blocks *is* the code.”

*[Advance to Slide 2]*

---

## 2. The Problem (Slide 2) — ~0:45  
**Speaker: Camilla**

**Script**

“Programming is powerful, but for beginners it’s very abstract. You’re staring at an empty text editor, there’s no physical feedback, and concepts like loops or branches are invisible. It’s easy to get stuck before you ever see something real happen.  

Our goal for this semester was to make programming tangible: you build your program as a chain of physical blocks. The Brain block reads that arrangement, checks that it’s a valid ‘program’, and then executes it with real LEDs, audio, and a touch display—all in real time.”

*[Advance to Slide 3]*

---

## 3. Key Requirements (Slide 3) — ~0:45  
**Speaker: Annie**

**Script**

“For requirements, we focused on three big ideas.  

First, functionally: the system has to detect which blocks are connected and in what order, validate that sequence against our syntax rules, and then execute on command—driving LEDs, audio, and the display—with real‑time feedback in the companion app.  

Second, on the engineering side, we defined three quantitative specs we’ll show today: LED color preview latency under 50 milliseconds; config‑change‑to‑app latency under 2 seconds; and I2C rise time under 1000 nanoseconds. For each spec, we collected 10‑run data and we also have three short live recordings.”

*[Advance to Slide 4]*

---

## 4. The 15 Blocks (Slide 4) — ~1:00  
**Speakers: Annie + Destiny (split)**

**Annie**

“All 15 blocks share a common hardware platform. Each one has an ESP32 microcontroller, a 2.8‑inch TFT touch display, a 4×4 LED matrix, addressable LEDs, and an audio path with an amplifier and speaker, all powered through rechargable lithium-ion batteries.  

At the system level we have one Brain block and up to 14 child blocks. The Brain is the I2C master and runs the program executor; the child blocks are all identical from a hardware perspective, but they present themselves as different logical block types over I2C.”

**Destiny**

“Those logical types fall into three main categories.  

We have **control‑flow blocks**—If, Then, End If, Loop, End Loop, and Delay—which shape the structure of the program but don’t directly produce output. We have a single **input block**, a Button, which gives us user interaction for If statements. And we have **output blocks** like LED Color Flash, Note, and Music Sequence.  

From the user’s point of view, you snap a chain together, tap the TFT on each block to choose its behavior, and the LEDs and speaker give immediate visual and audio feedback.”

*[Advance to Slide 5]*

---

## 5. System Architecture (Slide 5) — ~0:45  
**Speaker: Jordan**

**Script**

“This is the full architecture. On the left, all of the child blocks share a single I2C bus. The Brain block in the middle acts as the I2C master, runs a config manager to scan and identify the current topology, and has an executor plus a TCP client running on the ESP32.  

On the right is our Flutter companion app. It runs a TCP server on a fixed port, receives JSON ‘block_config’ messages from the Brain, parses those into a Dart model, validates the program according to our grammar rules, and then updates the UI in real time—showing the block list, any errors, and a 3D view of the configuration.”

*(Optional, by Camilla, if you want)*  
“Physically, all of this rides on one I2C bus running at 100 kHz, with addresses allocated across the Brain and child blocks.”

*[Advance to Slide 6]*

---

## 6. Brain Block Deep Dive (Slide 6) — ~0:45  
**Speaker: Camilla**

**Script**

“Zooming in on the Brain: it continuously scans I2C addresses from 0x08 to 0x16 using a dedicated scan task. For each address that responds, it reads a ‘who‑am‑I’ register to learn what logical block type is present.  

The config manager compares the new scan to the previous one to detect adds, removes, or type changes, and when something changes it builds a JSON ‘block_config’ structure and caches that under a mutex. That change sets an event bit that wakes the TCP client task, which then streams the most recent JSON up to the app.  

From there the Brain can also send commands down to the child blocks—ping, set LED, execute, reset, and so on—so it fully controls execution over I2C.”

*[Advance to Slide 7]*

---

## 7. Flutter App — Validation (Slide 7) — ~0:45  
**Speaker: Jordan**

**Script**

“On the app side, every time we get a new JSON configuration we run it through a validation engine. We enforce four rules: the Brain must exist and be at index zero; If blocks must be followed by Button, Then, some outputs, and an End If; Loops must start at Loop and end at End Loop with only output blocks inside; and we warn on interleaved or overlapping structures.  

You can see examples on this slide: a valid loop sequence, an If without End If that is rejected, and a correct If with Button and Then. The app surfaces all of that in the UI—so if the physical chain is invalid, we show an error and we don’t let you execute.”

*[Advance to Slide 8]*

---

## 8. Demo Intro — Two Scenarios & 3D Enclosure (Slide 8) — ~1:15  
**Speakers: Jordan + Annie**

**Jordan (intro, ~0:20–0:30)**

“For the live demo we’re focusing on two simple scenarios that use the same pipeline: Brain plus an LED Color Flash block, and Brain plus a Music Sequence block. In both cases, the Brain scans the configuration, the app validates it, and then we execute from the Brain’s TFT.  

We’ll start by showing the 3D enclosure that will house the blocks, and then we’ll walk through those two scenarios.”

**▶ PLAY: 3D Block Enclosure — `3d-enclosure.mp4` (~0:45)**  
**Annie (over video, short lines)**

“This is the enclosure we designed for the system. It’s sized to hold the Brain and child blocks securely, with cutouts for the TFT, LEDs, and power.  

We designed it to keep the blocks aligned mechanically while still making the connections and the user interaction really visible.”

*[Advance or segue into scenarios]*

---

## 9. Demo Videos — Scenario 1 & Scenario 2 — ~1:45  
**Speakers: Jordan + Destiny**  

*(Use this block right after Slide 8, or later after specs.)*

**Jordan (before Scenario 1)**

“Next we’ll show how the system behaves with two concrete programs. First is Brain plus an LED Color Flash block.

“Here we connect the Brain and an LED Color Flash block. We first configure the LED color flash block by choosing a pattern from the keypad and submitting the one we want.
The Brain scans the I2C bus, sends the new configuration to the app, and you can see the app update to show a valid sequence.  

From there we tap Execute on the Brain’s TFT, which kicks off a `CMD_EXECUTE` to the LED block.”

“As soon as we execute, the LED block plays the programmed flash pattern on the matrix and addressable LEDs. That’s the user’s immediate visual feedback that their physical ‘program’ is running.”

**Destiny (before Scenario 2)**

“The second scenario is Brain plus a Music Sequence block.”
Just like the LED block, we must choose a music sequence to set the block to.
“Again we snap the blocks together, the Brain detects the new configuration, and the app shows a valid sequence. We start execution from the TFT, and the Brain sends an execute command down to the Music Sequence block.”
“In this case the output is audio instead of light—you’ll hear the programmed tune play through the speaker. Mechanically and in software it’s the same pipeline; just a different type of child block.”

---

## 10. End-to-End Execution Flow (Slide 9) — ~0:30  
**Speaker: Jordan**

**Script**

“This slide summarizes that pipeline for any pair of child blocks. The user snaps the blocks together; the Brain scans I2C and builds a configuration; it sends JSON to the Flutter app; the app validates and shows either ‘valid’ or errors; once it’s valid, the user taps Execute on the Brain’s TFT, and the Brain issues `CMD_EXECUTE` to each child block in order, while also sending execution status back to the app.”

*[Advance to Slide 10]*

---

## 11. Engineering Specs Overview (Slide 10) — ~0:30  
**Speaker: Annie**

**Script**

“Here are the three engineering specs we committed to.  

Spec 1 is LED color select to preview latency on the LED Color Flash block, target mean under 50 milliseconds. Spec 2 is physical config‑change to app latency, target mean under 2000 milliseconds. Spec 3 is I2C SCL/SDA rise time at the Brain’s I2C header, target under 1000 nanoseconds.  

For each one, we ran 10 trials to get mean, standard deviation, min, and max, and we captured three short live recordings that we’ll play next.”

*[Advance to Slide 11]*

---

## 12. Spec 1 — LED Color Preview Latency (Slide 11) — ~1:45  
**Speaker: Destiny**

**Script**

“Spec 1 measures how responsive the LED Color Flash block feels. The quantity is the time from tapping a new color on the TFT to seeing the LED matrix actually change to that color.  

We measured this with a logic analyzer by capturing the SPI traffic from the touch event and the WS2812 data line to the matrix. Our target was a mean latency of 50 milliseconds or less; the 10‑run data on the slide shows a mean around 13 milliseconds, so we’re comfortably inside spec.  

We’ll show three short runs so you can see that behavior.”

**▶ PLAY: Spec 1 — Live recording 1 — `spec1-run1.mp4` (~0:25)**  
“Watch the moment we tap the TFT and how quickly the LED matrix updates—that’s the latency we’re measuring.”

**▶ PLAY: Spec 1 — Live recording 2 — `spec1-run2.mp4` (~0:25)**  
“Again, touch to color change stays in that low‑teens millisecond range.”

**▶ PLAY: Spec 1 — Live recording 3 — `spec1-run3.mp4` (~0:25)**  
“Across all three runs and the 10‑run data set, Spec 1 is clearly met.”

*[Advance to Slide 12]*

---

## 13. Spec 2 — Config-Change-to-App Latency (Slide 12) — ~1:45  
**Speaker: Jordan**

**Script**

“Spec 2 looks at the responsiveness of the entire end‑to‑end pipeline when the physical topology changes. We start a timer when we physically add or remove a block, and we stop it when the app UI shows the new configuration.  

That includes I2C scan on the Brain, config diff, JSON serialization, TCP send, parsing on the Flutter side, and rendering the updated UI. Our target was a mean of 2 seconds or less given the amount of processing needed; the 10‑run table here shows a mean of roughly 50 milliseconds, so again we’re well below the target.  

Here are three quick runs.”

**▶ PLAY: Spec 2 — Live recording 1 — `spec2-run1.mp4` (~0:25)**  
“Watch for when we plug or unplug the block, and then how quickly the app reflects that change.”

**▶ PLAY: Spec 2 — Live recording 2 — `spec2-run2.mp4` (~0:25)**  
“In each run, the app tracks the physical chain almost immediately.”

**▶ PLAY: Spec 2 — Live recording 3 — `spec2-run3.mp4` (~0:25)**  
“With all runs considered, Spec 2 is also met.”

*[Advance to Slide 13]*

---

## 14. Spec 3 — I2C Rise Time (Slide 13) — ~1:45  
**Speaker: Camilla**

**Script**

“Spec 3 focuses on electrical integrity of the I2C bus with one child block connected. We measure the 10‑to‑90% rise time on SCL at the Brain block I2C header while the system is scanning.  

Too‑slow edges can violate the timing budget, especially as you add more devices and bus capacitance. Our target was a rise time of 1000 nanoseconds or less. The 10‑run results here show a mean around 289 nanoseconds, with all measurements under 300 nanoseconds—so our pull‑ups and bus loading are in a very safe range.  

We’ll show three captures from the oscilloscope.”

**▶ PLAY: Spec 3 — Live recording 1 — `spec3-run1.mp4` (~0:25)**  
“On this trace, the scope is automatically measuring the 10–90% rise time for SCL.”

**▶ PLAY: Spec 3 — Live recording 2 — `spec3-run2.mp4` (~0:25)**  
“You can see that even under typical scan activity, the rise stays well under the 1000‑nanosecond limit.”

**▶ PLAY: Spec 3 — Live recording 3 — `spec3-run3.mp4` (~0:25)**  
“Across all runs, Spec 3 is met with plenty of margin.”

*[Advance to Slide 14]*

---

## 15. Closing (Slide 14) — ~0:45  
**Speakers: Annie + Jordan**

**Annie (first ~0:25)**

“To wrap up: on the hardware side we’ve built the Brain and child block platform, integrated all the peripherals—TFT, LEDs, audio—and designed the 3D enclosure that will house the system. The blocks snap together magnetically on a shared I2C bus, and the Brain reliably detects and represents that topology.”

**Jordan (last ~0:20)**

“On the software side, we’ve implemented the JSON/TCP link to the Flutter app, the validation engine that enforces our grammar rules, and the execution path that drives LEDs and audio from a physical ‘program’. We also have implemeneted 3 vital blocks, the brain, the led color flash, and the music sequence block. All three engineering specs are met with measured data and live recordings. Next steps are full execution support for all block types, final PCBs, and polishing both the app UI and the TFT interfaces.  

Thanks for listening—we’re happy to take any questions.”

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

**Total: 12 videos** (1 enclosure + 2 demo scenarios + 9 spec recordings) · **Total video time: ~6 min** (keep clips tight for a 10–15 min demo)
