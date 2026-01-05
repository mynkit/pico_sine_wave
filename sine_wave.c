#include <math.h>
#include <stdlib.h>
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"

// =====================================================
// Audio basic settings
// =====================================================
#define SAMPLE_RATE 48000
#define TABLE_SIZE  256
#define SAMPLES_PER_BUFFER 256
#define PICO_AUDIO_PACK_MUTE_PIN 21

// silence between notes
#define NOTE_OFF_MIN_SEC 0.01f
#define NOTE_OFF_MAX_SEC 0.2f

// =====================================================
// Utility
// =====================================================
static inline float frand(void) {
    return (float)rand() / (float)RAND_MAX;
}

static inline float mapf(
    float v, float in_min, float in_max,
    float out_min, float out_max
) {
    return (v - in_min) / (in_max - in_min)
        * (out_max - out_min) + out_min;
}

// =====================================================
// Note structure (SC Synth 相当)
// =====================================================
typedef struct {
    float amp;
    float freq;
    float sustain;
    float accelerate;

    uint32_t attack_samples;
    uint32_t release_samples;
    uint32_t note_total_samples;

    uint32_t freq_ramp_samples;

    uint32_t phase_step_start;
    uint32_t phase_step_end;
} SineNote;

// =====================================================
static int16_t sine_table[TABLE_SIZE];

static SineNote current_note;
static uint32_t note_sample_pos = 0;
static uint32_t gate_counter = 0;
static bool note_on = false;
static uint32_t phase = 0;

// =====================================================
// Random parameter generator (TS/C++ 移植)
// =====================================================
static SineNote random_sine_note(float bubble1, float bubble2) {

    float bubbleSizeMin = mapf(bubble1, 0.0f, 100.0f, 10.0f, 100.0f);
    float bubbleSizeMax = mapf(bubble2, 0.0f, 100.0f, 10.0f, 100.0f);

    float r = frand();

    SineNote n;

    // sustain
    n.sustain = mapf(
        r, 0.0f, 1.0f,
        1.0f / bubbleSizeMax,
        fminf(1.0f / bubbleSizeMin, 0.08f)
    ) * 1.1f;

    // freq
    n.freq = mapf(
        sqrtf(r), 0.0f, 1.0f,
        bubbleSizeMax * bubbleSizeMax,
        bubbleSizeMin * bubbleSizeMin
    );

    // accelerate
    n.accelerate = mapf(
        r, 0.0f, 1.0f,
        sqrtf(304.0f / bubbleSizeMax),
        sqrtf(304.0f / bubbleSizeMin)
    );

    // amp
    n.amp = 1.0f;
    n.amp *= mapf(r * r, 0.0f, 1.0f, 0.1f, 1.0f);
    n.amp *= mapf(frand() * frand(), 0.0f, 1.0f, 0.0f, 1.0f);

    // envelope
    n.attack_samples  = (uint32_t)(0.01f * n.sustain * SAMPLE_RATE);
    n.release_samples = (uint32_t)(0.6f  * n.sustain * SAMPLE_RATE);
    n.note_total_samples = n.attack_samples + n.release_samples;

    // frequency ramp
    float ramp_time = (n.sustain > 0.5f) ? n.sustain : 0.5f;
    n.freq_ramp_samples = (uint32_t)(ramp_time * SAMPLE_RATE);

    n.phase_step_start =
        (uint32_t)((float)TABLE_SIZE * n.freq
                   / SAMPLE_RATE * (1 << 16));

    n.phase_step_end =
        (uint32_t)((float)TABLE_SIZE *
                   (n.freq * (1.0f + n.accelerate))
                   / SAMPLE_RATE * (1 << 16));

    return n;
}

// =====================================================
// Start new note (SC: Synth.new)
// =====================================================
static void start_new_note(void) {
    current_note = random_sine_note(10.0f, 73.0f); // ← 任意
    note_sample_pos = 0;
    note_on = true;
}

// =====================================================
// Audio init
// =====================================================
static struct audio_buffer_pool *init_audio(void) {
    static audio_format_t audio_format = {
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .sample_freq = SAMPLE_RATE,
        .channel_count = 1,
    };

    static struct audio_buffer_format producer_format = {
        .format = &audio_format,
        .sample_stride = 2
    };

    struct audio_buffer_pool *pool =
        audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);

    struct audio_i2s_config config = {
        .data_pin = 9,
        .clock_pin_base = 10,
        .dma_channel = 0,
        .pio_sm = 0,
    };

    if (!audio_i2s_setup(&audio_format, &config)) {
        panic("I2S setup failed");
    }

    audio_i2s_connect(pool);
    audio_i2s_set_enabled(true);

    return pool;
}

// =====================================================
// Main
// =====================================================
int main() {
    stdio_init_all();
    srand(time_us_32());

    // DAC unmute
    gpio_init(PICO_AUDIO_PACK_MUTE_PIN);
    gpio_set_dir(PICO_AUDIO_PACK_MUTE_PIN, GPIO_OUT);
    gpio_put(PICO_AUDIO_PACK_MUTE_PIN, 0);

    // sine table
    for (int i = 0; i < TABLE_SIZE; i++) {
        sine_table[i] = (int16_t)(
            32767.0f * sinf(2.0f * M_PI * i / TABLE_SIZE) * 0.05f
        );
    }

    struct audio_buffer_pool *ap = init_audio();
    start_new_note();

    while (true) {
        struct audio_buffer *buffer = take_audio_buffer(ap, true);
        int16_t *samples = (int16_t *)buffer->buffer->bytes;

        for (uint i = 0; i < buffer->max_sample_count; i++) {

            if (note_on) {

                // ===== Env.linen =====
                float env;
                if (note_sample_pos < current_note.attack_samples) {
                    float x = (float)note_sample_pos
                              / current_note.attack_samples;
                    env = x * x;
                }
                else if (note_sample_pos <
                         current_note.note_total_samples) {
                    float x = 1.0f -
                        (float)(note_sample_pos -
                        current_note.attack_samples)
                        / current_note.release_samples;
                    env = x * x;
                }
                else {
                    note_on = false;
                    note_sample_pos = 0;
                    samples[i] = 0;
                    continue;
                }

                // ===== accelerate (Line.kr) =====
                uint32_t ramp_pos =
                    (note_sample_pos < current_note.freq_ramp_samples)
                        ? note_sample_pos
                        : current_note.freq_ramp_samples;

                float t = (float)ramp_pos
                          / current_note.freq_ramp_samples;

                uint32_t phase_step =
                    current_note.phase_step_start +
                    (uint32_t)(
                        (current_note.phase_step_end -
                         current_note.phase_step_start) * t
                    );

                int16_t s =
                    sine_table[(phase >> 16) % TABLE_SIZE];

                samples[i] = (int16_t)(s * env * current_note.amp);
                phase += phase_step;
                note_sample_pos++;
            }
            else {
                samples[i] = 0;
                gate_counter++;

                uint32_t off_samples = mapf(
                    frand(),
                    0.0f, 1.0f,
                    NOTE_OFF_MIN_SEC,
                    NOTE_OFF_MAX_SEC
                ) * SAMPLE_RATE;

                if (gate_counter >= off_samples) {
                    gate_counter = 0;
                    start_new_note();
                }
            }
        }

        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(ap, buffer);
    }
}
