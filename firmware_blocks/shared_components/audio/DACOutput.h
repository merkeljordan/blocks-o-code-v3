#ifndef __sampler_base_h__
#define __sampler_base_h__

#include <Arduino.h>
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"

class SampleSource;

class DACOutput
{
private:
    TaskHandle_t m_i2sWriterTaskHandle = nullptr;
    QueueHandle_t m_i2sQueue = nullptr;
    SampleSource *m_sample_generator = nullptr;
    uint32_t m_active_sample_rate = 0;

public:
    void start(SampleSource *sample_generator);
    void setSampleSource(SampleSource *source);

    friend void i2sWriterTask(void *param);
};

#endif
