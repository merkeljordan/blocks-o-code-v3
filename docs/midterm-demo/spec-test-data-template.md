# Midterm Demo: Spec Test Data (10 Runs per Spec)

Use this template to record 10 trials per specification, then compute summary statistics. Fill in the tables before recording Part 3 of the video. **Three of the 10 runs for each spec will be shown live in the demo.**

---

## Spec 1: LED Color Flash — Color Selection Preview Latency (ms)

**Test condition**: Same color key pressed each run on the LED Color Flash block (e.g. always key "1" → red). Note which key/color: ____________________.

| Run | Latency (ms) | Notes |
|-----|--------------|--------|
| 1   |              |       |
| 2   |              |       |
| 3   |              | *Show live in video* |
| 4   |              |       |
| 5   |              |       |
| 6   |              | *Show live in video* |
| 7   |              |       |
| 8   |              |       |
| 9   |              | *Show live in video* |
| 10  |              |       |

**Summary statistics** (fill after collecting data):

| Statistic | Value (ms) |
|-----------|------------|
| Mean      |            |
| Std dev   |            |
| Min       |            |
| Max       |            |

**Target**: Mean ≤ 50 ms. **Met?** Yes / No

---

## Spec 2: Config-Change-to-App Latency (ms)

**Test condition**: _____ child blocks before change; action: add one / remove one / reorder (circle one). Same network for all runs.

| Run | Latency (ms) | Notes |
|-----|--------------|--------|
| 1   |              |       |
| 2   |              |       |
| 3   |              | *Show live in video* |
| 4   |              |       |
| 5   |              |       |
| 6   |              | *Show live in video* |
| 7   |              |       |
| 8   |              |       |
| 9   |              | *Show live in video* |
| 10  |              |       |

**Summary statistics**:

| Statistic | Value (ms) |
|-----------|------------|
| Mean      |            |
| Std dev   |            |
| Min       |            |
| Max       |            |

**Target**: Mean ≤ 2000 ms. **Met?** Yes / No

---

## Spec 3: I2C Rise Time (ns or µs)

**Test condition**: Probe SCL (or SDA) at the Brain Block with normal pull-ups and a representative number of child blocks connected.

| Run | Rise time (ns or µs) | Notes |
|-----|----------------------|--------|
| 1   |                      |       |
| 2   |                      |       |
| 3   |                      | *Show live in video* |
| 4   |                      |       |
| 5   |                      |       |
| 6   |                      | *Show live in video* |
| 7   |                      |       |
| 8   |                      |       |
| 9   |                      | *Show live in video* |
| 10  |                      |       |

**Summary statistics**:

| Statistic | Value (ns or µs) |
|-----------|------------------|
| Mean      |                  |
| Std dev   |                  |
| Min       |                  |
| Max       |                  |

**Target**: Within I2C standard-mode requirement (e.g. ≤ 1000 ns for 100 kHz) with margin. **Met?** Yes / No

---

## Formulas (for reference)

- **Mean**: sum of values / 10  
- **Standard deviation**: √( Σ(x_i − mean)² / (10−1) )  
- **Min / Max**: smallest and largest of the 10 values

Use a spreadsheet (Excel, Google Sheets) to compute these; then paste the summary into the tables above and into your slides or figures for the video.
