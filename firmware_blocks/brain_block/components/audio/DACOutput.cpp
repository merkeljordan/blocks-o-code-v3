// ============================================================================
// DACOutput.cpp
// ============================================================================
// Low-level I2S DAC writer.
//
// Call flow:
// - speaker_init() -> DACOutput::start(...)
// - start() installs I2S driver and spawns i2sWriterTask
// - i2sWriterTask waits for TX-done events and keeps writing frames

#include <Arduino.h>
#include "driver/i2s.h"
#include "sdkconfig.h"

#include "DACOutput.h"
#include "SampleSource.h"

// Number of stereo frames requested each refill cycle.
#define NUM_FRAMES_TO_SEND 128

// Pin low-level audio writer on a dedicated core in dual-core builds.
#if CONFIG_FREERTOS_UNICORE
#define AUDIO_WRITER_CORE_ID 0
#else
#define AUDIO_WRITER_CORE_ID 0
#endif

// --------------------------------------------------------------------------
// i2sWriterTask
// --------------------------------------------------------------------------
// Called by: xTaskCreate() in DACOutput::start
// Calls: active SampleSource::getFrames + i2s_write
void i2sWriterTask(void *param)
{
    DACOutput *output = (DACOutput *)param;

    // Tracks how many bytes remain in local frame buffer for current cycle.
    int availableBytes = 0;

    // Byte offset into local frame buffer when partial writes happen.
    int buffer_position = 0;

    // Local frame staging buffer that gets filled from sample source.
    Frame_t frames[NUM_FRAMES_TO_SEND];

    // Task runs forever while audio subsystem is active.
    while (true) {
        i2s_event_t evt;

        // Block here until I2S driver signals an event.
        if (xQueueReceive(output->m_i2sQueue, &evt, portMAX_DELAY) == pdPASS) {

            // We refill/send only when a TX buffer has finished.
            if (evt.type == I2S_EVENT_TX_DONE) {
                size_t bytesWritten = 0;

                // Keep writing until i2s_write reports no more immediate progress.
                do {
                    // If local staging buffer is empty, request a fresh chunk.
                    if (availableBytes == 0) {
                        output->m_sample_generator->getFrames(frames, NUM_FRAMES_TO_SEND);
                        availableBytes = NUM_FRAMES_TO_SEND * (int)sizeof(uint32_t);
                        buffer_position = 0;
                    }

                    // If we still have staged bytes, push them to I2S DMA.
                    if (availableBytes > 0) {
                        i2s_write(I2S_NUM_0,
                                  buffer_position + (uint8_t *)frames,
                                  (size_t)availableBytes,
                                  &bytesWritten,
                                  portMAX_DELAY);

                        // Move forward by the amount successfully written.
                        availableBytes -= (int)bytesWritten;
                        buffer_position += (int)bytesWritten;
                    }
                } while (bytesWritten > 0);
            }
        }
    }
}

// --------------------------------------------------------------------------
// DACOutput::start
// --------------------------------------------------------------------------
// Called by: speaker_init() in speaker.cpp
// Calls: I2S driver setup APIs + task creation.
void DACOutput::start(SampleSource *sample_generator)
{
    // Save initial active source (usually SilenceSource).
    m_sample_generator = sample_generator;

    // Configure ESP32 built-in DAC mode via I2S peripheral.
    i2s_config_t i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = (uint32_t)m_sample_generator->sampleRate(),
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 64,
    };

    // Install I2S driver and create event queue used by writer task.
    i2s_driver_install(I2S_NUM_0, &i2sConfig, 4, &m_i2sQueue);

    // Route output to built-in right DAC channel (GPIO25 on classic ESP32).
    i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);

    // Start with cleared DMA buffers.
    i2s_zero_dma_buffer(I2S_NUM_0);

    // Spawn writer task pinned to the audio core.
    xTaskCreatePinnedToCore(i2sWriterTask,
                            "i2s Writer Task",
                            4096,
                            this,
                            1,
                            &m_i2sWriterTaskHandle,
                            AUDIO_WRITER_CORE_ID);
}
