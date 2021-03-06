//-----------------------------------------------------------------------------
/*

GooGooMuck Synthesizer

*/
//-----------------------------------------------------------------------------

#ifndef GGM_H
#define GGM_H

//-----------------------------------------------------------------------------

#include <inttypes.h>
#include <stddef.h>

#include "audio.h"

//-----------------------------------------------------------------------------

#define AUDIO_TS (1.f/AUDIO_FS)

#define PI (3.1415927f)
#define TAU (2.f * PI)
#define INV_TAU (1.f/TAU)

//-----------------------------------------------------------------------------
// block operations

void block_mul(float *out, float *buf, size_t n);
void block_mul_k(float *out, float k, size_t n);
void block_add(float *out, float *buf, size_t n);
void block_add_k(float *out, float k, size_t n);
void block_copy(float *dst, float *src, size_t n);

//-----------------------------------------------------------------------------
// lookup tables

#define COS_BITS (6U)
#define COS_SIZE (1U << COS_BITS)
extern const uint32_t COS_data[COS_SIZE << 1];

//-----------------------------------------------------------------------------
// Sine Wave Oscillators

struct sin {
	float freq;		// base frequency
	uint32_t x;		// current x-value
	uint32_t xstep;		// current x-step
};

float sin_eval(float x);
float cos_eval(float x);
void sin_init(struct sin *osc, float freq);
void sin_gen(struct sin *osc, float *out, float *fm, size_t n);

//-----------------------------------------------------------------------------
// Goom Waves

struct gwave {
	float freq;		// base frequency
	uint32_t tp;		// s0f0 to s1f1 transition point
	float k0;		// scaling factor for slope 0
	float k1;		// scaling factor for slope 1
	uint32_t x;		// phase position
	uint32_t xstep;		// phase step per sample
};

void gwave_init(struct gwave *osc, float freq);
void gwave_shape(struct gwave *osc, float duty, float slope);
void gwave_gen(struct gwave *osc, float *out, float *fm, size_t n);

//-----------------------------------------------------------------------------
// ADSR envelope

struct adsr {
	float s;		// sustain level
	float ka;		// attack constant
	float kd;		// decay constant
	float kr;		// release constant
	float d_trigger;	// attack->decay trigger level
	float s_trigger;	// decay->sustain trigger level
	float i_trigger;	// release->idle trigger level
	int state;		// envelope state
	float val;		// output value
};

// generators
void adsr_gen(struct adsr *e, float *out, size_t n);

// envelopes
void adsr_init(struct adsr *e, float a, float d, float s, float r);
void ad_init(struct adsr *e, float a, float d);

// actions
void adsr_attack(struct adsr *e);
void adsr_release(struct adsr *e);
void adsr_idle(struct adsr *e);

// state
int adsr_is_active(struct adsr *e);

//-----------------------------------------------------------------------------
// Karplus Strong

#define KS_DELAY_BITS (6U)
#define KS_DELAY_SIZE (1U << KS_DELAY_BITS)

struct ks {
	float freq;		// base frequency
	float delay[KS_DELAY_SIZE];
	float k;		// attenuation and averaging constant 0 to 0.5
	uint32_t x;		// phase position
	uint32_t xstep;		// phase step per sample
};

void ks_init(struct ks *osc, float freq, float attenuate);
void ks_attenuate(struct ks *osc, float attenuate);
void ks_pluck(struct ks *osc);
void ks_gen(struct ks *osc, float *out, size_t n);

//-----------------------------------------------------------------------------
// Low Pass Filters

struct svf {
	float kf;		// constant for cutoff frequency
	float kq;		// constant for filter resonance
	float bp;		// bandpass state variable
	float lp;		// low pass state variable
};

void svf_init(struct svf *f, float cutoff, float resonance);
void svf_gen(struct svf *f, float *out, float *in, float *x, size_t n);

//-----------------------------------------------------------------------------
// midi

// midi message receiver
struct midi_rx {
	struct ggm *ggm;	// pointer back to the parent ggm state
	void (*func) (struct midi_rx * midi);	// event function
	int state;		// rx state
	uint8_t status;		// message status byte
	uint8_t arg0;		// message byte 0
	uint8_t arg1;		// message byte 1
};

void midi_rx_serial(struct midi_rx *midi, struct usart_drv *serial);
float midi_map(uint8_t val, float a, float b);
float midi_to_frequency(float note);

//-----------------------------------------------------------------------------
// events

// event type in the upper 8 bits
#define EVENT_TYPE(x) ((x) & 0xff000000U)
#define EVENT_TYPE_KEY_DN (1U << 24)
#define EVENT_TYPE_KEY_UP (2U << 24)
#define EVENT_TYPE_MIDI (3U << 24)
#define EVENT_TYPE_AUDIO (4U << 24)

// key number in the lower 8 bits
#define EVENT_KEY(x) ((x) & 0xffU)
// midi message in the lower 3 bytes
#define EVENT_MIDI(x) ((x) & 0xffffffU)
// audio block size in the lower 16 bits
#define EVENT_BLOCK_SIZE(x) ((x) & 0xffffU)

struct event {
	uint32_t type;		// the event type
	void *ptr;		// pointer to event data (or data itself)
};

int event_init(void);
int event_rd(struct event *event);
int event_wr(uint32_t type, void *ptr);

//-----------------------------------------------------------------------------
// voices

#define VOICE_STATE_SIZE 1024

struct voice {
	int idx;		// index in table
	uint8_t note;		// current note
	uint8_t channel;	// current channel
	struct patch *patch;	// patch in use
	uint8_t state[VOICE_STATE_SIZE];	// per voice state
};

struct voice *voice_lookup(struct ggm *s, uint8_t channel, uint8_t note);
struct voice *voice_alloc(struct ggm *s, uint8_t channel, uint8_t note);

//-----------------------------------------------------------------------------
// patches

// patch operations
struct patch_ops {
	// voice functions
	void (*start) (struct voice * v);	// start a voice
	void (*stop) (struct voice * v);	// stop a voice
	void (*note_on) (struct voice * v, uint8_t vel);
	void (*note_off) (struct voice * v, uint8_t vel);
	int (*active) (struct voice * v);	// is the voice active
	void (*generate) (struct voice * v, float *out_l, float *out_r, size_t n);	// generate samples
	// patch functions
	void (*init) (struct patch * p);
	void (*control_change) (struct patch * p, uint8_t ctrl, uint8_t val);
	void (*pitch_wheel) (struct patch * p, uint16_t val);
};

#define PATCH_STATE_SIZE 128

struct patch {
	struct ggm *ggm;	// pointer back to the parent ggm state
	const struct patch_ops *ops;
	uint8_t state[PATCH_STATE_SIZE];	// per patch state
};

// implemented patches
extern const struct patch_ops patch0;
extern const struct patch_ops patch1;
extern const struct patch_ops patch2;
extern const struct patch_ops patch3;
extern const struct patch_ops patch4;

//-----------------------------------------------------------------------------

// number of simultaneous voices
#define NUM_VOICES 16
// number of concurrent channels
#define NUM_CHANNELS 16

struct ggm {
	struct audio_drv *audio;	// audio output
	struct usart_drv *serial;	// serial port for midi interface
	struct midi_rx midi_rx0;	// midi rx from the serial port
	struct patch patches[NUM_CHANNELS];	// current patch set
	struct voice voices[NUM_VOICES];	// voices
	int voice_idx;		// FIXME round robin voice allocation
};

int ggm_init(struct ggm *s, struct audio_drv *audio, struct usart_drv *midi);
int ggm_run(struct ggm *s);

//-----------------------------------------------------------------------------

#endif				// GGM_H

//-----------------------------------------------------------------------------
