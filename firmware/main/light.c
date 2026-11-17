#include "light.h"

#include <math.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "soc/ledc_struct.h"

static const char *TAG = "light";

/* The C6 duty register has 4 fractional bits that the LEDC dithers over 16
 * PWM periods. Writing them buys 4 extra bits of resolution, which is what
 * lets the PWM sit above the audible band (12 integer bits at 19.5 kHz)
 * without coarse steps at low brightness. The IDF API only writes the integer
 * part, so the register is written here and latched with ledc_update_duty(),
 * which does not touch the value. */
#define DUTY_FRAC_BITS 4

#define RENDER_PERIOD_US 5000          /* 200 Hz */
#define SRC_CLK_HZ       80000000UL    /* LEDC PLL_F80M clock on the C6 */
#define FEEDBACK_LEVEL   200.0f        /* own blinks (paired, reset) */
#define MS               1000LL

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Requested state */
static bool s_on;
static uint8_t s_level = LIGHT_LEVEL_MAX;

/* Fade in progress, in "level" units (0 = off, 1..254) */
static float s_from, s_to;
static int64_t s_t0;
static int64_t s_dur;

/* Effects override the output while they run; the state keeps tracking
 * underneath and the output returns to it when they end. */
typedef enum {
    FX_NONE,
    FX_FEEDBACK,        /* n short blinks */
    FX_IDENTIFY,        /* while IdentifyTime runs */
    FX_BLINK,           /* ZCL 0x00: on/off once */
    FX_BREATHE,         /* ZCL 0x01: up and down in 1 s, 15 times */
    FX_OKAY,            /* ZCL 0x02: non-colour light, two flashes */
    FX_CHANNEL_CHANGE,  /* ZCL 0x0b: non-colour light, max 0.5 s then min 7.5 s */
} fx_t;

static fx_t s_fx;
static int64_t s_fx_t0;
static int64_t s_fx_end;    /* 0 = open-ended (identify) */

/* Curve */
static uint32_t s_duty_full;
static uint32_t s_last_duty = UINT32_MAX;
static float s_gamma;
static float s_min_frac;
static light_change_cb_t s_change_cb;

static float current_level_locked(int64_t now)
{
    if (s_dur <= 0 || now >= s_t0 + s_dur) {
        return s_to;
    }
    float k = (float)(now - s_t0) / (float)s_dur;
    return s_from + (s_to - s_from) * k;
}

/* Level of the running effect, or < 0 if none (or it ended). s_mux held. */
static float effect_level_locked(int64_t now)
{
    if (s_fx == FX_NONE) {
        return -1.0f;
    }
    if (s_fx_end && now >= s_fx_end) {
        s_fx = FX_NONE;
        return -1.0f;
    }
    int64_t el = (now - s_fx_t0) / MS;      /* ms */
    switch (s_fx) {
    case FX_FEEDBACK:
        return (el % 300) < 150 ? FEEDBACK_LEVEL : 0.0f;
    case FX_IDENTIFY:
        return (el % 1000) < 500 ? FEEDBACK_LEVEL : 0.0f;
    case FX_BLINK:
        return el < 500 ? (float)LIGHT_LEVEL_MAX : 0.0f;
    case FX_BREATHE: {
        int64_t ph = el % 1000;
        float tri = ph < 500 ? ph / 500.0f : (1000 - ph) / 500.0f;
        return tri * LIGHT_LEVEL_MAX;
    }
    case FX_OKAY:
        return (el % 400) < 200 ? (float)LIGHT_LEVEL_MAX : 0.0f;
    case FX_CHANNEL_CHANGE:
        return el < 500 ? (float)LIGHT_LEVEL_MAX : (float)LIGHT_LEVEL_MIN;
    default:
        return -1.0f;
    }
}

static void start_fx_locked(fx_t fx, int64_t dur_ms)
{
    int64_t now = esp_timer_get_time();
    s_fx = fx;
    s_fx_t0 = now;
    s_fx_end = dur_ms > 0 ? now + dur_ms * MS : 0;
}

/* Returns the duty in sixteenths of a count (integer part << DUTY_FRAC_BITS).
 * Called outside the critical section: the C6 has no FPU and powf is slow. */
static uint32_t level_to_duty(float l, float min_frac, float gamma)
{
    const uint32_t full = s_duty_full << DUTY_FRAC_BITS;
    if (l <= 0.0f) {
        return 0;
    }
    float frac;
    if (l < 1.0f) {
        frac = min_frac * l;                /* ramp from 0 to the minimum */
    } else {
        float x = (l - 1.0f) / (float)(LIGHT_LEVEL_MAX - 1);
        if (x > 1.0f) {
            x = 1.0f;
        }
        frac = min_frac + (1.0f - min_frac) * powf(x, gamma);
    }
    uint32_t d = (uint32_t)lroundf(frac * (float)full);
    if (d == 0 && l > 0.0f && min_frac > 0.0f) {
        d = 1;
    }
    return d > full ? full : d;
}

static void write_duty(uint32_t duty_x16)
{
    LEDC.channel_group[LEDC_LOW_SPEED_MODE].channel[LEDC_CHANNEL_0].duty.duty = duty_x16;
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void render_cb(void *arg)
{
    int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_mux);
    float l = current_level_locked(now);
    float fx = effect_level_locked(now);
    if (fx >= 0.0f) {
        l = fx;
    }
    float min_frac = s_min_frac;
    float gamma = s_gamma;
    portEXIT_CRITICAL(&s_mux);

    uint32_t duty = level_to_duty(l, min_frac, gamma);
    if (duty != s_last_duty) {
        s_last_duty = duty;
        write_duty(duty);
    }
}

void light_init(uint16_t min_x10000, uint8_t gamma_x10)
{
    light_set_curve(min_x10000, gamma_x10);

    /* Highest integer resolution the frequency allows (capped at 14); the
     * 4 dithered fractional bits come on top. */
    uint32_t bits = 0;
    while ((SRC_CLK_HZ / CONFIG_DIMMER_PWM_FREQ_HZ) >> (bits + 1)) {
        bits++;
    }
    if (bits > 14) {
        bits = 14;
    }

    esp_err_t err = ESP_FAIL;
    for (; bits >= 8; bits--) {
        ledc_timer_config_t t = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = bits,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = CONFIG_DIMMER_PWM_FREQ_HZ,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        err = ledc_timer_config(&t);
        if (err == ESP_OK) {
            break;
        }
    }
    ESP_ERROR_CHECK(err);
    s_duty_full = 1UL << bits;

    ledc_channel_config_t c = {
        .gpio_num = CONFIG_DIMMER_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
#if CONFIG_DIMMER_PWM_INVERTED
        .flags.output_invert = 1,
#endif
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
    s_last_duty = 0;

    const esp_timer_create_args_t ta = {
        .callback = render_cb,
        .name = "light_render",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&ta, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, RENDER_PERIOD_US));

    ESP_LOGI(TAG, "PWM GPIO%d, %d Hz, %lu+%d bits",
             CONFIG_DIMMER_PWM_GPIO, CONFIG_DIMMER_PWM_FREQ_HZ, (unsigned long)bits, DUTY_FRAC_BITS);
}

void light_set_curve(uint16_t min_x10000, uint8_t gamma_x10)
{
    if (min_x10000 > LIGHT_MIN_OUTPUT_MAX) {
        min_x10000 = LIGHT_MIN_OUTPUT_MAX;
    }
    if (gamma_x10 < 10) {
        gamma_x10 = 10;
    } else if (gamma_x10 > 40) {
        gamma_x10 = 40;
    }
    portENTER_CRITICAL(&s_mux);
    s_min_frac = min_x10000 / 10000.0f;
    s_gamma = gamma_x10 / 10.0f;
    portEXIT_CRITICAL(&s_mux);
    ESP_LOGI(TAG, "curve: minimum %.2f %%, gamma %.1f", min_x10000 / 100.0f, gamma_x10 / 10.0f);
}

void light_set(bool on, uint8_t level, uint32_t fade_ms)
{
    if (level < LIGHT_LEVEL_MIN) {
        level = LIGHT_LEVEL_MIN;
    } else if (level > LIGHT_LEVEL_MAX) {
        level = LIGHT_LEVEL_MAX;
    }

    bool changed = false;
    int64_t now = esp_timer_get_time();

    portENTER_CRITICAL(&s_mux);
    if (on != s_on || level != s_level) {
        changed = true;
        s_on = on;
        s_level = level;
        float to = on ? (float)level : 0.0f;
        if (to != s_to) {
            s_from = current_level_locked(now);
            s_to = to;
            s_t0 = now;
            s_dur = (int64_t)fade_ms * 1000;
        }
    }
    portEXIT_CRITICAL(&s_mux);

    if (changed && s_change_cb) {
        s_change_cb(on, level);
    }
}

void light_get(bool *on, uint8_t *level)
{
    portENTER_CRITICAL(&s_mux);
    if (on) {
        *on = s_on;
    }
    if (level) {
        *level = s_level;
    }
    portEXIT_CRITICAL(&s_mux);
}

void light_blink(int times)
{
    portENTER_CRITICAL(&s_mux);
    start_fx_locked(FX_FEEDBACK, (int64_t)times * 300);
    portEXIT_CRITICAL(&s_mux);
}

void light_identify(bool active)
{
    portENTER_CRITICAL(&s_mux);
    if (active) {
        if (s_fx != FX_IDENTIFY) {
            start_fx_locked(FX_IDENTIFY, 0);
        }
    } else if (s_fx == FX_IDENTIFY) {
        s_fx = FX_NONE;
    }
    portEXIT_CRITICAL(&s_mux);
}

void light_trigger_effect(uint8_t effect_id)
{
    portENTER_CRITICAL(&s_mux);
    switch (effect_id) {
    case LIGHT_EFFECT_BLINK:          start_fx_locked(FX_BLINK, 1000); break;
    case LIGHT_EFFECT_BREATHE:        start_fx_locked(FX_BREATHE, 15000); break;
    case LIGHT_EFFECT_OKAY:           start_fx_locked(FX_OKAY, 800); break;
    case LIGHT_EFFECT_CHANNEL_CHANGE: start_fx_locked(FX_CHANNEL_CHANGE, 8000); break;
    case LIGHT_EFFECT_FINISH:
        /* Finish the current sequence: breathe at the end of the current
         * cycle, identify right away; the others are short and run out. */
        if (s_fx == FX_BREATHE) {
            int64_t now = esp_timer_get_time();
            int64_t cycles = (now - s_fx_t0) / (1000 * MS) + 1;
            s_fx_end = s_fx_t0 + cycles * 1000 * MS;
        } else if (s_fx == FX_IDENTIFY) {
            s_fx = FX_NONE;
        }
        break;
    case LIGHT_EFFECT_STOP:
    default:
        s_fx = FX_NONE;
        break;
    }
    portEXIT_CRITICAL(&s_mux);
}

void light_set_change_cb(light_change_cb_t cb)
{
    s_change_cb = cb;
}
