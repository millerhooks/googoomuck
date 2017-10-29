//-----------------------------------------------------------------------------
/*

Audio Control for the STM32F4 Discovery Board

*/
//-----------------------------------------------------------------------------

#ifndef AUDIO_H
#define AUDIO_H

//-----------------------------------------------------------------------------

#include "stm32f4_soc.h"
#include "cs43l22.h"
#include "utils.h"

//-----------------------------------------------------------------------------

// Total bits/sec = AUDIO_SAMPLE_RATE * N bits per channel * 2 channels
// This is used to lookup the i2s clock configuration in a table.
#define AUDIO_SAMPLE_RATE 35156U	// Hz

// The hardware is often not capable of the exact sample rate.
// This is the sample rate we actually get per the clock divider settings.
// See ./scripts/i2sclk.py for details.
#define AUDIO_FS 35156.25f	// Hz

// This is the size (in bytes) of the buffer that is DMAed
// from memory to the I2S device.
#define AUDIO_BUFFER_SIZE 256

//-----------------------------------------------------------------------------

struct audio_drv {
	struct dma_drv dma;
	struct i2s_drv i2s;
	struct i2c_drv i2c;
	struct cs4x_drv dac;
	uint8_t buffer[AUDIO_BUFFER_SIZE] ALIGN(4);	// dma->i2s buffer
};

extern struct audio_drv ggm_audio;

//-----------------------------------------------------------------------------

int audio_init(struct audio_drv *audio);
int audio_start(struct audio_drv *audio);

void audio_wr(struct audio_drv *audio, float ch_l, float ch_r);
void audio_master_volume(struct audio_drv *audio, uint8_t vol);

// This is a callback from the dma interrupt handler to the synthesizer.
// It requests the synth to generate and write samples to the upper or
// lower half of the audio buffer.
void audio_generate(int index);

//-----------------------------------------------------------------------------

#endif				// AUDIO_H

//-----------------------------------------------------------------------------
