// ============================================================================
// WAVFileReader.cpp
// ============================================================================
// In-memory WAV parser + frame provider.
//
// Call flow:
// - speaker_play_wav() or speaker_play_boot_sound() constructs WAVFileReader
// - DACOutput writer task calls getFrames() repeatedly
// - getFrames() outputs stereo frames in expected format

#include <Arduino.h>
#include <limits.h>
#include <string.h>

#include "WAVFileReader.h"

// --------------------------------------------------------------------------
// read_u32
// --------------------------------------------------------------------------
// Called by: WAVFileReader constructor
// Purpose: parse little-endian 32-bit RIFF fields.
static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

// --------------------------------------------------------------------------
// read_u16
// --------------------------------------------------------------------------
// Called by: WAVFileReader constructor
// Purpose: parse little-endian 16-bit RIFF fields.
static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// --------------------------------------------------------------------------
// apply_gain_and_clip
// --------------------------------------------------------------------------
// Called by: WAVFileReader::getFrames
// Purpose: apply Q15 gain and keep sample inside signed 16-bit range.
static int16_t apply_gain_and_clip(int16_t sample, int32_t gain_q15)
{
    int32_t scaled = ((int32_t)sample * gain_q15) / 32768;
    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

// --------------------------------------------------------------------------
// WAVFileReader::WAVFileReader
// --------------------------------------------------------------------------
// Called by: speaker.cpp playback functions
// Purpose: validate WAV header and locate fmt/data chunks.
WAVFileReader::WAVFileReader(const uint8_t *start, const uint8_t *end)
{
    // Initialize defaults so object remains safe even if parsing fails.
    m_data = nullptr;
    m_data_bytes = 0;
    m_num_channels = 1;
    m_sample_rate = 44100;
    m_pos = 0;
    m_gain_q15 = 32768; // Unity gain by default (no attenuation/boost).

    size_t total = (size_t)(end - start);

    // Smallest normal WAV header is 44 bytes.
    if (total < 44) {
        Serial.println("ERROR: WAV too small");
        return;
    }

    // Validate RIFF/WAVE signature.
    if (memcmp(start, "RIFF", 4) != 0 || memcmp(start + 8, "WAVE", 4) != 0) {
        Serial.println("ERROR: not a WAV file");
        return;
    }

    // Chunk scan begins after RIFF header.
    size_t pos = 12;
    bool found_fmt = false;

    // Walk chunk table until we find required pieces.
    while (pos + 8 <= total) {
        const uint8_t *chunk = start + pos;
        uint32_t chunk_size = read_u32(chunk + 4);

        // Parse format chunk (audio properties).
        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            uint16_t audio_format = read_u16(chunk + 8);
            m_num_channels = read_u16(chunk + 10);
            m_sample_rate = (int)read_u32(chunk + 12);
            uint16_t bit_depth = read_u16(chunk + 22);

            // Accept only PCM format currently.
            if (audio_format != 1) {
                Serial.printf("ERROR: audio_format %d not supported (PCM only)\n", audio_format);
                return;
            }

            // Accept only 16-bit samples currently.
            if (bit_depth != 16) {
                Serial.printf("ERROR: bit depth %d is not supported\n", bit_depth);
                return;
            }

            found_fmt = true;
            Serial.printf("fmt: audio_format=%d, num_channels=%d, sample_rate=%d, bit_depth=%d\n",
                          audio_format, m_num_channels, m_sample_rate, bit_depth);
        }
        // Parse data chunk (actual PCM payload).
        else if (memcmp(chunk, "data", 4) == 0) {
            m_data = chunk + 8;
            m_data_bytes = (int)chunk_size;
            Serial.printf("data: %d bytes\n", m_data_bytes);
            break;
        }

        // Advance to next chunk: 8-byte chunk header + payload size.
        pos += 8 + chunk_size;

        // RIFF chunks are word-aligned; odd payload includes one pad byte.
        if (chunk_size & 1U) {
            pos++;
        }
    }

    // Final guard: both fmt and data are required.
    if (!found_fmt || !m_data) {
        Serial.println("ERROR: WAV missing fmt or data chunk");
        m_data = nullptr;
        m_data_bytes = 0;
    }
}

// --------------------------------------------------------------------------
// WAVFileReader::setGain
// --------------------------------------------------------------------------
// Called by: speaker_play_wav / speaker_play_boot_sound
// Purpose: store playback gain once before DACOutput starts pulling frames.
void WAVFileReader::setGain(float gain)
{
    // Clamp input to a safe range: 0.0 (mute) to 2.0 (6 dB-ish boost).
    if (gain < 0.0f) {
        gain = 0.0f;
    } else if (gain > 2.0f) {
        gain = 2.0f;
    }
    m_gain_q15 = (int32_t)(gain * 32768.0f);
}

// --------------------------------------------------------------------------
// WAVFileReader::getFrames
// --------------------------------------------------------------------------
// Called by: DACOutput writer task
// Purpose: output `number_frames` stereo frames from parsed WAV payload.
void WAVFileReader::getFrames(Frame_t *frames, int number_frames)
{
    // Fill caller-provided frame buffer one frame at a time.
    for (int i = 0; i < number_frames; i++) {

        // If WAV is invalid/unavailable, emit silence for this frame.
        if (!m_data || m_data_bytes == 0) {
            frames[i].left = 32768;
            frames[i].right = 32768;
            continue;
        }

        // If we reached end, rewind so playback loops.
        if (m_pos >= m_data_bytes) {
            m_pos = 0;
        }

        int16_t left;
        int16_t right;

        // Read left sample.
        memcpy(&left, m_data + m_pos, sizeof(int16_t));
        m_pos += (int)sizeof(int16_t);

        // If mono input, duplicate left into right.
        if (m_num_channels == 1) {
            right = left;
        }
        // If stereo input, read explicit right sample, then downmix so the
        // single on-board DAC hears the full song instead of only one side.
        else {
            memcpy(&right, m_data + m_pos, sizeof(int16_t));
            m_pos += (int)sizeof(int16_t);

            int32_t mixed = ((int32_t)left + (int32_t)right) / 2;
            left = (int16_t)mixed;
            right = (int16_t)mixed;
        }

        // Apply user volume/gain before converting to unsigned DAC format.
        left = apply_gain_and_clip(left, m_gain_q15);
        right = apply_gain_and_clip(right, m_gain_q15);

        // Convert signed PCM [-32768..32767] to unsigned-centered [0..65535].
        frames[i].left = (uint16_t)(left + 32768);
        frames[i].right = (uint16_t)(right + 32768);
    }
}
