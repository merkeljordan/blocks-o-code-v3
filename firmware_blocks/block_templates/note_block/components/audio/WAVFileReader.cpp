#include <Arduino.h>
#include <string.h>
#include "WAVFileReader.h"

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

WAVFileReader::WAVFileReader(const uint8_t *start, const uint8_t *end)
{
    m_data = nullptr;
    m_data_bytes = 0;
    m_num_channels = 1;
    m_sample_rate = 44100;
    m_pos = 0;

    size_t total = (size_t)(end - start);
    if (total < 44) {
        Serial.println("ERROR: WAV too small");
        return;
    }

    if (memcmp(start, "RIFF", 4) != 0 || memcmp(start + 8, "WAVE", 4) != 0) {
        Serial.println("ERROR: not a WAV file");
        return;
    }

    size_t pos = 12;
    bool found_fmt = false;

    while (pos + 8 <= total) {
        const uint8_t *chunk = start + pos;
        uint32_t chunk_size = read_u32(chunk + 4);

        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            uint16_t audio_format = read_u16(chunk + 8);
            m_num_channels = read_u16(chunk + 10);
            m_sample_rate = (int)read_u32(chunk + 12);
            uint16_t bit_depth = read_u16(chunk + 22);

            if (audio_format != 1) {
                Serial.printf("ERROR: audio_format %d not supported (PCM only)\n", audio_format);
                return;
            }
            if (bit_depth != 16) {
                Serial.printf("ERROR: bit depth %d is not supported\n", bit_depth);
                return;
            }
            found_fmt = true;

            Serial.printf("fmt: audio_format=%d, num_channels=%d, sample_rate=%d, bit_depth=%d\n",
                          audio_format, m_num_channels, m_sample_rate, bit_depth);
        }
        else if (memcmp(chunk, "data", 4) == 0) {
            m_data = chunk + 8;
            m_data_bytes = (int)chunk_size;
            Serial.printf("data: %d bytes\n", m_data_bytes);
            break;
        }

        pos += 8 + chunk_size;
        if (chunk_size & 1) pos++;
    }

    if (!found_fmt || !m_data) {
        Serial.println("ERROR: WAV missing fmt or data chunk");
        m_data = nullptr;
        m_data_bytes = 0;
    }
}

void WAVFileReader::getFrames(Frame_t *frames, int number_frames)
{
    for (int i = 0; i < number_frames; i++)
    {
        if (!m_data || m_data_bytes == 0) {
            frames[i].left = 32768;
            frames[i].right = 32768;
            continue;
        }

        if (m_pos >= m_data_bytes)
        {
            m_pos = 0;
        }
        int16_t left;
        int16_t right;
        memcpy(&left, m_data + m_pos, sizeof(int16_t));
        m_pos += sizeof(int16_t);
        if (m_num_channels == 1)
        {
            right = left;
        }
        else
        {
            memcpy(&right, m_data + m_pos, sizeof(int16_t));
            m_pos += sizeof(int16_t);
        }
        frames[i].left = left + 32768;
        frames[i].right = right + 32768;
    }
}
