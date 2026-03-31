#ifndef __sampler_base_h__
#define __sampler_base_h__

#include <Arduino.h>
#include "driver/i2s.h"

class SampleSource;

class DACOutput
{
private:
    TaskHandle_t m_i2sWriterTaskHandle;
    QueueHandle_t m_i2sQueue;
    SampleSource *m_sample_generator;

public:
    void start(SampleSource *sample_generator);
    void setSampleSource(SampleSource *source) { m_sample_generator = source; }

    friend void i2sWriterTask(void *param);
};

#endif
