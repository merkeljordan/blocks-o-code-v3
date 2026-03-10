// ============================================================================
// SinWaveGenerator.cpp
// ============================================================================
// Procedural tone source used for beeps and single-frequency notes.
//
// Call flow:
// - speaker_play_tone() constructs SinWaveGenerator
// - DACOutput writer task calls getFrames() repeatedly

#include <math.h>

#include "SinWaveGenerator.h"

// --------------------------------------------------------------------------
// SinWaveGenerator::SinWaveGenerator
// --------------------------------------------------------------------------
// Called by: speaker_play_tone()
// Purpose: store tone parameters and reset phase position.
SinWaveGenerator::SinWaveGenerator(int sample_rate, int frequency, float magnitude)
{
    m_sample_rate = sample_rate;
    m_frequency = frequency;
    m_magnitude = magnitude;
    m_current_position = 0;
}

// --------------------------------------------------------------------------
// SinWaveGenerator::getFrames
// --------------------------------------------------------------------------
// Called by: DACOutput writer task
// Purpose: fill the requested frame buffer with sine-wave samples.
void SinWaveGenerator::getFrames(Frame_t *frames, int number_frames)
{
    // How many output samples form one full waveform cycle.
    float full_wave_samples = (float)m_sample_rate / (float)m_frequency;

    // Phase step each sample so generated tone stays at target frequency.
    float step_per_sample = M_TWOPI / full_wave_samples;

    // Fill each output frame.
    for (int i = 0; i < number_frames; i++) {
        // Generate one sine sample and write to both channels.
        frames[i].left = frames[i].right =
            (uint16_t)(32768 + (int16_t)(32767 * m_magnitude * sinf(m_current_position)));

        // Advance phase for next sample.
        m_current_position += step_per_sample;

        // Wrap phase to keep value bounded and stable over long runs.
        if (m_current_position > M_TWOPI) {
            m_current_position -= M_TWOPI;
        }
    }
}
