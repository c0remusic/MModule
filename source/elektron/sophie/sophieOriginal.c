/*
 * The original Sophie, compiled into sophieTest only, from a copy outside the repository (SOPHIE_ORIGINAL_DIR, the
 * clone of github.com/mestela/schwung-sophie): its source is included as it is, so that the test reaches its voices.
 */

#define __attribute__(x)
#include "sophie.c"

static host_api_v1_t g_testHost;

void *sophieOriginalCreate(void) {
    g_testHost.api_version = MOVE_PLUGIN_API_VERSION;
    g_testHost.sample_rate = 44100;
    g_testHost.frames_per_block = MOVE_FRAMES_PER_BLOCK;
    move_plugin_init_v2(&g_testHost);
    return sophie_create(".", "{}");
}

void sophieOriginalDestroy(void *instance) {
    sophie_destroy(instance);
}

void sophieOriginalSet(void *instance, const char *key, const char *value) {
    sophie_set(instance, key, value);
}

void sophieOriginalNote(void *instance, int note, int velocity) {
    uint8_t msg[3] = {0x90, (uint8_t)note, (uint8_t)velocity};
    sophie_midi(instance, msg, 3, MOVE_MIDI_SOURCE_INTERNAL);
}

/* The voices' sum before render_block's gain, a sample at a time as render_block runs them. */
void sophieOriginalRender(void *instance, float *out, int frames) {
    sophie_t *s = instance;
    for (int i = 0; i < frames; ++i) {
        float mono = 0.0f;
        for (int j = 0; j < SOPHIE_VOICES; ++j) mono += render_voice(s, &s->voices[j]);
        out[i] = mono;
    }
}

int sophieOriginalActiveVoices(void *instance) {
    sophie_t *s = instance;
    int n = 0;
    for (int j = 0; j < SOPHIE_VOICES; ++j) n += s->voices[j].active;
    return n;
}
