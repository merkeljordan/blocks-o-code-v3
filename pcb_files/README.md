## PCB files

This folder contains the hardware design deliverables for Blocks o' Code v3 (schematics/PCB CAD sources and manufacturing exports).

### What’s in here

- **`Main_PCB.zip`**: The shared ESP32 main board used by **Brain + all child blocks**.
- **`Amplifier.zip`**: Audio amplifier board/design files.
- **`ChargingModule.zip`**: Battery charging module board/design files.
- **`VoltageRegulators.zip`**: Voltage regulator board/design files.

> If you add new hardware, prefer a single zip per board/module and keep the name stable over time.

### GPIO source of truth

The human-readable pin map lives here:

- `docs/hardware/gpio-pinouts.md`

When the PCB schematic changes, update the GPIO doc in the same PR so firmware and PCB stay in sync.

### How to use these zips

Because CAD toolchains vary, these archives may include a mix of:

- CAD project files (schematic + PCB layout)
- Gerbers / drill files
- BOM, pick-and-place, netlists, PDFs, renders

Recommended workflow:

- **To manufacture**: use the Gerber/drill outputs inside each archive (or regenerate them from the CAD project to ensure they match the latest revision).
- **To review**: open the CAD project in its native tool, or use any included PDFs/renders for quick inspection.

### Conventions (please follow)

- **One module = one zip**: keep everything for that board together.
- **Include exports**: if possible, include Gerbers + drill + BOM + pick-and-place in the archive.
- **Document revisions**: if the CAD tool supports it, include the board revision in filenames or in an included `REVISION.txt`.
- **Avoid duplication**: don’t copy the same Gerbers into multiple zips—reference the owning module instead.
