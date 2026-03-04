#ifndef __sample_source_h__
#define __sample_source_h__

#include <Arduino.h>

typedef struct
{
    uint16_t left;
    uint16_t right;
} Frame_t;

class SampleSource
{
public:
    virtual int sampleRate() = 0;
    virtual void getFrames(Frame_t *frames, int number_frames) = 0;
};

#endif
