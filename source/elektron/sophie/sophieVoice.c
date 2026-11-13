/*
 * Sophie — an original metallic percussion synthesizer for Schwung.
 * Copyright (c) 2026 Matt Estela, Copyright (c) 2026 digisophie contributors. MIT, see LICENSE.
 *
 * PIPE, BOOM and the smooth-feedback/brightness ideas are adapted from
 * soejrd/digisophie (MIT), commit ee4ff40.
 *
 * Ported from src/dsp/sophie.c: one voice per instance. render_voice is the original's, its pad target read from the
 * instance's patch, with what depends on the voice's patch alone computed once a block (sophie_block_t) and the
 * filter's coefficient again only when its cutoff moves: the same expressions, the same samples, for a
 * fifth to a half less of the cost. trigger_voice is the original's with a pool of one voice. Upstream's "pad_index == 0" (the kick body) is a
 * flag of its own, so that any pad can play the body.
 */

#include "sophieVoice.h"

#include <math.h>
#include <string.h>

#define SOPHIE_PI 3.14159265358979323846f
#define SOPHIE_TAU (2.0f * SOPHIE_PI)

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static float wrap_phase(float x) {
    if (x >= SOPHIE_TAU) x -= SOPHIE_TAU;
    if (x < 0.0f) x += SOPHIE_TAU;
    return x;
}

static float wrap_continuous(float x) {
    /* A phase wrap_phase left inside the turn needs no floorf: floorf(q) is +0 there, and x - 0 is x */
    float q = x / SOPHIE_TAU;
    if (q > 0.0f && q < 1.0f) return x;
    return x - floorf(q) * SOPHIE_TAU;
}

static float soft_clip(float x) {
    float ax = fabsf(x);
    return x / (1.0f + ax);
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state ? *state : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float noise_bipolar(uint32_t *state) {
    return (float)(xorshift32(state) & 0x00ffffffu) * (2.0f / 16777215.0f) - 1.0f;
}

static float midi_hz(int note) {
    return 440.0f * exp2f(((float)note - 69.0f) / 12.0f);
}

static sophie_patch_t patch(int model, float tune, float color, float metal, float feedback,
                            float sweep, float decay, float crush, float drive) {
    sophie_patch_t p = {model, tune, color, metal, feedback, sweep, decay, crush, drive};
    p.level = 100.0f; p.cutoff = 100.0f; p.resonance = 0.0f; p.filter_type = 0;
    p.ring_time = 6.0f; p.ring_feedback = 0.0f; p.ring_mix = 0.0f; p.ring_tone = 70.0f;
    return p;
}

sophie_patch_t sophie_factory_patch(int pad) {
    /* GM/Ableton Drum Rack order, MIDI notes 36–51. */
    switch (pad) {
    default:
    case 0:  return patch(0, -5, 24,  8,  6,  55, 0.28f,  0, 58); /* Kick */
    case 1:  return patch(3,  0, 76, 72, 34,  16, 0.09f, 48, 58); /* Rim */
    case 2:  return patch(3,  0, 52, 76, 18,  28, 0.24f, 30, 52); /* Snare */
    case 3:  return patch(2,  0, 84, 88, 42,  -8, 0.19f, 56, 68); /* Clap */
    case 4:  return patch(1,  0, 63, 82, 28,  24, 0.18f, 38, 58); /* Snare 2 */
    case 5:  return patch(0,  0, 24, 44, 16,  58, 0.42f,  8, 48); /* Low Tom */
    case 6:  return patch(3,  0, 94, 98, 58,   0, 0.055f, 52, 62); /* Closed HH */
    case 7:  return patch(0,  0, 31, 48, 20,  54, 0.47f, 10, 50); /* Floor Tom */
    case 8:  return patch(3,  0, 88, 94, 50,   2, 0.10f, 46, 58); /* Pedal HH */
    case 9:  return patch(0,  0, 38, 52, 22,  48, 0.36f, 12, 52); /* Mid Tom */
    case 10: return patch(3,  0, 91, 96, 62,  -4, 0.48f, 42, 60); /* Open HH */
    case 11: return patch(0,  0, 45, 56, 25,  44, 0.31f, 14, 54); /* Low-mid Tom */
    case 12: return patch(0,  0, 51, 60, 28,  40, 0.27f, 16, 56); /* High-mid Tom */
    case 13: return patch(1,  0, 78, 92, 72, -12, 1.65f, 28, 54); /* Crash */
    case 14: return patch(0,  0, 58, 64, 30,  36, 0.23f, 18, 58); /* High Tom */
    case 15: return patch(2,  0, 69, 86, 66,  -6, 1.15f, 22, 52); /* Ride */
    }
}

void sophie_init(sophie_t *s, int sample_rate, int pad) {
    memset(s, 0, sizeof(*s));
    s->sample_rate = sample_rate;
    s->control_slew = 1.0f - expf(-1.0f / (0.005f * s->sample_rate));
    s->feedback_slew = 1.0f - expf(logf(0.875f) * 24000.0f / s->sample_rate);
    s->steal_samples = (int)(0.0015f * s->sample_rate);
    if (s->steal_samples < 1) s->steal_samples = 1;
    s->rng = 0x534f5048u;
    sophie_set_pad(s, pad);
    s->patch = sophie_factory_patch(s->pad);
}

void sophie_set_pad(sophie_t *s, int pad) {
    s->pad = pad < 0 ? 0 : (pad >= SOPHIE_PADS ? SOPHIE_PADS - 1 : pad);
    s->body = s->pad == 0;
}

void sophie_set_body(sophie_t *s, int on) {
    s->body = on != 0;
}

int sophie_set_param(sophie_t *s, sophie_param_t param, float value) {
    sophie_patch_t *p = &s->patch;
    if (!isfinite(value)) return 0;
    switch (param) {
    case SOPHIE_MODEL: p->model = (int)clampf(value, 0.0f, 5.0f); break;
    case SOPHIE_METAL_DECAY: p->metal_decay = clampf(value, 0, 2000); break;
    case SOPHIE_FEEDBACK_MODE: p->feedback_mode = (int)clampf(value, 0, 1); break;
    case SOPHIE_TAIL_MODE: p->tail_mode = (int)clampf(value, 0, 1); break;
    case SOPHIE_TUNE: p->tune = clampf(value, -24.0f, 24.0f); break;
    case SOPHIE_COLOR: p->color = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_METAL: p->metal = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_FEEDBACK: p->feedback = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_SWEEP: p->sweep = clampf(value, -100.0f, 100.0f); break;
    case SOPHIE_DECAY: p->decay = clampf(value, 0.03f, 4.0f); break;
    case SOPHIE_CRUSH: p->crush = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_DRIVE: p->drive = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_LEVEL: p->level = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_CUTOFF: p->cutoff = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_RESONANCE: p->resonance = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_FILTER_TYPE: p->filter_type = (int)clampf(value, 0.0f, 4.0f); break;
    case SOPHIE_RING_TIME: p->ring_time = clampf(value, 0.5f, 30.0f); break;
    case SOPHIE_RING_FEEDBACK: p->ring_feedback = clampf(value, 0.0f, 95.0f); break;
    case SOPHIE_RING_MIX: p->ring_mix = clampf(value, 0.0f, 100.0f); break;
    case SOPHIE_RING_TONE: p->ring_tone = clampf(value, 0.0f, 100.0f); break;
    default: return 0;
    }
    return 1;
}

float sophie_get_param(const sophie_t *s, sophie_param_t param) {
    const sophie_patch_t *p = &s->patch;
    switch (param) {
    case SOPHIE_MODEL: return (float)p->model;
    case SOPHIE_TUNE: return p->tune;
    case SOPHIE_COLOR: return p->color;
    case SOPHIE_METAL: return p->metal;
    case SOPHIE_FEEDBACK: return p->feedback;
    case SOPHIE_SWEEP: return p->sweep;
    case SOPHIE_DECAY: return p->decay;
    case SOPHIE_CRUSH: return p->crush;
    case SOPHIE_DRIVE: return p->drive;
    case SOPHIE_CUTOFF: return p->cutoff;
    case SOPHIE_RESONANCE: return p->resonance;
    case SOPHIE_FILTER_TYPE: return (float)p->filter_type;
    case SOPHIE_RING_TIME: return p->ring_time;
    case SOPHIE_RING_FEEDBACK: return p->ring_feedback;
    case SOPHIE_RING_MIX: return p->ring_mix;
    case SOPHIE_RING_TONE: return p->ring_tone;
    case SOPHIE_METAL_DECAY: return p->metal_decay;
    case SOPHIE_FEEDBACK_MODE: return (float)p->feedback_mode;
    case SOPHIE_TAIL_MODE: return (float)p->tail_mode;
    case SOPHIE_LEVEL: return p->level;
    default: return 0.0f;
    }
}

int sophie_active(const sophie_t *s) {
    return s->voice.active;
}

void sophie_trigger(sophie_t *s, int velocity) {
    velocity &= 0x7f;
    if (velocity == 0) return;
    int note = SOPHIE_FIRST_NOTE + s->pad;
    sophie_voice_t *v = &s->voice;
    float tail = v->active ? v->last : 0.0f;
    int stolen = v->active;
    memset(v, 0, sizeof(*v));
    v->steal_tail = tail;
    v->steal_left = stolen ? s->steal_samples : 0;
    v->active = 1;
    v->velocity = clampf((float)velocity / 127.0f, 0.01f, 1.0f);
    v->patch = s->patch;
    v->body = s->body;
    v->color_s = v->patch.color; v->metal_s = v->patch.metal;
    v->feedback_s = v->patch.feedback; v->cutoff_s = v->patch.cutoff;
    v->resonance_s = v->patch.resonance;
    int legacy_kick = v->body && v->patch.model < 4;
    float pitch_time = legacy_kick ? 0.075f : 0.028f;
    v->amp_mul = expf(logf(0.00012f) / (clampf(v->patch.decay, 0.03f, 4) * s->sample_rate));
    v->pitch_mul = expf(-1.0f / (pitch_time * s->sample_rate));
    float metal_ms = v->patch.metal_decay > 0 ? fmaxf(5.0f, v->patch.metal_decay) : 135.0f;
    v->metal_mul = expf(-1.0f / (metal_ms * 0.001f * s->sample_rate));
    v->metal_env = 1.0f;
    v->base_hz = clampf(midi_hz(note) * exp2f(v->patch.tune / 12.0f), 20.0f, 10000.0f);
    v->amp_env = 1.0f;
    v->pitch_env = 1.0f;
    v->attack = 0.0f;
    v->rng = xorshift32(&s->rng) ^ ((uint32_t)note << 16) ^ (uint32_t)velocity;
    /* Slight phase variation retains impact while avoiding machine-gun repeats. */
    v->mod2_phase = noise_bipolar(&v->rng) * 0.12f;
    v->mod3_phase = noise_bipolar(&v->rng) * 0.08f;
}

/* What render_voice reads of the voice's patch, which no render changes (a trigger replaces it between blocks),
 * computed by the original's own expressions once a block; and the filter's coefficient for the last cutoff seen */
typedef struct {
    int legacy_kick, shaped, smooth;
    float level;
    float crush;
    int rate_div;
    float levels;
    float drive_gain, norm;
    float delay_samples, tone, ring_fb, mix;
    float cutoff_s, f;
} sophie_block_t;

static float filter_f(const sophie_t *s, float cutoff_s) {
    float fc = 40.0f * powf(400.0f, cutoff_s * 0.01f);
    return clampf(2.0f * sinf(SOPHIE_PI * fc / (float)s->sample_rate), 0.001f, 0.95f);
}

static void block_init(const sophie_t *s, const sophie_voice_t *v, sophie_block_t *k) {
    const sophie_patch_t *p = &v->patch;
    k->legacy_kick = v->body && p->model < 4;
    k->shaped = p->metal_decay > 0 || p->model >= 4;
    k->smooth = p->feedback_mode == 1;
    k->level = p->level * 0.01f;
    k->crush = p->crush * 0.01f;
    k->rate_div = 1 + (int)(k->crush * k->crush * 31.0f);
    k->levels = exp2f(15.0f - k->crush * 12.0f);
    float drive = clampf(p->drive * 0.01f, 0.0f, 1.0f);
    /* Preserve the original curve through 50%, then open up a much hotter
     * range above it. The old normalization kept 100% surprisingly polite;
     * easing it out in the upper half lets the soft clip become audibly
     * destructive without changing the useful low-drive region. */
    float drive_units = drive <= 0.5f
        ? drive * 100.0f
        : 50.0f + 250.0f * powf((drive - 0.5f) * 2.0f, 1.5f);
    k->drive_gain = 1.0f + drive_units * 0.075f;
    k->norm = soft_clip(k->drive_gain);
    if (drive > 0.5f) {
        float t = (drive - 0.5f) * 2.0f;
        k->norm += (1.0f - k->norm) * 0.72f * t;
    }
    float delay_samples = p->ring_time * 0.001f * (float)s->sample_rate;
    k->delay_samples = clampf(delay_samples, 1.0f, (float)(SOPHIE_MAX_DELAY_SAMPLES - 2));
    k->tone = 0.02f + p->ring_tone * 0.0098f;
    k->ring_fb = p->ring_feedback * 0.01f;
    k->mix = p->ring_mix * 0.01f;
    k->cutoff_s = v->cutoff_s;
    k->f = filter_f(s, v->cutoff_s);
}

static float render_voice(sophie_t *s, sophie_voice_t *v, sophie_block_t *k) {
    if (!v->active) return 0.0f;
    const sophie_patch_t *p = &v->patch;

    int legacy_kick = k->legacy_kick;
    int shaped = k->shaped;
    int smooth = k->smooth;
    if (p->tail_mode == 1) {
        const sophie_patch_t *target = &s->patch;
#define SLEW(field) v->field##_s += (target->field - v->field##_s) * s->control_slew
        SLEW(color); SLEW(metal); SLEW(feedback); SLEW(cutoff); SLEW(resonance);
#undef SLEW
    }
    v->amp_env *= v->amp_mul;
    v->pitch_env *= v->pitch_mul;
    v->metal_env *= v->metal_mul;
    if (v->metal_env < 0.000001f) v->metal_env = 0.0f;
    v->attack += (1.0f - v->attack) * 0.085f;
    if (v->amp_env < 0.00012f) {
        v->active = 0;
        return 0.0f;
    }

    float color = v->color_s * 0.01f;
    float metal = v->metal_s * 0.01f;
    float feedback = v->feedback_s * 0.01f;
    if (smooth) feedback *= feedback;
    float brightness = shaped ? v->metal_env : v->pitch_env;
    float sweep_range = legacy_kick ? 2.5f : 4.5f;
    float sweep_oct = p->sweep * 0.01f * sweep_range * v->pitch_env;
    /* exp2f(±0) is exactly 1: a Sweep at 0, or a pitch envelope run out, needs no call */
    float hz = clampf(v->base_hz * (sweep_oct == 0.0f ? 1.0f : exp2f(sweep_oct)), 18.0f, 18000.0f);

    /* Deliberately non-integer ratios: Color moves through useful clang zones. */
    float r1 = 0.47f + color * color * 7.6f;
    if (p->model == 4) r1 = 1.31f + color * 6.0f;
    if (p->model == 5) r1 = 0.60f + color * 4.2f;
    float r2 = 1.31f + color * 11.17f;
    float r3 = 2.07f + (1.0f - color) * 15.73f;
    float inc = SOPHIE_TAU * hz / (float)s->sample_rate;

    v->carrier_phase = wrap_phase(v->carrier_phase + inc);
    v->mod1_phase = wrap_phase(v->mod1_phase + inc * r1);
    v->mod2_phase = wrap_phase(v->mod2_phase + inc * r2);
    v->mod3_phase = wrap_phase(v->mod3_phase + inc * r3);

    if (smooth || p->model >= 4) {
        v->carrier_phase = wrap_continuous(v->carrier_phase);
        v->mod1_phase = wrap_continuous(v->mod1_phase);
        v->mod2_phase = wrap_continuous(v->mod2_phase);
        v->mod3_phase = wrap_continuous(v->mod3_phase);
    }
    if (smooth) {
        v->sub_phase = wrap_continuous(v->sub_phase + inc * (legacy_kick ? 0.503f : 0.501f));
        v->upper_phase = wrap_continuous(v->upper_phase + inc * (legacy_kick ? 2.01f : 1.997f));
        v->split_phase = wrap_continuous(v->split_phase + inc * 1.003f);
    }
    float fb = v->feedback_z * feedback * (2.0f + metal * 10.0f);
    float m1 = sinf(v->mod1_phase + fb);
    /* Only the kick, Fuse, Stack and Split read m2, only Stack and Split m3: a sine nobody reads is not computed */
    float m2 = legacy_kick || p->model <= 2 ? sinf(v->mod2_phase + m1 * metal * 2.5f) : 0.0f;
    float m3 = !legacy_kick && (p->model == 1 || p->model == 2) ? sinf(v->mod3_phase) : 0.0f;
    float index = metal * metal * (2.0f + 24.0f * (0.25f + brightness * 0.75f));
    if (smooth) {
        /* Heuristic FM bandwidth ceiling, not an alias-free guarantee. */
        float bandwidth = fmaxf(100.0f, s->sample_rate * 0.45f - hz);
        float rate = hz * r1 * (1.0f + metal * 2.5f);
        index = fminf(index, bandwidth / fmaxf(rate, 1.0f));
    }
    float out;

    if (legacy_kick) {
        /* Ponyboy-style kick architecture: a low analog-BD sine owns the
         * pitch and weight. Folded/FM material is a quiet parallel texture,
         * rather than the entire body being forced through metallic FM. */
        float body = sinf(v->carrier_phase);
        float folded = sinf(body * (1.0f + color * 2.2f));
        float texture = sinf(v->carrier_phase + m2 * (0.18f + feedback * 1.35f));
        float n = noise_bipolar(&v->rng);
        v->noise_z += (n - v->noise_z) * 0.32f;
        float click = v->noise_z * v->pitch_env;
        out = body * (0.86f - color * 0.08f)
            + folded * (0.14f + color * 0.08f)
            + texture * metal * 0.18f * (shaped ? brightness : 1.0f)
            + click * (0.012f + metal * 0.035f);
        /* Keep the kick's low tuned body, but let Model remain meaningful on
         * pad 1 instead of silently behaving like Fuse for every choice. */
        if (p->model == 1)
            out += 0.16f * sinf((smooth ? v->upper_phase : v->carrier_phase * 2.01f) + m1 * (0.4f + metal * 2.0f));
        else if (p->model == 2)
            out += 0.14f * sinf((smooth ? v->sub_phase : v->carrier_phase * 0.503f) - m2 * (0.3f + metal * 1.5f));
        else if (p->model == 3) {
            float stair = floorf((v->carrier_phase / SOPHIE_TAU) * 24.0f) * (SOPHIE_TAU / 24.0f);
            out += 0.12f * sinf(stair + m1 * (0.5f + metal * 2.0f));
        }
    } else switch (p->model) {
    default: /* Fuse: two modulators in series, highly responsive feedback. */
        out = sinf(v->carrier_phase + index * (m2 * 0.72f + m1 * 0.28f));
        break;
    case 1: /* Stack: dense additive/FM stack. */
        out = sinf(v->carrier_phase + index * 0.62f * m1)
            + 0.55f * sinf((smooth ? v->sub_phase : v->carrier_phase * 0.501f) + index * 0.35f * m2)
            + 0.32f * sinf((smooth ? v->upper_phase : v->carrier_phase * 1.997f) + index * 0.22f * m3);
        out *= 0.56f;
        break;
    case 2: /* Split: parallel operators with asymmetric modulation. */
        out = 0.62f * sinf(v->carrier_phase + index * m1)
            + 0.38f * sinf((smooth ? v->split_phase : v->carrier_phase * 1.003f) - index * 0.71f * m2)
            + 0.18f * m3;
        break;
    case 4: { /* PIPE: FM carrier mixed with an inharmonic ring. */
        float body = sinf(v->carrier_phase + index * m1);
        float ring_mix = 0.3125f + metal * 0.5f + brightness * 0.125f;
        out = (body * (1.0f - ring_mix) + body * m1 * ring_mix) * 1.25f;
        break;
    }
    case 5: { /* BOOM: clean fundamental with a receding metallic skin. */
        float body = sinf(v->carrier_phase);
        float edge = sinf(v->carrier_phase + index * m1);
        float edge_mix = 0.0625f + metal * brightness * 0.5f;
        out = body * (1.0f - edge_mix) + edge * edge_mix;
        break;
    }
    case 3: { /* Shard: 12-bit wavetable/noise hybrid. */
        float stair = floorf((v->carrier_phase / SOPHIE_TAU) * 32.0f) * (SOPHIE_TAU / 32.0f);
        float n = noise_bipolar(&v->rng);
        v->noise_z += (n - v->noise_z) * (0.02f + color * 0.35f);
        out = sinf(stair + index * 0.55f * m1) * (0.85f - metal * 0.25f)
            + v->noise_z * metal * 0.42f * (shaped ? brightness : 1.0f);
        break;
    }
    }
    float feedback_out = soft_clip(out * 1.4f);
    v->feedback_z = smooth ? v->feedback_z + (feedback_out - v->feedback_z) * s->feedback_slew : feedback_out;
    out *= v->amp_env * v->attack;

    out *= k->level;

    if (k->crush >= 0.005f) {
        if (v->crush_count <= 0) {
            v->crush_hold = out;
            v->crush_count = k->rate_div;
        }
        out = v->crush_hold;
        v->crush_count--;
        out = floorf(out * k->levels + 0.5f) / k->levels;
    }
    /* Drive (block_init): the original curve through 50%, then a much hotter range above it */
    float dry = (soft_clip(out * k->drive_gain) / k->norm) * v->velocity;

    /* Per-voice short feedback delay. At sub-10 ms times this behaves like a
     * resonant comb, adding the metallic rings used in Sophie's references.
     * The bounded buffer and one interpolated read keep it cheap and smooth;
     * zero mix is a true bypass apart from the buffer write. */
    float read = (float)v->delay_pos - k->delay_samples;
    while (read < 0.0f) read += (float)SOPHIE_MAX_DELAY_SAMPLES;
    /* A read a hair below zero rounds to exactly the buffer's length when the length is added: that
     * is the wrap point, the buffer's first sample, not one past its end */
    if (read >= (float)SOPHIE_MAX_DELAY_SAMPLES) read = 0.0f;
    int r0 = (int)read;
    int rnext = (r0 + 1) % SOPHIE_MAX_DELAY_SAMPLES;
    float frac = read - (float)r0;
    float delayed = v->delay_buf[r0] * (1.0f - frac) + v->delay_buf[rnext] * frac;
    v->delay_lp += k->tone * (delayed - v->delay_lp);
    delayed = v->delay_lp;
    v->delay_buf[v->delay_pos] = clampf(dry + delayed * k->ring_fb, -1.0f, 1.0f);
    v->delay_pos = (v->delay_pos + 1) % SOPHIE_MAX_DELAY_SAMPLES;
    out = dry * (1.0f - k->mix) + delayed * k->mix;

    /* The per-pad filter is deliberately last: crusher, drive and ring delay
     * all create fresh high-frequency material, and the filter must be able to
     * tame the complete sound. It sits outside the delay feedback loop so a
     * cutoff change shapes the output without rewriting the loop's decay. */
    /* The cutoff moves only under a Live tail's slew: its coefficient is computed again only then */
    if (v->cutoff_s != k->cutoff_s) {
        k->cutoff_s = v->cutoff_s;
        k->f = filter_f(s, v->cutoff_s);
    }
    float f = k->f;
    float q = clampf(1.0f - v->resonance_s * 0.009f, 0.05f, 1.0f);
    v->filter_low += f * v->filter_band;
    float high = out - v->filter_low - q * v->filter_band;
    v->filter_band += f * high;
    float notch = high + v->filter_low;
    if (p->filter_type == 0) out = v->filter_low;
    else if (p->filter_type == 1) out = high;
    else if (p->filter_type == 2) out = notch;
    else if (p->filter_type == 4) out = v->filter_band;
    else if (p->filter_type == 3) {
        /* DJ-style sweep: low-pass at the left, band-pass in the middle,
         * high-pass at the right, with equal-power-ish crossfades. */
        float pos = clampf(v->cutoff_s * 0.01f, 0.0f, 1.0f);
        if (pos < 0.5f) {
            float t = pos * 2.0f;
            out = v->filter_low * (1.0f - t) + v->filter_band * t;
        } else {
            float t = (pos - 0.5f) * 2.0f;
            out = v->filter_band * (1.0f - t) + high * t;
        }
    }
    if (v->steal_left) {
        float t = (float)v->steal_left-- / s->steal_samples;
        out = out * (1.0f - t) + v->steal_tail * t;
    }
    v->last = out;
    return out;
}

void sophie_render(sophie_t *s, float *out, int frames) {
    if (!s->voice.active) {
        for (int i = 0; i < frames; ++i) out[i] = 0.0f;
        return;
    }
    sophie_block_t k;
    block_init(s, &s->voice, &k);
    for (int i = 0; i < frames; ++i) out[i] = render_voice(s, &s->voice, &k);
}
