#ifndef __wav_file_reader_h__
#define __wav_file_reader_h__

#include "SampleSource.h"

/*
 * Lightweight WAV parser for in-memory assets.
 *
 * I only support the formats we actually use in this project:
 * - RIFF/WAVE
 * - PCM (audio_format = 1)
 * - 16-bit samples
 * - mono or stereo
 */
class WAVFileReader : public SampleSource
{
private:
    int m_num_channels;
    int m_bits_per_sample;
    int m_sample_rate;
    const uint8_t *m_data;
    int m_data_bytes;
    int m_pos;
    // Q15 gain scalar (32768 = 1.0x, 16384 = 0.5x, 0 = mute).
    int32_t m_gain_q15;

public:
    /* Called by: speaker_play_wav()/speaker_play_boot_sound()
     * `start` and `end` are linker pointers to embedded WAV bytes.
     */
    WAVFileReader(const uint8_t *start, const uint8_t *end);

    /* Called by: DACOutput::start() */
    int sampleRate() override { return m_sample_rate; }

    /* Called by: playback helpers to estimate blocking duration. */
    int getDataBytes() { return m_data_bytes; }
    int getNumChannels() { return m_num_channels; }
    int getBitsPerSample() { return m_bits_per_sample; }

    /* Debug/inspection helpers */
    int getPos() { return m_pos; }
    void resetPos() { m_pos = 0; }

    /* Fill output frames; loops back to start when end of data is reached. */
    void getFrames(Frame_t *frames, int number_frames) override;

    /* Called by: speaker.cpp before playback starts.
     * Accepts linear gain in 0.0..2.0 range (values outside are clamped).
     */
    void setGain(float gain);
};

#endif
