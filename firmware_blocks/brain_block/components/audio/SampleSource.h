#ifndef __sample_source_h__
#define __sample_source_h__

#include <Arduino.h>

/* Stereo frame in unsigned-centered 16-bit format expected by DACOutput. */
typedef struct {
    uint16_t left;
    uint16_t right;
} Frame_t;

/* Polymorphic audio source interface consumed by DACOutput writer task. */
class SampleSource
{
public:
    virtual ~SampleSource() = default;

    /* Called by: DACOutput::start() during configuration. */
    virtual int sampleRate() = 0;

    /* Called by: i2sWriterTask repeatedly to fill audio frame buffers. */
    virtual void getFrames(Frame_t *frames, int number_frames) = 0;
};

#endif
