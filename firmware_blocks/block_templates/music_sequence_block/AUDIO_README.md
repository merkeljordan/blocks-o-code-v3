# Music Sequence Block Audio (Current Implementation)

## Overview

The `music_sequence_block` currently uses a simple ESP32 PWM tone output path:

- Output pin: `GPIO25`
- Backend: `LEDC PWM` (square-wave tone generation)
- Amplifier: `LM386` analog amplifier board
- Speaker: connected to the LM386 output

This is a reliability-first implementation. It trades audio quality for simplicity and stable behavior on the current hardware.

## Where Audio Is Implemented

- API + public functions: `firmware_blocks/block_templates/music_sequence_block/speaker.h`
- Audio + presets implementation: `firmware_blocks/block_templates/music_sequence_block/speaker.c`
- Startup beeps and init volume: `firmware_blocks/block_templates/music_sequence_block/main/main.c`
- TFT preview requests (preset/compose preview): `firmware_blocks/block_templates/music_sequence_block/main/tft_ui.c`

## Audio Architecture (Current)

The firmware does not stream PCM audio and does not use the ESP32 internal DAC right now.

It generates note tones by changing PWM frequency on `GPIO25`, then the LM386 amplifies that signal.

Flow:

1. `main.c` calls `speaker_init()` during boot
2. `main.c` sets startup volume with `speaker_set_volume(28)`
3. Boot/ok/error feedback calls `speaker_beep_ok()`, `speaker_beep_error()`, or `speaker_play_tone(...)`
4. TFT UI preview task calls `speaker_play_preset(...)` or `speaker_play_sequence(...)`
5. `speaker.c` converts notes to PWM frequency + duration and blocks until playback finishes

## LEDC PWM Backend Details

The low-level output in `speaker.c` uses:

- `LEDC_LOW_SPEED_MODE`
- `LEDC_TIMER_0`
- `LEDC_CHANNEL_0`
- `LEDC_TIMER_10_BIT` duty resolution

### `speaker_init()`

`speaker_init()`:

1. Configures the LEDC timer (initial frequency `1000 Hz`)
2. Configures the LEDC channel on `GPIO25`
3. Starts with duty `0` (silent)
4. Logs: `Speaker initialized via LEDC PWM on GPIO25`

### `speaker_play_tone(freq_hz, duration_ms)`

Behavior:

- `duration_ms == 0`: returns immediately
- `freq_hz == 0`: treated as a rest (stops output, delays, returns)
- Otherwise:
  - `ledc_set_freq(...)`
  - set duty from current volume
  - `ledc_update_duty(...)`
  - delay for the tone duration
  - `speaker_stop()` at the end

### `speaker_stop()`

`speaker_stop()` calls `ledc_stop(...)` and drives the output low when idle.

Important:

- This silences the firmware-generated tone signal
- It does **not** mute analog amplifier hiss/noise from the LM386 stage

## Volume Model

The volume API is software-only (`speaker_set_volume`, `speaker_get_volume`).

- Volume is stored as `0..100%`
- PWM duty is derived from volume
- At `100%` volume, duty is capped around `50%` (not 100%) to keep the square wave centered and avoid unnecessary drive

## Music / Preset Layer (Preserved)

Higher-level music APIs are unchanged and still sit on top of the PWM backend:

- `speaker_play_note(...)`
- `speaker_play_step(...)`
- `speaker_play_sequence(...)`
- `speaker_play_preset(...)`

Current built-in presets:

- `Twinkle`
- `Jingle Bells`

Tempo behavior:

- `speaker_play_sequence(...)` and `speaker_play_step(...)` scale note and gap durations using tempo %
- Tempo is clamped in software

## Timing Behavior (Important for UI/Brain Integration)

Playback calls are currently blocking:

- `speaker_play_tone(...)` blocks for the duration
- `speaker_play_sequence(...)` blocks until the whole sequence finishes

This is fine for boot beeps and preview playback, but long playback should eventually be handled by a dedicated execution state machine if the Brain needs non-blocking execution control.

## Why You May Still Hear Buzzing / Static

If you hear buzzing/static immediately after flash, even before user interaction, the most likely causes are hardware/analog noise rather than software bus contention.

Common causes:

- LM386 noise floor / hiss (common on simple LM386 circuits)
- High LM386 gain
- Power rail noise (USB supply noise, TFT backlight current spikes)
- Ground return noise/shared ground path between ESP32, TFT, and amp
- Long/unshielded wire from `GPIO25` to amp input
- PWM edge coupling into the analog stage (square waves are noisy by nature)

## Is It Because Of A Constant Frequency?

Sometimes during active playback, yes:

- PWM notes are square waves, so they can sound harsh/buzzy compared to DAC audio
- Low notes especially can sound like a strong buzz

But if the buzz is present while idle:

- `speaker_stop()` already drives the pin low
- That usually means the LM386 is amplifying noise, not a stuck firmware tone

## Is It Bus Contention?

Usually no (not protocol contention):

- TFT/touch: SPI
- Brain communication: I2C
- Audio: LEDC PWM on a GPIO

They are separate peripherals. What can still happen is electrical coupling through shared power/ground, which sounds like noise but is not a software bus conflict.

## Can Firmware Fully Mute Idle Buzz?

Firmware can mute the PWM signal, and it already does that with `speaker_stop()`.

Firmware cannot fully mute LM386 hiss if the amplifier remains powered.

Note about `AMP_EN`:

- On the shared LM386 schematic discussed during debugging, `AMP_EN` is exposed on the connector but not actually wired into the LM386 control path
- That means software cannot use `AMP_EN` to shut down the amplifier on this board revision

## Practical Troubleshooting (Current Hardware)

1. Confirm the PWM backend is running
- Look for `Speaker initialized via LEDC PWM on GPIO25` in serial logs

2. Distinguish idle hiss vs playback tone
- Idle buzz/static: usually analog noise
- Buzz only during notes: expected PWM/square-wave character

3. Improve the LM386 path
- Lower gain (remove/reduce gain capacitor between pins 1 and 8 if present)
- Add local decoupling near LM386 power (`100 nF` + bulk cap)
- Keep `GPIO25` to amp-input wiring short
- Add a small RC filter before the LM386 input if needed

4. Verify signal routing
- `GPIO25` -> coupling capacitor -> LM386 input
- Common ground between ESP32 and amp board

## Future Upgrade Path (For Better Sound Quality)

If you want cleaner sound than PWM + LM386:

- Use an external I2S DAC/amp module (best option)
- Or use an external I2S DAC feeding a better analog amplifier stage

This will improve tone quality and usually reduce audible noise compared with PWM square-wave output.
