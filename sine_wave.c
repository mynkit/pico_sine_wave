#include <math.h>
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"

#define SAMPLE_RATE 48000
#define SINE_FREQ   440
#define TABLE_SIZE  256
#define SAMPLES_PER_BUFFER 256
#define PICO_AUDIO_PACK_MUTE_PIN 21

// 500ms on / 500ms off
#define NOTE_ON_SAMPLES  (SAMPLE_RATE / 2)   // 24000
#define NOTE_OFF_SAMPLES (SAMPLE_RATE / 2)   // 24000

#define SUSTAIN_SEC 0.5f

#define ATTACK_TIME   (0.01f * SUSTAIN_SEC)
#define RELEASE_TIME  (0.6f  * SUSTAIN_SEC)

#define ATTACK_SAMPLES  ((uint32_t)(ATTACK_TIME  * SAMPLE_RATE))
#define RELEASE_SAMPLES ((uint32_t)(RELEASE_TIME * SAMPLE_RATE))

#define NOTE_TOTAL_SAMPLES (ATTACK_SAMPLES + RELEASE_SAMPLES)

uint32_t note_sample_pos = 0;

static int16_t sine_table[TABLE_SIZE];

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

    const struct audio_format *output_format =
        audio_i2s_setup(&audio_format, &config);

    if (!output_format) {
        panic("I2S setup failed");
    }

    audio_i2s_connect(pool);
    audio_i2s_set_enabled(true);

    return pool;
}

int main() {
    stdio_init_all();

    // ---- DAC mute解除 ----
    gpio_init(PICO_AUDIO_PACK_MUTE_PIN);
    gpio_set_dir(PICO_AUDIO_PACK_MUTE_PIN, GPIO_OUT);
    gpio_put(PICO_AUDIO_PACK_MUTE_PIN, 0);

    // サイン波テーブル生成
    for (int i = 0; i < TABLE_SIZE; i++) {
        sine_table[i] = (int16_t)(
            32767 * sinf(2.0f * M_PI * i / TABLE_SIZE) * 0.05
        );
    }

    struct audio_buffer_pool *ap = init_audio();

    uint32_t phase = 0;
    uint32_t phase_step =
        (uint32_t)((float)TABLE_SIZE * SINE_FREQ / SAMPLE_RATE * (1 << 16));

    uint32_t gate_counter = 0;
    bool note_on = true;

    while (true) {
        struct audio_buffer *buffer = take_audio_buffer(ap, true);
        int16_t *samples = (int16_t *)buffer->buffer->bytes;

        for (uint i = 0; i < buffer->max_sample_count; i++) {

            float env = 0.0f;

            if (note_on) {

                if (note_sample_pos < ATTACK_SAMPLES) {
                    // Attack
                    float x = (float)note_sample_pos / ATTACK_SAMPLES;
                    env = x * x; // curve ≈ -3
                }
                else if (note_sample_pos < NOTE_TOTAL_SAMPLES) {
                    // Release
                    float x = 1.0f -
                        (float)(note_sample_pos - ATTACK_SAMPLES) / RELEASE_SAMPLES;
                    env = x * x;
                }
                else {
                    // ノート終了
                    env = 0.0f;
                    note_on = false;
                    note_sample_pos = 0;
                    gate_counter = 0;
                }

                int16_t s =
                    sine_table[(phase >> 16) % TABLE_SIZE];
                samples[i] = (int16_t)(s * env);

                phase += phase_step;
                note_sample_pos++;
            }
            else {
                samples[i] = 0;
                gate_counter++;

                if (gate_counter >= NOTE_OFF_SAMPLES) {
                    gate_counter = 0;
                    note_on = true;
                    note_sample_pos = 0;
                }
            }
        }


        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(ap, buffer);
    }
}
