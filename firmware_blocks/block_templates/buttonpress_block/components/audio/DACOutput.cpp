
#include <Arduino.h>
#include "driver/i2s.h"
#include <math.h>

#include "SampleSource.h"
#include "DACOutput.h"

#define NUM_FRAMES_TO_SEND 128

void i2sWriterTask(void *param)
{
    DACOutput *output = (DACOutput *)param;
    int availableBytes = 0;
    int buffer_position = 0;
    Frame_t frames[128];
    while (true)
    {
        i2s_event_t evt;
        if (xQueueReceive(output->m_i2sQueue, &evt, portMAX_DELAY) == pdPASS)
        {
            if (evt.type == I2S_EVENT_TX_DONE)
            {
                size_t bytesWritten = 0;
                do
                {
                    if (availableBytes == 0)
                    {
                        output->m_sample_generator->getFrames(frames, NUM_FRAMES_TO_SEND);
                        availableBytes = NUM_FRAMES_TO_SEND * sizeof(uint32_t);
                        buffer_position = 0;
                    }
                    if (availableBytes > 0)
                    {
                        i2s_write(I2S_NUM_0, buffer_position + (uint8_t *)frames,
                                  availableBytes, &bytesWritten, portMAX_DELAY);
                        availableBytes -= bytesWritten;
                        buffer_position += bytesWritten;
                    }
                } while (bytesWritten > 0);
            }
        }
    }
}

void DACOutput::start(SampleSource *sample_generator)
{
    m_sample_generator = sample_generator;
    i2s_config_t i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = (uint32_t)m_sample_generator->sampleRate(),
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 64};

    i2s_driver_install(I2S_NUM_0, &i2sConfig, 4, &m_i2sQueue);
    i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);
    i2s_zero_dma_buffer(I2S_NUM_0);

    TaskHandle_t writerTaskHandle;
    xTaskCreate(i2sWriterTask, "i2s Writer Task", 4096, this, 1, &writerTaskHandle);
}
