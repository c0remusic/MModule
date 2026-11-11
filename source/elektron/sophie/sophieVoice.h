/*
 * Sophie — an original metallic percussion synthesizer for Schwung.
 * Copyright (c) 2026 Matt Estela, Copyright (c) 2026 digisophie contributors. MIT, see LICENSE.
 *
 * One Sophie voice per instance, ported from src/dsp/sophie.c (README.md names the upstream commit). The 16-pad
 * kit, the 12-voice pool, the JSON state and the Schwung plug-in API are left out: the caller holds one sophie_t per
 * Track, sets its parameters, triggers it and renders it in blocks of any size.
 */

#ifndef SOPHIE_VOICE_H
#define SOPHIE_VOICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOPHIE_PADS 16
#define SOPHIE_FIRST_NOTE 36
#define SOPHIE_MAX_DELAY_SAMPLES 1536 /* ~34 ms at 44.1 kHz */
#define SOPHIE_MIX_GAIN 0.24f         /* what Sophie's render_block multiplies its voices' sum by */

/* The 19 parameters of a pad in the order of the machine page, then Level (the Track's level does that job). */
typedef enum {
    SOPHIE_MODEL,         /* 0-5: Fuse, Stack, Split, Shard, Pipe, Boom */
    SOPHIE_TUNE,          /* -24..24 semitones */
    SOPHIE_COLOR,         /* 0..100 % */
    SOPHIE_METAL,         /* 0..100 % */
    SOPHIE_FEEDBACK,      /* 0..100 % */
    SOPHIE_SWEEP,         /* -100..100 % */
    SOPHIE_DECAY,         /* 0.03..4 s */
    SOPHIE_CRUSH,         /* 0..100 % */
    SOPHIE_DRIVE,         /* 0..100 % */
    SOPHIE_CUTOFF,        /* 0..100 % */
    SOPHIE_RESONANCE,     /* 0..100 % */
    SOPHIE_FILTER_TYPE,   /* 0-4: LPF, HPF, Notch, DJ, BPF */
    SOPHIE_RING_TIME,     /* 0.5..30 ms */
    SOPHIE_RING_FEEDBACK, /* 0..95 % */
    SOPHIE_RING_MIX,      /* 0..100 % */
    SOPHIE_RING_TONE,     /* 0..100 % */
    SOPHIE_METAL_DECAY,   /* 0..2000 ms, 0 keeps the pitch-linked contour */
    SOPHIE_FEEDBACK_MODE, /* 0 Raw, 1 Smooth */
    SOPHIE_TAIL_MODE,     /* 0 Hit, 1 Live: latched at the trigger */
    SOPHIE_LEVEL,         /* 0..100 % */
    SOPHIE_PARAM_COUNT
} sophie_param_t;

typedef struct {
    int model;
    float tune;
    float color;
    float metal;
    float feedback;
    float sweep;
    float decay;
    float crush;
    float drive;
    float level;
    float cutoff;
    float resonance;
    int filter_type;
    float ring_time;
    float ring_feedback;
    float ring_mix;
    float ring_tone;
    float metal_decay;
    int feedback_mode;
    int tail_mode;
} sophie_patch_t;

typedef struct {
    float carrier_phase;
    float mod1_phase;
    float mod2_phase;
    float mod3_phase;
    float sub_phase, upper_phase, split_phase;
    float metal_env, metal_mul, amp_mul, pitch_mul;
    float color_s, metal_s, feedback_s, cutoff_s, resonance_s;
    float last, steal_tail;
    int steal_left;
    float amp_env;
    float pitch_env;
    float attack;
    float feedback_z;
    float filter_low;
    float filter_band;
    float noise_z;
    float base_hz;
    float velocity;
    float crush_hold;
    int crush_count;
    float delay_buf[SOPHIE_MAX_DELAY_SAMPLES];
    float delay_lp;
    int delay_pos;
    int body; /* the kick body for models 0-3, as upstream's pad 1 plays it */
    sophie_patch_t patch;
    uint32_t rng;
    int active;
} sophie_voice_t;

typedef struct {
    sophie_voice_t voice;
    sophie_patch_t patch; /* the pad's current settings: the next trigger takes them, a Live tail follows them */
    int pad;              /* 0-15: the base note 36 + pad */
    int body;             /* the kick body for models 0-3: upstream plays it on pad 0 alone */
    int sample_rate;
    float control_slew, feedback_slew;
    int steal_samples;
    uint32_t rng;
} sophie_t;

/* The factory kit's patch of a pad (0-15, General MIDI drum order: 0 is the kick). */
sophie_patch_t sophie_factory_patch(int pad);

/* Silent voice, factory patch of the pad, Sophie's slews for the sample rate. Allocates nothing. */
void sophie_init(sophie_t *s, int sample_rate, int pad);

/* The next trigger's base note, and the kick body on pad 0 alone as upstream has it. */
void sophie_set_pad(sophie_t *s, int pad);

/* The next trigger's kick body (models 0-3), on any pad: what sophie_set_pad chose, changed. */
void sophie_set_body(sophie_t *s, int on);

/* Clamped as Sophie clamps. Returns 0 for a value that is not finite or a parameter out of range. */
int sophie_set_param(sophie_t *s, sophie_param_t param, float value);
float sophie_get_param(const sophie_t *s, sophie_param_t param);

/* A hit at a velocity of 1-127 (0 does nothing, as a note-on of velocity 0 in Sophie). A ringing voice is
 * replaced with Sophie's 1.5 ms steal blend. */
void sophie_trigger(sophie_t *s, int velocity);

/* Writes the voice's mono output, before Sophie's SOPHIE_MIX_GAIN. */
void sophie_render(sophie_t *s, float *out, int frames);

int sophie_active(const sophie_t *s);

#ifdef __cplusplus
}
#endif

#endif
