# Midterm Demo Video Script (10–15 minutes)

Fill in **[bracketed placeholders]** with your team's actual names, numbers, and status. Timings are approximate — calibrate in rehearsal. Each slide cue is marked **[SLIDE]**; each camera/recording cue is marked **[CAMERA]**.

---

## Part 1: Intro & System Overview (2–3 min)

---

### 1.1 Title Slide (15–20 s)

**[SLIDE: Title — project name, course, team name, member names + roles]**

> "Hi, we're [Team Name]. This is our midterm demo for Blocks o' Code v3. We'll give you an overview of our subsystems, walk through a live demo scenario, and then show three key engineering specifications backed by test data. Let's get into it."

---

### 1.2 Problem & Goal (30–45 s)

**[SLIDE: Problem statement + one-line goal]**

> "The problem we're solving: programming is abstract and inaccessible to beginners, especially young learners. Our solution is a physical, block-based programming system. Users snap together magnetic blocks—control flow, inputs, and outputs—to build programs. The brain of the system reads the physical arrangement and executes it. The goal is to make programming tangible, immediate, and fun."

---

### 1.3 Requirements Slide (30–45 s)

**[SLIDE: Top-level requirements — functional + engineering]**

> "Here are our key requirements. On the functional side: the system must detect which blocks are connected and in what order, validate that the sequence is a legal program, execute the sequence on command, and provide real-time visual feedback. On the engineering side: [list your three top engineering specs here, e.g. color preview latency ≤ 200 ms, config-to-app latency ≤ 6 s, I2C rise time within standard-mode spec]. These are the specs we'll demonstrate quantitatively in Part 3."

---

### 1.4 Subsystems (60–75 s)

**[SLIDE: System block diagram — Brain Block, Child Blocks by category, Flutter App, I2C bus, TCP link]**

> "Our system has three major subsystems."

**Brain Block**

> "First, the Brain Block — an ESP32 with a TFT touch display. It's the I2C master: it scans the bus, identifies which blocks are connected and where, manages program execution, and communicates with the companion app over Wi-Fi TCP."

**[SLIDE or callout: Child blocks divided into three categories]**

**Child Blocks — Control Flow**

> "Second, the child blocks. Control flow blocks — If, Then, End If, Loop, End Loop, Delay — define the structure of the program. They're marker blocks: they don't produce output themselves, but they tell the Brain how to route execution."

**Child Blocks — Output**

> "Output blocks produce the visible and audible results: LED Color Flash, Disco Mode, Note, and Music Sequence. Each one has an LED matrix, addressable LEDs, and a speaker. When a user configures an output block, they pick a setting using the on-block numpad, and the block previews it immediately."

**Child Blocks — Input**

> "The input block is Button Press. It lets the program wait for a physical button press before continuing — enabling interactive, event-driven programs."

**Flutter Companion App**

> "Third, the Flutter companion app. It runs a TCP server, receives JSON configuration from the Brain, validates the block sequence against our rule set — checking for complete If/Loop structures and correct ordering — and displays the full configuration in real time, including any validation errors or warnings."

---

## Part 2: Demo of Overall Functionality (4–6 min)

---

### 2.0 Scenario Setup (20–30 s)

**[SLIDE: Scenario title — "Build a Light + Sound Program"]**

> "For our functionality demo, we'll walk through a single end-to-end scenario called 'Build a Light + Sound Program.' A user snaps together a sequence of blocks to make the system flash LEDs and play a sound, then presses a button on the Brain to run it."

---

### 2.1 Connect Blocks Magnetically (30–45 s)

**[CAMERA: close-up of hands snapping blocks together on a surface or rail]**

> "Starting with just the Brain Block powered on. We connect child blocks magnetically — here we're adding a Loop block, then an LED Color Flash block configured to red, a Note block set to a musical note, and then an End Loop. Each block has a unique I2C address, and as they're connected they join the bus."

---

### 2.2 App Detects Configuration (30–45 s)

**[CAMERA: split view — hardware + app screen]**

> "Within a few seconds the Brain Block scans the I2C bus, detects the new blocks, and sends a configuration message to the app over TCP. Watch the app — the block list updates automatically. We can see all four blocks appear in order with their types and I2C addresses. The app runs our validation rules and confirms this is a valid program sequence — no errors, no warnings."

> "The Brain's TFT display also updates to reflect the connected configuration."

---

### 2.3 Press Button to Execute (15–20 s)

**[CAMERA: finger pressing a button or touch on the Brain's TFT display]**

> "Now we press the execute button on the Brain Block's touch display. The app sends a validation confirmation — the program is valid, execution is allowed — and the Brain begins running the sequence."

---

### 2.4 LEDs Animate, Sound Plays, Display Updates (45–60 s)

**[CAMERA: wide shot showing LED matrix light up + speaker reacting, with TFT display visible]**

> "The Loop block tells the Brain to repeat the body twice. The LED Color Flash block fires — you can see the red flash on the LED matrix and addressable LEDs. Then the Note block plays the note through the speaker. The sequence loops, plays again, and then ends."

> "The Brain's TFT display tracks execution state in real time — you can see it progress through each block."

---

### 2.5 Real-Time Responsiveness + Modular Detection (30–40 s)

**[CAMERA: add or swap one block while the system is idle; show app updating]**

> "One of the key behaviors to highlight: the system is fully real-time. Watch what happens when we physically add a block. [Add one block.] The app detects and shows it within a few seconds — no restart needed. And if we swap the order, the configuration updates to match. The program is defined by the physical arrangement of the blocks."

---

### 2.6 Configuration Validation — Invalid Sequence (40–50 s)

**[SLIDE or screen recording: app receiving a block_config JSON with an invalid sequence, e.g. If without End If]**

> "We can't easily create a physically invalid sequence — our blocks are designed to guide users toward valid programs. But our configuration engine handles invalid sequences, and we want to show that. So we'll demonstrate it directly through the app."

> "Here we're sending the app a pre-built configuration JSON with an invalid sequence — an If block with no matching End If. Watch the app immediately flag it: we get a red validation error, the affected block indices are highlighted, and execution is blocked."

> "[Send the valid JSON back.] When we replace it with a valid configuration, the error clears and the system is ready to run again."

---

### 2.7 Functionality Recap (20–30 s)

**[SLIDE: quick bullet recap]**

> "To recap what we just showed: magnetic connection and automatic detection, real-time configuration updates in the app, validation of both valid and invalid sequences, and full program execution — LEDs, sound, and display — from a single button press."

---

## Part 3: Demo of Three Key Engineering Specifications (4–6 min)

---

### 3.0 Specs Introduction (20–30 s)

**[SLIDE: three-row summary table — spec, quantity, units, target]**

> "Part 3 is aimed at viewers with an engineering background. We'll demonstrate three specs, each backed by 10 test runs with summary statistics. Three of those runs per spec are shown live here."

> "Our three specs are:
> - Spec 1: LED Color Flash preview latency — how fast the block's LED changes after a color is selected. Target: mean ≤ 200 ms.
> - Spec 2: Config-change-to-app latency — how quickly the app reflects a physical block change. Target: mean ≤ 6000 ms.
> - Spec 3: I2C rise time — signal integrity on the bus. Target: within the I2C standard-mode requirement."

---

### 3.1 Spec 1 — LED Color Flash Preview Latency (≈1.5 min)

**[SLIDE: spec definition — quantity, units, target, measurement setup diagram]**

> "Spec 1 is color-selection preview latency on the LED Color Flash block. When a user presses a numpad key to pick a color, the block spec requires an immediate LED preview. We're measuring how fast that actually happens — from the key press to the moment the LED changes color."

> "We measure using [oscilloscope / logic analyzer / high-fps video — describe your method]. Marker A is at the key press; Marker B is the LED data line transition."

**[SLIDE: 10-run results table + bar chart if available — mean, std dev, min, max, target line]**

> "Over 10 runs we got a mean of [X] ms with a standard deviation of [Y] ms, minimum [Z], maximum [W]. Our target is mean under 200 ms. [State whether met and margin.]"

> "Now three live runs."

**[CAMERA: close-up of numpad press + LED matrix, with timer visible or narrated]**

> "Run 1: press — [X] ms. Run 2: [X] ms. Run 3: [X] ms. Consistent with our summary data. [Brief interpretation — met/not met, what it means for user experience.]"

---

### 3.2 Spec 2 — Config-Change-to-App Latency (≈1.5 min)

**[SLIDE: spec definition — quantity, units, target, measurement method]**

> "Spec 2 is the end-to-end latency from a physical block change — adding or removing a block from the I2C bus — until the Flutter app's configuration view reflects the new topology. This validates the full pipeline: I2C scan → config manager → JSON generation → TCP send → app parse → UI update."

> "We measure from the moment of physical connection or disconnection to when the block count and list in the app change. We use [stopwatch / app-side received timestamp — describe your method]."

**[SLIDE: 10-run results table — mean, std dev, min, max, target line]**

> "Over 10 runs: mean [X] ms, std dev [Y], min [Z], max [W]. Target is 6000 ms. [State whether met and by how much margin.]"

> "Three live runs."

**[CAMERA: add/remove a block; show stopwatch and app side-by-side or in sequence]**

> "Run 1: [X] ms. Run 2: [X] ms. Run 3: [X] ms. [Brief interpretation.]"

---

### 3.3 Spec 3 — I2C Rise Time (≈1.5 min)

**[SLIDE: spec definition — quantity, units, target; diagram showing probe point on SCL/SDA]**

> "Spec 3 is I2C rise time — the time for the SCL or SDA line to transition from low to high, measured 10% to 90% of supply voltage. This is a hardware signal-integrity spec. The I2C standard-mode requirement is a rise time under 1000 ns at 100 kHz. If our edges are too slow due to bus capacitance from multiple blocks and long traces, we risk communication errors."

> "We probe SCL at the Brain Block's I2C header with our normal pull-up resistors and [N] child blocks connected, and use the oscilloscope's automatic rise-time measurement."

**[SLIDE: 10-run results table — mean, std dev, min, max, target line at 1000 ns]**

> "Over 10 captures: mean [X] ns, std dev [Y], min [Z], max [W]. [State whether within spec and describe margin.]"

> "Three live captures."

**[CAMERA: oscilloscope screen with 10%–90% markers on a rising SCL edge]**

> "Capture 1: [X] ns. Capture 2: [X] ns. Capture 3: [X] ns. [Brief interpretation — pull-up sizing and bus loading are appropriate / need tuning.]"

---

### 3.4 Specs Wrap (15 s)

> "That covers our three engineering specs — LED preview latency, config-to-app latency, and I2C rise time — each with 10-run data and three live demonstrations."

---

## Part 4: Closing & Looking Ahead (30–45 s)

**[SLIDE: summary + remaining work]**

> "To wrap up: we built a working end-to-end system — physical blocks connect magnetically, the Brain detects and validates the configuration in real time, the companion app reflects every change instantly, and programs execute with LED animation, audio, and display output. Three engineering specs are backed by measured data."

> "Before the final demo we're focused on [fill in top 2–3 remaining items — e.g. PCB bring-up, completing execution engine for all block types, mechanical enclosure]. We'd welcome the committee's feedback on [areas you want input on — e.g. signal integrity margin, execution robustness, UX]."

> "Thanks for watching."

---

## Timing Summary

| Section | Content | Target |
|---------|---------|--------|
| 1.1 | Title slide | 15–20 s |
| 1.2 | Problem & goal | 30–45 s |
| 1.3 | Requirements | 30–45 s |
| 1.4 | Subsystems (Brain, child blocks ×3 categories, app) | 60–75 s |
| 2.1–2.7 | Functionality demo — scenario + validation engine | 4–6 min |
| 3.0–3.4 | Three engineering specs with live runs | 4–6 min |
| 4 | Closing & looking ahead | 30–45 s |
| **Total** | | **10–15 min** |

Rehearse with a timer. If over 15 min, trim the subsystem walkthrough (1.4) or the functionality recap (2.7). If under 10 min, expand the spec narration or add a second functionality scenario.
