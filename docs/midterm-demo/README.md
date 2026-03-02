# Midterm Demo Video Materials

This folder contains everything needed to prepare and record the 10–15 minute midterm demo video for Blocks o' Code v3, following the required structure:

- **Part 1 (2–3 min):** Overview of all parts/subsystems  
- **Part 2 (4–6 min):** Demo of overall functionality  
- **Part 3 (4–6 min):** Demo of 3 key engineering specifications (10 runs each, 3 shown live)  
- **Part 4 (30–60 s):** Closing and looking ahead  

## Files in this folder

| File | Purpose |
|------|--------|
| [engineering-specifications.md](engineering-specifications.md) | Definitions of the three specs (quantity, units, target, measurement method). |
| [spec-test-data-template.md](spec-test-data-template.md) | Tables to record 10 trials per spec and compute mean, std dev, min, max. |
| [visuals.md](visuals.md) | Mermaid diagrams (subsystem, data flow, scenarios) and result table templates for Part 3. |
| [demo-script.md](demo-script.md) | Narration script with approximate timings for all four parts. |
| [rehearsal-and-recording-checklist.md](rehearsal-and-recording-checklist.md) | Pre-rehearsal, rehearsal, recording-day, and post-recording checklist. |

## Recommended order of use

1. **Define and measure**  
   Use [engineering-specifications.md](engineering-specifications.md) to confirm the three specs, then run 10 trials per spec and fill [spec-test-data-template.md](spec-test-data-template.md). Compute summary statistics.

2. **Prepare visuals**  
   Use [visuals.md](visuals.md) to create slides or overlays: render the Mermaid diagrams, fill the result tables with your data.

3. **Script and rehearse**  
   Adapt [demo-script.md](demo-script.md) to your team’s wording. Run through [rehearsal-and-recording-checklist.md](rehearsal-and-recording-checklist.md) and time the run-through (target 10–15 min total).

4. **Record**  
   On recording day, follow the recording section of the checklist and use the script and visuals during the take.

## Three engineering specifications (summary)

| Spec | Quantity | Units | Target |
|------|----------|--------|--------|
| 1 | LED color select → preview latency | ms | Mean ≤ 50 ms |
| 2 | Config-change-to-app latency | ms | Mean ≤ 6000 ms |
| 3 | I2C rise time | ns/µs | Within I2C standard-mode spec |

For full details, measurement methods, and rationale, see [engineering-specifications.md](engineering-specifications.md).
