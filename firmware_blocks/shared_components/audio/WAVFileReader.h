#ifndef __wav_file_reader_h__
#define __wav_file_reader_h__

#include "SampleSource.h"

class WAVFileReader : public SampleSource
{
private:
    int m_num_channels;
    int m_sample_rate;
    const uint8_t *m_data;
    int m_data_bytes;
    int m_pos;
    float m_gain;

public:
    WAVFileReader(const uint8_t *start, const uint8_t *end);
    void setGain(float gain) { m_gain = gain; }
    int sampleRate() { return m_sample_rate; }
    int numChannels() const { return m_num_channels; }
    /** PCM 16-bit: sample_rate * channels * 2 */
    int bytesPerSecond() const { return m_sample_rate * m_num_channels * 2; }
    int getDataBytes() { return m_data_bytes; }
    int getPos() { return m_pos; }
    void resetPos() { m_pos = 0; }
    void getFrames(Frame_t *frames, int number_frames);
};

#endif
