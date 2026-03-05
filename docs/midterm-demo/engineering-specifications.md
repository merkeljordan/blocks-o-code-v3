# Midterm Demo: Three Key Engineering Specifications

This document defines the three engineering specifications to be demonstrated in Part 3 of the midterm demo video. Each spec has a **quantity**, **units**, **target**, and **rationale** for engineering reviewers.

---

## Specification 1: LED Color Flash Block — Color Selection Preview Latency


| Item               | Value                                                                                                                                                                                   |
| ------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Quantity**       | Latency from a color selection on the LED Color Flash block to the block's visible LED preview output                                                                                   |
| **Definition**     | Time from when the user presses a numpad key on the LED Color Flash block (selecting a color) until the block's LED matrix (or addressable LEDs) visibly changes to the selected color as a preview. |
| **Units**          | milliseconds (ms)                                                                                                                                                                       |
| **Target**         | Mean ≤ 50 ms (with stable, consistent behavior across 10 runs)                                                                                                                         |
| **Why it matters** | Preview responsiveness is a direct UX requirement: the block spec states "Preview flashes selected color on LED matrix + addressable LEDs." Slow preview makes the block feel unresponsive and hurts usability. It also validates the block-local path: numpad input → firmware logic → LED driver output. |


**Measurement method**

- Recommended (most engineering-rigorous): use an oscilloscope/logic analyzer and **two timing markers**:
  - **Marker A**: the numpad key signal or a GPIO toggled in firmware at the instant the key press is registered.
  - **Marker B**: the LED data line transitioning (e.g. WS2812 data pin or LED matrix signal) as the LED color changes.
- Practical alternative: high-frame-rate video (240 fps+) pointing at both the numpad press and the LED output; compute latency from frame count.
- **Test condition**: Same color key pressed each run (e.g. always key "1" → red); repeat 10 times; report mean, standard deviation, min, max.

---

## Specification 2: Config-Change-to-App Latency


| Item               | Value                                                                                                                                                                                                     |
| ------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Quantity**       | Latency from physical block change to app UI update                                                                                                                                                       |
| **Definition**     | Time from the moment a block is added or removed on the I2C bus (or connection order changed) until the Flutter app's block configuration view shows the updated topology (correct block count and list). |
| **Units**          | milliseconds (ms)                                                                                                                                                                                         |
| **Target**         | Mean ≤ 2000 ms (within ~2 scan intervals at 3 s) under normal Wi‑Fi and TCP conditions                                                                                                                    |
| **Why it matters** | Users expect near real-time feedback when rearranging blocks. This end-to-end latency drives perceived responsiveness and validates the pipeline: scan → config manager → JSON → TCP → app parse → UI.    |


**Measurement method**

- Start with stable connection (Brain + app). Note app display state.
- At t=0: add or remove one child block (or swap order).
- Measure time until app UI shows the new configuration (e.g. block count or list change). Use a stopwatch or on-screen timestamps if the app logs receive time.
- **Test condition**: Same network and same number of blocks per run; 10 trials; report mean, standard deviation, min, max.

---

## Specification 3: I2C Rise Time


| Item               | Value                                                                                                                                                                                      |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Quantity**       | Rise time of the I2C SCL (or SDA) signal                                                                                                                                                   |
| **Definition**     | Time for the selected I2C line to transition from low to high (typically 10% to 90% of Vdd), measured at the Brain Block's I2C header under a representative bus loading (pull-ups + blocks). |
| **Units**          | nanoseconds (ns) or microseconds (µs)                                                                                                                                                      |
| **Target**         | Within the I2C standard-mode requirement (≤ 1000 ns recommended for 100 kHz), with comfortable margin under our actual bus loading.                                                        |
| **Why it matters** | Rise time is a core signal-integrity metric for I2C. Too-slow edges can cause timing violations, communication errors, or limit how many blocks you can place on the bus and how long the bus can be. |


**Measurement method**

- Use an oscilloscope to probe SCL (or SDA) at the Brain Block, with the normal pull-up resistors and a representative number of child blocks connected.
- Capture multiple rising edges during normal I2C activity (e.g. during a scan) and use the scope's automatic 10%–90% rise-time measurement.
- Record the rise time for 10 separate runs (e.g. different captures or zoomed windows) and compute mean, standard deviation, min, and max.
- Compare the measured times to the I2C standard-mode limit and briefly discuss margin.

---

## Summary Table for Video


| Spec | Quantity                             | Units | Target                        |
| ---- | ------------------------------------ | ----- | ----------------------------- |
| 1    | LED color select → preview latency   | ms    | Mean ≤ 50 ms                 |
| 2    | Config-change-to-app latency         | ms    | Mean ≤ 2000 ms                |
| 3    | I2C rise time                        | ns/µs | Within I2C standard-mode spec |


Use this table on a slide or in the script when introducing Part 3 of the demo.
