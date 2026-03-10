#ifndef __sin_wave_generator_h__
#define __sin_wave_generator_h__

#include "SampleSource.h"

/* Procedural sine-wave generator I use for short beeps/tones. */
class SinWaveGenerator : public SampleSource
{
private:
    int m_sample_rate;
    int m_frequency;
    float m_magnitude;
    float m_current_position;

public:
    /* Called by: speaker_play_tone()
     * sample_rate controls output timing, frequency sets tone pitch, magnitude sets amplitude.
     */
    SinWaveGenerator(int sample_rate, int frequency, float magnitude);

    /* Called by: DACOutput::start() to determine stream sample rate. */
    int sampleRate() override { return m_sample_rate; }

    /* Called by: i2sWriterTask to fill outbound audio frames. */
    void getFrames(Frame_t *frames, int number_frames) override;
};

#endif
