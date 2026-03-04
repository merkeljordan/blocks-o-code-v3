#include <math.h>
#include "SinWaveGenerator.h"

SinWaveGenerator::SinWaveGenerator(int sample_rate, int frequency, float magnitude)
{
    m_sample_rate = sample_rate;
    m_frequency = frequency;
    m_magnitude = magnitude;
    m_current_position = 0;
}

void SinWaveGenerator::getFrames(Frame_t *frames, int number_frames)
{
    float full_wave_samples = (float)m_sample_rate / m_frequency;
    float step_per_sample = M_TWOPI / full_wave_samples;
    for (int i = 0; i < number_frames; i++)
    {
        frames[i].left = frames[i].right = 32768 + (int16_t)(32767 * m_magnitude * sin(m_current_position));
        m_current_position += step_per_sample;
        if (m_current_position > M_TWOPI)
        {
            m_current_position -= M_TWOPI;
        }
    }
}
