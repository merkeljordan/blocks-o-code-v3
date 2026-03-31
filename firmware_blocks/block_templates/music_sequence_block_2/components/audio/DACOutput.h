#ifndef __sampler_base_h__
#define __sampler_base_h__

#include <Arduino.h>
#include "driver/i2s.h"

class SampleSource;

/*
 * I2S DAC output bridge.
 *
 * I adapted this from example code and kept the same core idea:
 * a background writer task keeps feeding I2S while higher-level code only
 * swaps the active SampleSource.
 */
class DACOutput
{
private:
    TaskHandle_t m_i2sWriterTaskHandle;
    QueueHandle_t m_i2sQueue;
    SampleSource *m_sample_generator;

public:
    /* Called by: speaker_init()
     * Configures I2S/DAC and spawns the writer task.
     */
    void start(SampleSource *sample_generator);

    /* Called by: speaker_play_wav(), speaker_play_tone(), speaker_stop()
     * Swaps active sample source consumed by i2sWriterTask.
     */
    void setSampleSource(SampleSource *source);

    /* Friend task entrypoint declared in DACOutput.cpp. */
    friend void i2sWriterTask(void *param);
};

#endif
