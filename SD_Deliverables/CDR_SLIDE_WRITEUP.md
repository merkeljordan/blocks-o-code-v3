# Blocks o' Code v3 - CDR Slide Write-Up

Target format: **32 slides / 22 minutes**

Recommended pacing:
- **Anchor slides (8 total):** 45-60 sec each
- **Support slides (24 total):** 25-40 sec each
- Average overall: ~41 sec/slide

Team role assumption (edit as needed):
- **Jordan:** system flow, software, integration, plan
- **Camilla:** motivation, power, hardware decisions
- **Annesley:** CAD/enclosure, PCB layout/manufacturing
- **Destiny:** firmware architecture, specs/testing, reliability

---

## Section A - Opening and Context (Slides 1-5, ~2.5 min)

### 1) Title Slide
- **Owner:** Jordan
- **Time:** 0:10
- **Slide content:** Project title, subtitle
- **Talk track:** "We are presenting Blocks o' Code v3, a tangible programming platform designed for accessible learning."

### 2) Team Intro
- **Owner:** All
- **Time:** 0:30
- **Slide content:** team names, photos, majors, roles
- **Talk track:** Everyone introduces themselves and role scope.

### 3) Educational Problem
- **Owner:** Camilla
- **Time:** 0:35
- **Slide content:** Pain points with current tools for beginners, kids starting coding at a young age, etc.
- **Talk track:** "Students often struggle to map abstract code to physical outcomes."

### 4) Accessibility Gap
- **Owner:** Camilla
- **Time:** 0:30
- **Slide content:** light/sound/touch benefits for diverse learners
- **Talk track:** "Multi-sensory feedback broadens who can effectively learn programming concepts."

### 5) Value Proposition
- **Owner:** Camilla
- **Time:** 0:30
- **Slide content:** one sentence + 3 value bullets
- **Talk track:** "Blocks o' Code turns block sequencing into validated, physical programming with real-time feedback."

---

## Section B - What the System Does (Slides 6-10, ~3.0 min)

### 6) System at a Glance
- **Owner:** Jordan
- **Time:** 0:45 (anchor)
- **Slide content:** Child blocks -> Brain block -> Flutter app
- **Talk track:** "This is the full loop: physical arrangement, discovery, validation, and visualization."

### 7) Physical Interaction Flow
- **Owner:** Jordan
- **Time:** 0:35
- **Slide content:** user connects/arranges blocks and powers system
- **Talk track:** "The student experience starts by arranging blocks physically, not writing text code."

### 8) Data/Control Flow (No Code)
- **Owner:** Jordan
- **Time:** 0:45 (anchor)
- **Slide content:** telemetry/config -> I2C -> JSON -> app validation
- **Talk track:** "Firmware translates physical topology into machine-readable configuration for rule checking."

### 9) Valid Sequence Example
- **Owner:** Jordan
- **Time:** 0:30
- **Slide content:** valid If/Then/End If chain and expected output
- **Talk track:** "Here is a valid sequence and the behavior the learner sees immediately."

### 10) Invalid Sequence Example
- **Owner:** Jordan
- **Time:** 0:25
- **Slide content:** missing Then or End block; warning/error mapping
- **Talk track:** "Invalid chains become teachable moments through immediate, explicit feedback."

---

## Section C - Goals and Specifications (Slides 11-12, ~1.6 min)

### 11) Goals/Objectives
- **Owner:** Destiny
- **Time:** 0:40
- **Slide content:** basic goal, functional goal, stretch goal, and success criteria
- **Talk track:** "These goals define what success looks like and how we evaluate completion."

### 12) Engineering Specifications (Single Slide)
- **Owner:** Destiny
- **Time:** 0:55 (anchor)
- **Slide content:** one table with numeric specifications and three highlighted demonstrable specs
- **Talk track:** "This single table is our CDR specification baseline, with the three demonstrable specs clearly marked."

---

## Section D - Comparison and Selection (Slides 13-18, ~4.2 min)

### 13) MCU Selection
- **Owner:** Annesley
- **Time:** 0:50 (anchor)
- **Slide content:** MCU technology/part comparison table and final selection rationale
- **Talk track:** "ESP32-WROOM-32 gives the best balance of performance, tooling, and integration risk."

### 14) Power System Selection
- **Owner:** Camilla
- **Time:** 0:40
- **Slide content:** battery/charger/regulation technology and part selection rationale
- **Talk track:** "Power architecture is selected for stable rails and portable operation."

### 15) Connector and Peripheral Selection
- **Owner:** Destiny
- **Time:** 0:35
- **Slide content:** connector options, peripheral selection, and trade-off summary
- **Talk track:** "We prioritized assembly reliability and consistent behavior across child blocks."

### 16) Software Technology Selection
- **Owner:** Jordan
- **Time:** 0:45
- **Slide content:** firmware and app technology comparison table with chosen stack
- **Talk track:** "Our software stack balances real-time control, maintainability, and cross-platform UI needs."

### 17) Software Option Selection
- **Owner:** Jordan
- **Time:** 0:35
- **Slide content:** major implementation options considered and selected approach
- **Talk track:** "This option set minimizes integration complexity while preserving feature coverage."

---

## Section E - Hardware and Software Design (Slides 18-23, ~5.0 min)

### 18) Hardware Block Diagram
- **Owner:** Camilla
- **Time:** 0:45 (anchor)
- **Slide content:** brain and child hardware blocks and interfaces
- **Talk track:** "This diagram shows component partitioning and interface boundaries."

### 19) Overall Schematics
- **Owner:** Camilla
- **Time:** 0:40
- **Slide content:** overall schematic snapshots for the pcb
- **Talk track:** "These schematics show full electrical connectivity from power input through MCU and peripherals."

### 20) PCB Layout
- **Owner:** Annesley
- **Time:** 0:40
- **Slide content:** placement strategy, routing, serviceability, manufacturability
- **Talk track:** "Layout choices improve signal integrity and assembly consistency."

### 21) Prototyping and Enclosure
- **Owner:** Annesley
- **Time:** 0:35
- **Slide content:** enclosure constraints, CAD fit, and prototype integration
- **Talk track:** "Mechanical design supports electronics fit, usability, and repeatable prototyping."

### 22) Software Design (No Code)
- **Owner:** Destiny
- **Time:** 0:45
- **Slide content:** flowchart/state/sequence-level behavior for firmware and app
- **Talk track:** "Software behavior is presented through architecture diagrams only, with no source code."

### 23) App Logic and Validation (No Code)
- **Owner:** Jordan
- **Time:** 0:35
- **Slide content:** parser, validator, and feedback loop behavior
- **Talk track:** "The app converts incoming data into immediate, understandable feedback."

---

## Section F - Testing, Admin, and Plan (Slides 24-32, ~5.7 min)

### 24) Testing Methodology
- **Owner:** Destiny
- **Time:** 0:40
- **Slide content:** overall test strategy, instrumentation, and pass/fail criteria
- **Talk track:** "This slide defines how all tests were run and how success was measured."

### 25) Hardware Testing
- **Owner:** Camilla
- **Time:** 0:45
- **Slide content:** electrical/power/connectivity tests and hardware validation results
- **Talk track:** "Hardware testing verified power stability, communication reliability, and physical integration behavior."

### 26) Software Testing
- **Owner:** Jordan
- **Time:** 0:45
- **Slide content:** firmware/app validation tests, sequence-rule checks, and integration checks
- **Talk track:** "Software testing verified message handling, rule validation, and end-to-end behavior."

### 27) Performance Evaluation and Results
- **Owner:** Destiny
- **Time:** 0:50 (anchor)
- **Slide content:** measured outcomes against the specification table
- **Talk track:** "Measured performance is compared directly against each specification target."

### 28) Difficulty, Problem, and Proposed Solution
- **Owner:** Destiny
- **Time:** 0:45
- **Slide content:** key challenge, mitigation approach, and outcome
- **Talk track:** "Our biggest reliability issue was resolved with firmware and electrical mitigations."

### 29) Budget
- **Owner:** Annesley
- **Time:** 0:35
- **Slide content:** total cost and category breakdown
- **Talk track:** "Prototype cost remains controlled while preserving required functionality."

### 30) Work Distribution
- **Owner:** Destiny
- **Time:** 0:30
- **Slide content:** ownership by subsystem and responsibilities
- **Talk track:** "Responsibility split reduced bottlenecks and improved parallel progress."

### 31) Progress and Plan for Completion
- **Owner:** Jordan
- **Time:** 0:50 (anchor)
- **Slide content:** completion status, remaining milestones, and risk mitigation
- **Talk track:** "This is our concrete path from current state to final completion."

### 32) Q&A
- **Owner:** All (Jordan moderates)
- **Time:** 0:25 lead-in + Q&A
- **Slide content:** thank you
- **Talk track:** "We welcome questions."

---

## CDR Guideline Compliance Matrix (Mandatory Coverage)

Use this as the final review checklist before presenting.

- **Motivation and background:** Slides 3-5
- **Goals and objectives:** Slide 11
- **Table of engineering specifications with numbers:** Slide 12
- **Highlight 3 demonstrable specifications:** Slide 12
- **Comparison and selection of hardware technologies:** Slides 13-15
- **Comparison and selection of hardware parts:** Slides 13-15
- **Comparison and selection of software technologies:** Slides 16-17
- **Comparison and selection of software options:** Slides 16-17
- **Hardware design block diagram:** Slide 18
- **Overall schematic:** Slide 19
- **Significant PCB design (PSU, MCU, peripherals; no dev-board stacking):** Slide 20
- **PCB layout:** Slide 20
- **Software design diagrams (flow/state/sequence/activity/class):** Slides 22-23
- **No code on software design slides:** Slides 22-23
- **Prototyping and testing:** Slides 21 and 24-26
- **Hardware testing:** Slide 25
- **Software testing:** Slide 26
- **Hardware and software testing:** Slides 25-26
- **Performance evaluation:** Slide 27
- **Success:** Slide 27
- **Difficulty/problem/proposed solution:** Slide 28
- **Administrative content - budget:** Slide 29
- **Administrative content - work distribution:** Slide 30
- **Administrative content - progress and plan for completion:** Slide 31
- **Professional photo image with name and major:** Slide 2
- **Professional photo in corner of each presenter's slides:** apply slide master across all presented slides

---

## Backup Slides (Suggested, not in 22-min core timing)

If you include backups in the same deck file, append:
- **B1:** full hardware technology comparison tables
- **B2:** full hardware part comparison tables
- **B3:** full software technology/option comparison tables
- **B4:** full engineering specification table with requirement IDs
- **B5:** raw performance and test logs
- **B6:** detailed BOM and cost rollup
- **B7:** additional PCB layout and layer screenshots
- **B8:** full schematic sheets (brain and child)


