#include <math.h>
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/audio_i2s.h"

#define SAMPLE_RATE 48000
#define SINE_FREQ   440
#define TABLE_SIZE  256
#define SAMPLES_PER_BUFFER 256
#define PICO_AUDIO_PACK_MUTE_PIN 21

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
        // .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        // .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        // https://toys.poppo-ya.com/1108/
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

    // ---- Pico Audio Pack: DAC mute解除 ----
    gpio_init(PICO_AUDIO_PACK_MUTE_PIN);
    gpio_set_dir(PICO_AUDIO_PACK_MUTE_PIN, GPIO_OUT);
    gpio_put(PICO_AUDIO_PACK_MUTE_PIN, 0); // LOW = unmute

    // サイン波テーブル生成
    for (int i = 0; i < TABLE_SIZE; i++) {
        sine_table[i] = (int16_t)(
            32767 * sinf(2.0f * M_PI * i / TABLE_SIZE)
        );
    }

    struct audio_buffer_pool *ap = init_audio();

    uint32_t phase = 0;
    uint32_t phase_step =
        (uint32_t)((float)TABLE_SIZE * SINE_FREQ / SAMPLE_RATE * (1 << 16));

    while (true) {
        struct audio_buffer *buffer = take_audio_buffer(ap, true);
        int16_t *samples = (int16_t *)buffer->buffer->bytes;

        for (uint i = 0; i < buffer->max_sample_count; i++) {
            samples[i] = sine_table[(phase >> 16) % TABLE_SIZE];
            phase += phase_step;
        }

        buffer->sample_count = buffer->max_sample_count;
        give_audio_buffer(ap, buffer);
    }
}
