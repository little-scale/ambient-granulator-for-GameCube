#include <fat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogcsys.h>
#include <sdcard/gcsd.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_output.h"
#include "edit_repeat.h"
#include "sample_bank.h"
#include "sample_bank_bin.h"
#include "transient_analysis.h"
#include "ui.h"
#include "waveform.h"

#define SCREEN_WIDTH 320
#define WAVEFORM_COLUMNS SCREEN_WIDTH
#define MARKER_COUNT 16
#define MARKER_LIFETIME 24
#define TOP_ROW_OFFSET 1
#define EXTERNAL_BANK_PATH \
    "sd2:/gamecube-ambient-granulator/sample_bank.bin"

typedef struct {
    const char *name;
    int value;
    int minimum;
    int maximum;
    int step;
    int coarse_step;
    const char *unit;
    int column;
    int row;
    int value_column;
} Parameter;

enum {
    PARAM_RANGE,
    PARAM_PITCH,
    PARAM_FINE,
    PARAM_PITCH_DEVIATION,
    PARAM_FINE_DEVIATION,
    PARAM_CLOCK,
    PARAM_BPM,
    PARAM_DIVISION,
    PARAM_INTERVAL,
    PARAM_JITTER,
    PARAM_GRAINS,
    PARAM_POLYPHONY,
    PARAM_LENGTH,
    PARAM_ATTACK,
    PARAM_RELEASE,
    PARAM_GAIN,
    PARAM_VOLUME,
    PARAM_PAN,
    PARAM_PAN_DIVERGENCE,
    PARAM_REVERB,
    PARAM_REVERB_FEEDBACK,
    PARAM_REVERB_SIZE,
    PARAM_REVERB_DAMPING,
    PARAM_REVERB_FREEZE,
    PARAM_PHASER_DEPTH,
    PARAM_PHASER_SPEED,
    PARAM_HIGHPASS,
    PARAM_LOWPASS,
    PARAM_COUNT,
};

static Parameter parameters[PARAM_COUNT] = {
    { "RANGE",    24, -128, 128,  1,  16, "PX", 0,  2,  7 },
    { "PITCH",     0,  -24,  24,  1,  12, "ST", 0,  3,  7 },
    { "FINE",      0, -100, 100,  1,  10, "CT", 0,  4,  7 },
    { "P DEV",     0,    0,  12,  1,   4, "ST", 0,  5,  7 },
    { "F DEV",     0,    0, 100,  1,  10, "CT", 0,  6,  7 },
    { "MODE",      1,    0,   1,  1,   1,   "", 0,  9,  7 },
    { "BPM",      96,   40, 240,  1,  10,   "", 0, 10,  7 },
    { "DIV",       1,    0,   3,  1,   1,   "", 0, 11,  7 },
    { "INTERVAL",120,   20,1000, 10, 100, "MS", 0, 12,  7 },
    { "JITTER",   20,    0, 100,  5,  20,  "%", 0, 13,  7 },
    { "GRAINS",    8,    1,  32,  1,   4,   "", 0, 14,  7 },
    { "POLY",      16,   1,  16,  1,   4,   "", 0, 18,  7 },
    { "LENGTH",  120,   20, 500, 10, 100, "MS",20,  2, 28 },
    { "ATTACK",   15,    0,  50,  5,  20,  "%",20,  3, 28 },
    { "RELEASE",  25,    0,  50,  5,  20,  "%",20,  4, 28 },
    { "GAIN",      0,  -24,  18,  1,   6, "DB",20,  5, 28 },
    { "VOL",     100,    0, 100,  1,  10,  "%",20,  6, 28 },
    { "PAN",       0, -100, 100,  1,  10,  "%",20,  7, 28 },
    { "P DEV",   100,    0, 100,  1,  10,  "%",20,  8, 28 },
    { "REV",     100,    0, 100,  1,  10,  "%",20, 13, 28 },
    { "FEEDBACK",900,    0, 999,  1,  10,  "%",20, 14, 28 },
    { "SIZE",    100,    0, 100,  1,  10,  "%",20, 15, 28 },
    { "DAMP",      5,    0, 100,  1,  10,  "%",20, 16, 28 },
    { "FREEZE",    0,    0,   1,  1,   1,   "",20, 17, 28 },
    { "PHASE",   100,    0, 100,  1,  10,  "%",20, 11, 28 },
    { "P SPD",    10,    1, 100,  1,  10,   "",20, 12, 28 },
    { "HPF",       0,    0,4000, 10, 500, "HZ",20, 20, 28 },
    { "LPF",    8000,  200,8000, 10, 500, "HZ",20, 21, 28 },
};

typedef struct {
    int selected_parameter;
    int playhead_x;
    uint32_t playhead_sample;
    int sample_index;
    int live_pitch;
    int live_pan;
    int marker_x[MARKER_COUNT];
    int marker_ttl[MARKER_COUNT];
    int next_marker;
    bool b_pressed;
    bool b_used;
    EditRepeatState edit_repeat;
    EditRepeatState navigation_repeat;
    TransientMap transients;
    int view;
    int audition_ttl;
    char message[64];
} AppState;

static int16_t waveform_minimum[WAVEFORM_COLUMNS];
static int16_t waveform_maximum[WAVEFORM_COLUMNS];
static const int divisions[] = { 8, 16, 32, 64 };

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static int random_startup_sample_index(uint32_t sample_count)
{
    if (sample_count == 0)
        return 0;

    u64 ticks = gettime();
    uint32_t random = (uint32_t)ticks ^ (uint32_t)(ticks >> 32);
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return (int)(random % sample_count);
}

static uint32_t sample_position_from_x(const LoadedSample *sample, int x)
{
    if (sample->sample_count < 2)
        return 0;
    x = clamp_int(x, 0, SCREEN_WIDTH - 1);
    return (uint32_t)((uint64_t)x * (sample->sample_count - 1)
                    / (SCREEN_WIDTH - 1));
}

static int x_from_sample_position(const LoadedSample *sample,
                                  uint32_t position)
{
    if (sample->sample_count < 2)
        return 0;
    if (position >= sample->sample_count)
        position = sample->sample_count - 1;
    return (int)((uint64_t)position * (SCREEN_WIDTH - 1)
               / (sample->sample_count - 1));
}

static void set_playhead_x(AppState *state, const LoadedSample *sample, int x)
{
    state->playhead_x = clamp_int(x, 0, SCREEN_WIDTH - 1);
    state->playhead_sample = sample_position_from_x(
        sample, state->playhead_x);
}

static void set_playhead_sample(AppState *state, const LoadedSample *sample,
                                uint32_t position)
{
    if (sample->sample_count == 0) {
        state->playhead_x = 0;
        state->playhead_sample = 0;
        return;
    }
    if (position >= sample->sample_count)
        position = sample->sample_count - 1;
    state->playhead_sample = position;
    state->playhead_x = x_from_sample_position(sample, position);
}

static void trigger_burst(AudioOutput *audio, const AppState *state)
{
    audio_output_trigger_sample(audio, state->playhead_sample,
                                state->playhead_x,
                                parameters[PARAM_GRAINS].value);
}

static void reset_parameters(void)
{
    const int defaults[PARAM_COUNT] = {
        24, 0, 0, 0, 0, 1, 96, 1, 120, 20, 8, 16, 120, 15,
        25, 0, 100, 0, 100, 100, 900, 100, 5, 0, 100, 10, 0, 8000,
    };
    for (int index = 0; index < PARAM_COUNT; index++)
        parameters[index].value = defaults[index];
}

static void nudge_parameter(int index, int direction, bool coarse)
{
    Parameter *parameter = &parameters[index];
    int amount = coarse ? parameter->coarse_step : parameter->step;
    parameter->value = clamp_int(parameter->value + direction * amount,
                                 parameter->minimum, parameter->maximum);
}

static void add_marker(AppState *state, int x)
{
    state->marker_x[state->next_marker] = x;
    state->marker_ttl[state->next_marker] = MARKER_LIFETIME;
    state->next_marker = (state->next_marker + 1) % MARKER_COUNT;
}

static void animate_markers(AppState *state)
{
    for (int index = 0; index < MARKER_COUNT; index++) {
        if (state->marker_ttl[index] > 0)
            state->marker_ttl[index]--;
    }
}

static AudioRenderConfig render_config(const AppState *state, u16 held)
{
    AudioRenderConfig config = {
        .granular = {
            .center_x = state->playhead_x,
            .center_sample = state->playhead_sample,
            .center_sample_valid = true,
            .range = parameters[PARAM_RANGE].value,
            .pitch_semitones = clamp_int(
                parameters[PARAM_PITCH].value + state->live_pitch,
                -24, 24),
            .fine_cents = parameters[PARAM_FINE].value,
            .pitch_deviation = parameters[PARAM_PITCH_DEVIATION].value,
            .fine_deviation_cents
                = parameters[PARAM_FINE_DEVIATION].value,
            .clock_sync = parameters[PARAM_CLOCK].value != 0,
            .bpm = parameters[PARAM_BPM].value,
            .division = parameters[PARAM_DIVISION].value,
            .interval_ms = parameters[PARAM_INTERVAL].value,
            .jitter_percent = parameters[PARAM_JITTER].value,
            .grain_count = parameters[PARAM_GRAINS].value,
            .voice_limit = parameters[PARAM_POLYPHONY].value,
            .length_ms = parameters[PARAM_LENGTH].value,
            .attack_percent = parameters[PARAM_ATTACK].value,
            .release_percent = parameters[PARAM_RELEASE].value,
            .gain_db = parameters[PARAM_GAIN].value,
            .volume_percent = parameters[PARAM_VOLUME].value,
            .pan_percent = clamp_int(parameters[PARAM_PAN].value
                                   + state->live_pan, -100, 100),
            .pan_divergence = parameters[PARAM_PAN_DIVERGENCE].value,
            .gate = (held & PAD_BUTTON_A) != 0,
        },
        .effects = {
            .wet_percent = parameters[PARAM_REVERB].value,
            .feedback_tenths_percent
                = parameters[PARAM_REVERB_FEEDBACK].value,
            .size_percent = parameters[PARAM_REVERB_SIZE].value,
            .damping_percent = parameters[PARAM_REVERB_DAMPING].value,
            .freeze = parameters[PARAM_REVERB_FREEZE].value != 0,
            .phaser_depth_percent = parameters[PARAM_PHASER_DEPTH].value,
            .phaser_speed_hundredths_hz
                = parameters[PARAM_PHASER_SPEED].value,
            .highpass_hz = parameters[PARAM_HIGHPASS].value,
            .lowpass_hz = parameters[PARAM_LOWPASS].value,
        },
    };
    return config;
}

static bool load_sample(SampleBank *bank, LoadedSample *loaded,
                        AudioOutput *audio, AppState *state, int index)
{
    if (bank->sample_count == 0)
        return false;
    int wrapped = index % (int)bank->sample_count;
    if (wrapped < 0)
        wrapped += (int)bank->sample_count;
    LoadedSample replacement;
    if (!sample_bank_load(bank, (uint32_t)wrapped, &replacement)) {
        snprintf(state->message, sizeof(state->message),
                 "Sample %d failed CRC/read", wrapped + 1);
        return false;
    }
    if (audio != NULL)
        audio_output_set_sample(audio, replacement.samples,
                                replacement.sample_count,
                                replacement.sample_rate);
    loaded_sample_free(loaded);
    *loaded = replacement;
    state->sample_index = wrapped;
    set_playhead_x(state, loaded, SCREEN_WIDTH / 2);
    memset(state->marker_ttl, 0, sizeof(state->marker_ttl));
    waveform_analyze(loaded->samples, loaded->sample_count,
                     waveform_minimum, waveform_maximum,
                     WAVEFORM_COLUMNS);
    transient_analyze(loaded->samples, loaded->sample_count,
                      loaded->sample_rate, &state->transients);
    snprintf(state->message, sizeof(state->message), "Loaded %.24s: %lu hits",
             loaded->name, (unsigned long)state->transients.count);
    return true;
}

static void format_parameter(int index, char *text, size_t text_size)
{
    Parameter *parameter = &parameters[index];
    int value = parameter->value;
    if (index == PARAM_RANGE) {
        snprintf(text, text_size, value > 0 ? "+-%d" : "%d", value);
    } else if (index == PARAM_PITCH || index == PARAM_FINE) {
        snprintf(text, text_size, "%+d", value);
    } else if (index == PARAM_CLOCK) {
        snprintf(text, text_size, "%s", parameter->value ? "SYNC" : "FREE");
    } else if (index == PARAM_DIVISION) {
        snprintf(text, text_size, "1/%d", divisions[parameter->value]);
    } else if (index == PARAM_GAIN) {
        snprintf(text, text_size, "%+dDB", value);
    } else if (index == PARAM_PAN) {
        snprintf(text, text_size, "%+d%%", value);
    } else if (index == PARAM_JITTER
            || index == PARAM_ATTACK
            || index == PARAM_RELEASE
            || index == PARAM_VOLUME
            || index == PARAM_PAN_DIVERGENCE
            || index == PARAM_REVERB
            || index == PARAM_REVERB_SIZE
            || index == PARAM_REVERB_DAMPING
            || index == PARAM_PHASER_DEPTH) {
        snprintf(text, text_size, "%d%%", value);
    } else if (index == PARAM_REVERB_FREEZE) {
        snprintf(text, text_size, "%s", parameter->value ? "ON" : "OFF");
    } else if (index == PARAM_REVERB_FEEDBACK) {
        snprintf(text, text_size, "%d.%d%%", parameter->value / 10,
                 parameter->value % 10);
    } else if (index == PARAM_PHASER_SPEED) {
        snprintf(text, text_size, "%d.%02dHZ", parameter->value / 100,
                 parameter->value % 100);
    } else if (index == PARAM_HIGHPASS && value == 0) {
        snprintf(text, text_size, "OFF");
    } else if (index == PARAM_LOWPASS && value >= 8000) {
        snprintf(text, text_size, "OFF");
    } else {
        snprintf(text, text_size, "%d", value);
    }
}

static void format_output_peak(const AudioOutput *audio, char *text,
                               size_t text_size)
{
    uint16_t peak = audio_output_peak_left(audio);
    if (audio_output_peak_right(audio) > peak)
        peak = audio_output_peak_right(audio);
    if (peak == 0) {
        snprintf(text, text_size, "PEAK -INF");
        return;
    }
    int tenths = (int)lround(200.0 * log10((double)peak / 32768.0));
    if (tenths > 0)
        tenths = 0;
    int magnitude = tenths < 0 ? -tenths : tenths;
    snprintf(text, text_size, "PEAK %s%d.%dDB", tenths < 0 ? "-" : "",
             magnitude / 10, magnitude % 10);
}

static void draw_peak_bar(Ui *ui, int row, const char *label, uint16_t peak)
{
    const int x = 16;
    const int y = row * 8 + 1;
    const int width = 304;
    const int height = 6;
    ui_draw_text(ui, 0, row, label, false);
    ui_draw_rect(ui, x, y, width, 1, false);
    ui_draw_rect(ui, x, y + height - 1, width, 1, false);
    ui_draw_rect(ui, x, y, 1, height, false);
    ui_draw_rect(ui, x + width - 1, y, 1, height, false);
    int fill = (int)((uint32_t)(width - 2) * peak / 32768);
    if (fill > 0)
        ui_draw_rect(ui, x + 1, y + 1, fill, height - 2, false);
}

static void draw_controls_view(Ui *ui, const AppState *state,
                               const SampleBank *bank,
                               const LoadedSample *sample,
                               const AudioOutput *audio,
                               bool external_bank)
{
    char text[64];
    ui_begin(ui, 320, true);
    ui_draw_text(ui, 0, TOP_ROW_OFFSET, "GRAIN", false);
    snprintf(text, sizeof(text), "S%02d", state->sample_index + 1);
    ui_draw_text(ui, 8, TOP_ROW_OFFSET, text, false);
    ui_draw_text(ui, 20, TOP_ROW_OFFSET, "VOICE", false);
    ui_draw_text(ui, 33, TOP_ROW_OFFSET, "GC 0.14", false);
    ui_draw_rule(ui, 0, TOP_ROW_OFFSET * 8 + 7, 144, false);
    ui_draw_rule(ui, 160, TOP_ROW_OFFSET * 8 + 7, 144, false);
    ui_draw_section_header(ui, 0, 8 + TOP_ROW_OFFSET, "CLOCK");
    ui_draw_section_header(ui, 0, 16 + TOP_ROW_OFFSET, "SOURCE");
    ui_draw_section_header(ui, 20, 10 + TOP_ROW_OFFSET, "SPACE");
    ui_draw_section_header(ui, 20, 19 + TOP_ROW_OFFSET, "OUTPUT");

    for (int index = 0; index < PARAM_COUNT; index++) {
        Parameter *parameter = &parameters[index];
        ui_draw_text(ui, parameter->column,
                     parameter->row + TOP_ROW_OFFSET,
                     parameter->name, false);
        format_parameter(index, text, sizeof(text));
        ui_draw_text(ui, parameter->value_column,
                     parameter->row + TOP_ROW_OFFSET, text,
                     index == state->selected_parameter);
        if (parameter->unit[0] != '\0' && parameter->column == 0)
            ui_draw_text(ui, 13, parameter->row + TOP_ROW_OFFSET,
                         parameter->unit, false);
    }

    ui_draw_text(ui, 0, 17 + TOP_ROW_OFFSET, "SAMPLE", false);
    snprintf(text, sizeof(text), "%02d/%02lu", state->sample_index + 1,
             (unsigned long)bank->sample_count);
    ui_draw_text(ui, 7, 17 + TOP_ROW_OFFSET, text, false);
    snprintf(text, sizeof(text), "POSITION %03d", state->playhead_x);
    ui_draw_text(ui, 0, 19 + TOP_ROW_OFFSET, text, false);
    snprintf(text, sizeof(text), "BANK %02lu %s",
             (unsigned long)bank->sample_count,
             external_bank ? "SD2SP2" : "EMBEDDED");
    ui_draw_text(ui, 0, 20 + TOP_ROW_OFFSET, text, false);
    snprintf(text, sizeof(text), "SAMPLE %.11s", sample->name);
    ui_draw_text(ui, 0, 21 + TOP_ROW_OFFSET, text, false);
    snprintf(text, sizeof(text), "UND %08lX",
             (unsigned long)audio_output_underruns(audio));
    ui_draw_text(ui, 0, 22 + TOP_ROW_OFFSET, text, false);
    snprintf(text, sizeof(text), "DMA  V%02d/%02d",
             audio_output_active_voices(audio),
             parameters[PARAM_POLYPHONY].value);
    ui_draw_text(ui, 0, 23 + TOP_ROW_OFFSET, text, false);
    snprintf(text, sizeof(text), "BUF %08lX",
             (unsigned long)audio_output_buffers_submitted(audio));
    ui_draw_text(ui, 0, 24 + TOP_ROW_OFFSET, text, false);
    snprintf(text, sizeof(text), "GRAIN %08lX",
             (unsigned long)audio_output_grains_launched(audio));
    ui_draw_text(ui, 0, 25 + TOP_ROW_OFFSET, text, false);
    snprintf(text, sizeof(text), "TRANS %03lu",
             (unsigned long)state->transients.count);
    ui_draw_text(ui, 20, 24 + TOP_ROW_OFFSET, text, false);
    unsigned long render_tenths
        = (audio_output_render_time_us(audio) + 50) / 100;
    unsigned long render_peak_tenths
        = (audio_output_render_time_peak_us(audio) + 50) / 100;
    snprintf(text, sizeof(text), "RT %lu.%lu MAX %lu.%luMS",
             render_tenths / 10, render_tenths % 10,
             render_peak_tenths / 10, render_peak_tenths % 10);
    ui_draw_text(ui, 20, 22 + TOP_ROW_OFFSET, text, false);
    format_output_peak(audio, text, sizeof(text));
    ui_draw_text(ui, 20, 23 + TOP_ROW_OFFSET, text, false);
    if (audio_output_clipping(audio))
        ui_draw_text(ui, 35, 23 + TOP_ROW_OFFSET, "CLIP", true);
    if (state->audition_ttl > 0)
        snprintf(text, sizeof(text), "AUDIO TEST");
    else
        snprintf(text, sizeof(text), "P%+d PAN%+d", state->live_pitch,
                 state->live_pan);
    ui_draw_text(ui, 20, 25 + TOP_ROW_OFFSET, text, false);
    draw_peak_bar(ui, 27, "L", audio_output_peak_left(audio));
    draw_peak_bar(ui, 28, "R", audio_output_peak_right(audio));
    ui_draw_text(ui, 0, 29,
                 "DPAD NAV B+DPAD EDIT START WAVE", false);
    ui_present(ui);
}

static bool marker_covers_column(const AppState *state, int x)
{
    for (int index = 0; index < MARKER_COUNT; index++) {
        if (state->marker_ttl[index] > 0
                && x >= state->marker_x[index] - 1
                && x <= state->marker_x[index] + 2)
            return true;
    }
    return false;
}

static void draw_boundary(Ui *ui, int x)
{
    for (int y = 0; y < UI_LOGICAL_HEIGHT; y += 8)
        ui_draw_rect(ui, x, y, 2, 4, true);
}

static void draw_waveform_view(Ui *ui, const AppState *state,
                               const SampleBank *bank,
                               const LoadedSample *sample)
{
    ui_begin(ui, SCREEN_WIDTH, false);
    const int zero_y = UI_LOGICAL_HEIGHT / 2;
    for (int x = 0; x < WAVEFORM_COLUMNS; x++) {
        int top = zero_y - (int)waveform_maximum[x] * 108 / 32768;
        int bottom = zero_y - (int)waveform_minimum[x] * 108 / 32768;
        top = clamp_int(top, 0, UI_LOGICAL_HEIGHT - 1);
        bottom = clamp_int(bottom, 0, UI_LOGICAL_HEIGHT - 1);
        if (bottom < top)
            bottom = top;
        ui_draw_rect(ui, x, top, 1, bottom - top + 1,
                     !marker_covers_column(state, x));
    }

    for (size_t index = 0; index < state->transients.count; index++) {
        int x = x_from_sample_position(
            sample, state->transients.sample_positions[index]);
        ui_draw_rect(ui, x, 0, 1, 4, true);
        ui_draw_rect(ui, x, UI_LOGICAL_HEIGHT - 4, 1, 4, true);
    }

    int range = parameters[PARAM_RANGE].value;
    int amount = range < 0 ? -range : range;
    if (amount > 0) {
        int right = clamp_int(state->playhead_x + amount,
                              0, SCREEN_WIDTH - 1);
        draw_boundary(ui, right - 1);
        if (range >= 0) {
            int left = clamp_int(state->playhead_x - amount,
                                 0, SCREEN_WIDTH - 1);
            draw_boundary(ui, left);
        }
    }
    ui_draw_rect(ui, state->playhead_x, 0, 1, UI_LOGICAL_HEIGHT, true);

    char text[64];
    snprintf(text, sizeof(text), "WAVE S%02d/%02lu %.20s",
             state->sample_index + 1, (unsigned long)bank->sample_count,
             sample->name);
    ui_draw_light_text(ui, 1, 1, text);
    snprintf(text, sizeof(text), "POS %03d RANGE %+d TRANS %03lu",
             state->playhead_x, parameters[PARAM_RANGE].value,
             (unsigned long)state->transients.count);
    ui_draw_light_text(ui, 1, 2, text);
    ui_draw_light_text(ui, 1, 28, "A GATE B BURST X/Y TRANSIENT");
    ui_draw_light_text(ui, 1, 29,
                       "C-STICK POS START CONTROL Z+START EXIT");
    ui_present(ui);
}

static void draw_error_view(Ui *ui, const char *message, int result)
{
    char text[48];
    ui_begin(ui, 400, true);
    ui_draw_text(ui, 3, 5, "GAMECUBE AMBIENT GRANULATOR", false);
    ui_draw_text(ui, 3, 8, message, true);
    snprintf(text, sizeof(text), "ERROR %d", result);
    ui_draw_text(ui, 3, 10, text, false);
    ui_draw_text(ui, 3, 13, "PRESS START TO EXIT", false);
    ui_present(ui);
}

static void move_cursor(AppState *state, int horizontal, int vertical)
{
    int selected = state->selected_parameter;
    if (horizontal != 0) {
        int target_column = parameters[selected].column == 0 ? 20 : 0;
        int best = -1;
        int distance = 100;
        for (int index = 0; index < PARAM_COUNT; index++) {
            if (parameters[index].column != target_column)
                continue;
            int candidate = abs(parameters[index].row
                              - parameters[selected].row);
            if (candidate < distance) {
                best = index;
                distance = candidate;
            }
        }
        if (best >= 0)
            state->selected_parameter = best;
    }

    if (vertical != 0) {
        selected = state->selected_parameter;
        int column = parameters[selected].column;
        int row = parameters[selected].row;
        int best = -1;
        int best_row = vertical > 0 ? 100 : -1;
        for (int index = 0; index < PARAM_COUNT; index++) {
            int candidate = parameters[index].row;
            if (parameters[index].column != column)
                continue;
            if (vertical > 0 && candidate > row && candidate < best_row) {
                best = index;
                best_row = candidate;
            }
            if (vertical < 0 && candidate < row && candidate > best_row) {
                best = index;
                best_row = candidate;
            }
        }
        if (best < 0) {
            best_row = vertical > 0 ? 100 : -1;
            for (int index = 0; index < PARAM_COUNT; index++) {
                int candidate = parameters[index].row;
                if (parameters[index].column != column)
                    continue;
                if (vertical > 0 && candidate < best_row) {
                    best = index;
                    best_row = candidate;
                }
                if (vertical < 0 && candidate > best_row) {
                    best = index;
                    best_row = candidate;
                }
            }
        }
        if (best >= 0)
            state->selected_parameter = best;
    }
}

static void apply_edit_directions(AppState *state, u16 directions)
{
    if (directions & PAD_BUTTON_LEFT)
        nudge_parameter(state->selected_parameter, -1, false);
    if (directions & PAD_BUTTON_RIGHT)
        nudge_parameter(state->selected_parameter, 1, false);
    if (directions & PAD_BUTTON_UP)
        nudge_parameter(state->selected_parameter, 1, true);
    if (directions & PAD_BUTTON_DOWN)
        nudge_parameter(state->selected_parameter, -1, true);
}

int main(void)
{
    Ui ui;
    if (!ui_init(&ui))
        return 1;
    reset_parameters();
    AppState state;
    SampleBank bank;
    LoadedSample loaded;
    AudioOutput audio;
    memset(&state, 0, sizeof(state));
    memset(&bank, 0, sizeof(bank));
    memset(&loaded, 0, sizeof(loaded));
    memset(&audio, 0, sizeof(audio));
    state.playhead_x = SCREEN_WIDTH / 2;
    snprintf(state.message, sizeof(state.message), "%s", "Starting audio...");

    bool sd_mounted = fatMountSimple("sd2", &__io_gcsd2);
    bool external_bank = sd_mounted
        && sample_bank_open(&bank, EXTERNAL_BANK_PATH);
    if (!external_bank && !sample_bank_open_memory(
            &bank, sample_bank_bin, sample_bank_bin_size)) {
        draw_error_view(&ui, "SAMPLE BANK FAILED", -1);
        while (SYS_MainLoop()) {
            ui_wait_vsync();
            PAD_ScanPads();
            if (PAD_ButtonsDown(0) & PAD_BUTTON_START)
                break;
        }
        if (sd_mounted)
            fatUnmount("sd2");
        ui_shutdown(&ui);
        return 1;
    }
    int startup_sample = random_startup_sample_index(bank.sample_count);
    if (!load_sample(&bank, &loaded, NULL, &state, startup_sample)) {
        draw_error_view(&ui, "STARTUP SAMPLE FAILED", -2);
        sample_bank_close(&bank);
        if (sd_mounted)
            fatUnmount("sd2");
        ui_shutdown(&ui);
        return 1;
    }

    AudioRenderConfig config = render_config(&state, 0);
    if (!audio_output_init(&audio, &config, loaded.samples,
                           loaded.sample_count, loaded.sample_rate)) {
        draw_error_view(&ui, "AUDIO INITIALIZATION FAILED",
                        audio.init_result);
        while (SYS_MainLoop()) {
            ui_wait_vsync();
            PAD_ScanPads();
            if (PAD_ButtonsDown(0) & PAD_BUTTON_START)
                break;
        }
        loaded_sample_free(&loaded);
        sample_bank_close(&bank);
        if (sd_mounted)
            fatUnmount("sd2");
        ui_shutdown(&ui);
        return 1;
    }

    uint32_t startup_grains_before = audio_output_grains_launched(&audio);
    int startup_grain_count = parameters[PARAM_GRAINS].value;
    trigger_burst(&audio, &state);
    state.audition_ttl = 180;
    bool startup_freeze_pending = true;

    bool running = true;
    while (running && SYS_MainLoop()) {
        ui_wait_vsync();
        PAD_ScanPads();
        u16 down = PAD_ButtonsDown(0);
        u16 up = PAD_ButtonsUp(0);
        u16 held = PAD_ButtonsHeld(0);

        if (down & PAD_BUTTON_START) {
            if (held & PAD_TRIGGER_Z)
                running = false;
            else
                state.view ^= 1;
        }
        if (down & PAD_BUTTON_B) {
            state.b_pressed = true;
            state.b_used = false;
        }

        const u16 direction_mask = PAD_BUTTON_UP | PAD_BUTTON_DOWN
                                 | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT;
        u16 directions_down = down & direction_mask;
        u16 directions_held = held & direction_mask;
        bool editing = state.view == 0 && (held & PAD_BUTTON_B) != 0;
        u16 edit_directions = (u16)edit_repeat_update(
            &state.edit_repeat, editing, directions_down, directions_held);
        u16 navigation_directions = (u16)edit_repeat_update(
            &state.navigation_repeat, state.view == 0 && !editing,
            directions_down, directions_held);
        if (edit_directions != 0) {
            apply_edit_directions(&state, edit_directions);
            state.b_used = true;
        } else if (navigation_directions != 0) {
            int horizontal = ((navigation_directions & PAD_BUTTON_RIGHT)
                              ? 1 : 0)
                           - ((navigation_directions & PAD_BUTTON_LEFT)
                              ? 1 : 0);
            int vertical = ((navigation_directions & PAD_BUTTON_DOWN)
                            ? 1 : 0)
                         - ((navigation_directions & PAD_BUTTON_UP) ? 1 : 0);
            move_cursor(&state, horizontal, vertical);
        }

        if (up & PAD_BUTTON_B) {
            if (state.b_pressed && !state.b_used)
                trigger_burst(&audio, &state);
            state.b_pressed = false;
        }
        if (down & PAD_TRIGGER_L) {
            parameters[PARAM_REVERB_FREEZE].value ^= 1;
            startup_freeze_pending = false;
        }
        if (down & PAD_TRIGGER_R)
            parameters[PARAM_CLOCK].value ^= 1;
        if (state.view == 0) {
            if (down & PAD_BUTTON_X)
                load_sample(&bank, &loaded, &audio, &state,
                            state.sample_index + 1);
            if (down & PAD_BUTTON_Y)
                load_sample(&bank, &loaded, &audio, &state,
                            state.sample_index - 1);
        } else {
            uint32_t transient_position;
            if ((down & PAD_BUTTON_X)
                    && transient_step(&state.transients,
                                      state.playhead_sample, 1,
                                      &transient_position))
                set_playhead_sample(&state, &loaded, transient_position);
            if ((down & PAD_BUTTON_Y)
                    && transient_step(&state.transients,
                                      state.playhead_sample, -1,
                                      &transient_position))
                set_playhead_sample(&state, &loaded, transient_position);
        }

        if (startup_freeze_pending
                && (uint32_t)(audio_output_grains_launched(&audio)
                    - startup_grains_before)
                    >= (uint32_t)startup_grain_count) {
            parameters[PARAM_REVERB_FREEZE].value = 1;
            startup_freeze_pending = false;
        }
        if (state.audition_ttl > 0)
            state.audition_ttl--;

        int stick_x = PAD_StickX(0);
        int stick_y = PAD_StickY(0);
        state.live_pan = abs(stick_x) > 18
            ? clamp_int(stick_x * 100 / 100, -100, 100) : 0;
        state.live_pitch = abs(stick_y) > 18
            ? clamp_int(stick_y * 12 / 100, -12, 12) : 0;

        int cstick_x = PAD_SubStickX(0);
        int cstick_y = PAD_SubStickY(0);
        if (abs(cstick_x) > 22)
            set_playhead_x(&state, &loaded,
                           state.playhead_x + cstick_x / 24);
        if (abs(cstick_y) > 22)
            parameters[PARAM_RANGE].value = clamp_int(
                parameters[PARAM_RANGE].value + cstick_y / 32,
                parameters[PARAM_RANGE].minimum,
                parameters[PARAM_RANGE].maximum);

        config = render_config(&state, held);
        audio_output_update(&audio, &config);
        GranularMarker marker;
        while (audio_output_pop_marker(&audio, &marker))
            add_marker(&state, marker.x);
        animate_markers(&state);
        if (state.view == 0)
            draw_controls_view(&ui, &state, &bank, &loaded, &audio,
                               external_bank);
        else
            draw_waveform_view(&ui, &state, &bank, &loaded);
    }

    AudioRenderConfig quiet = render_config(&state, 0);
    quiet.granular.gate = false;
    audio_output_update(&audio, &quiet);
    audio_output_stop_grains(&audio);
    audio_output_exit(&audio);
    loaded_sample_free(&loaded);
    sample_bank_close(&bank);
    if (sd_mounted)
        fatUnmount("sd2");
    ui_shutdown(&ui);
    return 0;
}
