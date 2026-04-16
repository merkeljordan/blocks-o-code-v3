# Blocks o' Code v3 — Final Demo Script

Use this script with the slides (`slides.html`). Replace `[VIDEO: filename]` with your actual video file when presenting.

**Target: 15–20 min total**

---

## 1. Title (Slide 1) — ~0:30  
**Speaker: Jordan**

**Script**

“Hi everyone, thanks for being here. We’re the Blocks o’ Code v3 team. Our project is a physical, block-based programming system where you literally snap magnetic blocks together to build a program. There’s no keyboard, no traditional IDE—the physical arrangement of blocks *is* the code.”

---

## 2. The Problem (Slide 2) — ~0:45  
**Speaker: Camilla**

**Script**

“Programming is powerful, but for beginners it’s very abstract. You’re staring at an empty text editor, there’s no physical feedback, and concepts like loops or branches are invisible. It’s easy to get stuck before you ever see something real happen.  

Our goal for this semester was to make programming tangible: you build your program as a chain of physical blocks. The Brain block reads that arrangement, checks that it’s a valid ‘program’, and then executes it with real LEDs, audio, and a touch display—all in real time.”

---

## 3. Key Requirements (Slide 3) — ~0:45  
**Speaker: Annie**

**Script**

“For requirements, we focused on three big ideas.  

First, functionally: the system has to detect which blocks are connected and in what order, validate that sequence against our syntax rules, and then execute on command—driving LEDs, audio, and the display—with real‑time feedback in the companion app.  

Second, on the engineering side, we defined three quantitative specs we’ll show today: LED color preview latency under 50 milliseconds; config‑change‑to‑app latency under 2 seconds; and I2C rise time under 1000 nanoseconds. We will walk through the results of these specs later in the demo."

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

---

## 5. System Architecture (Slide 5) — ~0:45  
**Speaker: Jordan**

**Script**

“This is the full architecture. On the left, all of the child blocks share a single I2C bus. The Brain block in the middle acts as the I2C master, runs a config manager to scan and identify the current topology, and has an executor plus a TCP client running on the ESP32.  

On the right is our Flutter companion app. It runs a TCP server on a fixed port, receives JSON ‘block_config’ messages from the Brain, parses those into a Dart model, validates the program according to our grammar rules, and then updates the UI in real time—showing the block list, any errors, and a 3D view of the configuration.”

*(Optional, by Camilla, if you want)*  
“Physically, all of this rides on one I2C bus running at 100 kHz, with addresses allocated across the Brain and child blocks.”

---

## 6. Brain Block Deep Dive (Slide 6) — ~0:45  
**Speaker: Camilla**

**Script**

“Zooming in on the Brain: it continuously scans I2C addresses from 0x08 to 0x16 using a dedicated scan task. For each address that responds, it reads a ‘who‑am‑I’ register to learn what logical block type is present.  

The config manager compares the new scan to the previous one to detect adds, removes, or type changes, and when something changes it builds a JSON ‘block_config’ structure and caches that under a mutex. That change sets an event bit that wakes the TCP client task, which then streams the most recent JSON up to the app.  

From there the Brain can also send commands down to the child blocks—ping, set LED, execute, reset, and so on—so it fully controls execution over I2C.”

---

## 7. Flutter App — Validation (Slide 7) — ~0:45  
**Speaker: Jordan**

**Script**

“On the app side, every time we get a new JSON configuration we run it through a validation engine. We enforce four rules: the Brain must exist and be at index zero; If blocks must be followed by Button, Then, some outputs, and an End If; Loops must start at Loop and end at End Loop with only output blocks inside; and we warn on interleaved or overlapping structures.  

You can see examples on this slide: a valid loop sequence, an If without End If that is rejected, and a correct If with Button and Then. The app surfaces all of that in the UI—so if the physical chain is invalid, we show an error and we don’t let you execute.”

---

## 8. Demos Section Intro (Slide 8)

**Speaker: Jordan**

**Script**
“Now we will transition into our live demonstrations. We have several scenarios to show you to prove the varied features of the blocks and the app. First, before we get to the execution of code, we will show you our interactive walkthrough tutorial within the flutter app."

### Demo 1: Walkthrough Tutorial (Slide 9)

**Speaker: Jordan**

**[INTRODUCE]**
"The Flutter App includes a guided Walkthrough Tutorial, which is vital for beginners. This tutorial guides the user through building an `If` statement sequence, step by step, validating their block connections in real-time."

**[SHOW]**
**▶ PLAY: Walkthrough Tutorial — `demo-walkthrough-tutorial.mp4` (~1:00)**
"Here in the app, the user enters the tutorial mode. It visually demonstrates which block they need to connect next. As we snap the physical Brain, If, Button, Then, an Output block, and End If blocks together, the app dynamically acknowledges the correct block placement and advances the tutorial."

**[RESULTS]**
"This proves that our system can securely stream the network topology to the app to enforce interactive educational content, solving the core abstract programming problem we introduced at the start."

---

### Demo 2: All Output Block Functionalities (Slide 10)

**Speaker: Destiny**

**[INTRODUCE]**
"Now let's look at the output blocks: The LED Color Flash, the Note Block, and the Music Sequence block. This demo will illustrate how all three outputs can be programmed."

**[SHOW]**
**▶ PLAY: Output Blocks Demo — `demo-output-blocks.mp4` (~1:00)**
"First, we connect the LED Color Flash block, input a color on the TFT, and it immediately previews. Next, we swap it for a Note block. You select a specific frequency and duration on its display, and you'll hear the immediate audio preview. Finally, the Music Sequence block allows you to choose from a list of pre-programmed melodies. We snap the Brain block on and execute a quick sequence."

**[RESULTS]**
"This confirms that our block hardware is correctly receiving commands over I2C to manipulate the WS2812 LEDs and the LM386 audio amplifier."

---

### Demo 3: Loop Sequence (Slide 11)

**Speaker: Jordan**

**[INTRODUCE]**
"For our next demo, we'll demonstrate a control flow loop structure. We want a sequence of lights and sounds to repeat based on a count."

**[SHOW]**
**▶ PLAY: Loop Sequence — `demo-loop-sequence.mp4` (~0:45)**
"We connect the Brain to a Loop block. We configure the Loop block's TFT to iterate 3 times. We followed it with an LED Color Flash block, a Note block, and finally an End Loop block. The Flutter App acknowledges this is a valid Loop sequence. When we tap execute..."

**[RESULTS]**
"As you can see and hear, the Brain iterates through the child blocks exactly three times. The state management in the Brain's executor correctly maintains the program counter jump points for the loop structure."

---

### Demo 4: If Sequence (Slide 12)

**Speaker: Annie**

**[INTRODUCE]**
"Our next control flow demo is the conditional `If` statement, representing user interactivity."

**[SHOW]**
**▶ PLAY: If Sequence — `demo-if-sequence.mp4` (~0:45)**
"We assemble the chain: Brain, Then an If block, followed by a Button Press block. Then the 'Then' block, an LED output block, and an End If block. When we execute this, the Brain pauses execution, waiting for the Button block to be physically pressed."

**[RESULTS]**
"Once we press the button, the conditional is met, and the Brain proceeds to trigger the LED output block. This proves the system can block execution safely and handle input events before resuming."

---

### Demo 5: 15-Block Full System Stress Test (Slide 13)

**Speaker: Camilla**

**[INTRODUCE]**
"Our ultimate functionality test is pushing the system to its maximum capacity: 1 Brain and 14 functional child blocks on a single I2C bus."

**[SHOW]**
**▶ PLAY: 15 Block Test — `demo-15-block-sequence.mp4` (~1:30)**
"Here is the final configuration. It's a massive program: 
Brain -> Loop -> Note -> LED -> Endloop -> Delay -> Music 1 -> Music 2 -> If -> Button -> Then -> Note -> Note -> LED -> End If. 
Watch as the Flutter App correctly identifies all 15 blocks. We press execute, and the Brain effortlessly delegates the entire complex control flow and audio-visual sequence."

**[RESULTS]**
"Execution is flawless. This demonstrates robustness in our I2C bus address distribution, memory limits in the Brain Block firmware, and our real-time user-interface rendering in Flutter."

---

## 9. Specs Overview (Slide 14)

**Speaker: Destiny**

**[INTRODUCE]**
"Now we will review the engineering specifications that validate the performance of the system behind the scenes."

---

### Spec 1: LED Color Preview Latency (Slide 15)

**[INTRODUCE]**
"Spec 1 measured LED touch-to-preview latency, evaluating UX responsiveness. The target is under 50 milliseconds."

**[SHOW]**
**▶ PLAY: Spec 1 — Output Latency Run 1 — `spec1-run1.mp4` (~0:20)**
**▶ PLAY: Spec 1 — Output Latency Run 2 — `spec1-run2.mp4` (~0:20)**
**▶ PLAY: Spec 1 — Output Latency Run 3 — `spec1-run3.mp4` (~0:20)**
"We use a logic analyzer to measure the time delta between the SPI touch event and the WS2812 output pin transition."

**[RESULTS]**
"Our testing across 10 runs produced a mean of 13.1 milliseconds. We comfortably meet Spec 1."

---

### Spec 2: Config-Change-to-App Latency (Slide 16)

**[INTRODUCE]**
"Spec 2 measures the end-to-end pipeline latency from physically adding a block to the app UI updating, targeting under 2000 milliseconds."

**[SHOW]**
**▶ PLAY: Spec 2 — Sync Latency Run 1 — `spec2-run1.mp4` (~0:20)**
**▶ PLAY: Spec 2 — Sync Latency Run 2 — `spec2-run2.mp4` (~0:20)**
**▶ PLAY: Spec 2 — Sync Latency Run 3 — `spec2-run3.mp4` (~0:20)**

**[RESULTS]**
"Out of 10 runs, our mean latency was approximately 50 milliseconds, passing this requirement easily."

---

### Spec 3: I2C Rise Time with 15 Blocks (Slide 17)

**Speaker: Camilla**

**[INTRODUCE]**
"Spec 3 evaluates the electrical integrity of our shared communication bus. We measure the 10-to-90% rise time on the SCL line. Crucially, we are now measuring this under the absolute maximum bus load—with all 15 blocks connected, stressing the line capacitance."

**[SHOW]**
**▶ PLAY: Spec 3 — 15 Blocks Run 1 — `spec3-run1.mp4` (~0:20)**
**▶ PLAY: Spec 3 — 15 Blocks Run 2 — `spec3-run2.mp4` (~0:20)**
**▶ PLAY: Spec 3 — 15 Blocks Run 3 — `spec3-run3.mp4` (~0:20)**
"We use an oscilloscope probing the I2C header at the Brain Block during a full standard scan sequence."

**[RESULTS]**
"Our target, keeping within standard I2C operating mode safety margins, was under 1000 nanoseconds. Across 10 runs with 15 blocks attached, our mean rise time was 289ns. We met Spec 3 with plenty of margin, ensuring data stability even at max capacity."

---

## 10. Closing (Slide 18) — ~0:45  
**Speakers: Annie + Jordan**

**Annie (first ~0:25)**
“To wrap up: we have successfully demonstrated the core of the Blocks O' Code system. The 15 magnetic blocks, the complex conditional logic, the interactive outputs, and the responsive cross-platform application integrate seamlessly.”

**Jordan (last ~0:20)**
“Our architecture safely handles massive configurations, the electrical buses operate within spec under full load, and beginners now have a tangible, immediate bridge to entering the programming world. Next steps mainly involve producing the finalized PCBs and enclosure polish. 

Thanks for listening—we’re happy to take any questions.”

---

## Video Checklist

| # | Description | Suggested filename | Est. length | Status |
|---|-------------|--------------------|-------------|--------|
| 1 | Demo — Walkthrough Tutorial | `demo-walkthrough-tutorial.mp4` | ~1:00 | **NEW** |
| 2 | Demo — Outut Blocks | `demo-output-blocks.mp4` | ~1:00 | **NEW** |
| 3 | Demo — Loop Sequence | `demo-loop-sequence.mp4` | ~0:45 | **NEW** |
| 4 | Demo — If Sequence | `demo-if-sequence.mp4` | ~0:45 | **NEW** |
| 5 | Demo — 15 Block Test | `demo-15-block-sequence.mp4` | ~1:30 | **NEW** |
| 6 | Spec 1 — Output Latency run 1 | `spec1-run1.mp4` | ~0:25 | **REUSED** |
| 7 | Spec 1 — Output Latency run 2 | `spec1-run2.mp4` | ~0:25 | **REUSED** |
| 8 | Spec 1 — Output Latency run 3 | `spec1-run3.mp4` | ~0:25 | **REUSED** |
| 9 | Spec 2 — Sync Latency run 1 | `spec2-run1.mp4` | ~0:25 | **REUSED** |
| 10 | Spec 2 — Sync Latency run 2 | `spec2-run2.mp4` | ~0:25 | **REUSED** |
| 11 | Spec 2 — Sync Latency run 3 | `spec2-run3.mp4` | ~0:25 | **REUSED** |
| 12 | Spec 3 — I2C rise time (15 blocks), run 1 | `spec3-run1.mp4` | ~0:25 | **NEW** |
| 13 | Spec 3 — I2C rise time (15 blocks), run 2 | `spec3-run2.mp4` | ~0:25 | **NEW** |
| 14 | Spec 3 — I2C rise time (15 blocks), run 3 | `spec3-run3.mp4` | ~0:25 | **NEW** |
