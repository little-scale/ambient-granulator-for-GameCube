#include "effects_chain.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DS_EFFECTS_SAMPLE_RATE 16384
#define INTERNAL_SOFT_LIMIT_KNEE 30000
#define FDN_MATRIX_Q15 11585
#define FDN_IO_Q15 5793

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static int16_t clamp_sample(int64_t value)
{
    if (value < INT16_MIN)
        return INT16_MIN;
    if (value > INT16_MAX)
        return INT16_MAX;
    return (int16_t)value;
}

static int16_t soft_limit_sample(int64_t value, int knee,
                                 bool *overloaded)
{
    int range = INT16_MAX - knee;
    bool negative = value < 0;
    uint64_t magnitude = negative
        ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
    if (value < INT16_MIN || value > INT16_MAX)
        *overloaded = true;
    if (magnitude <= (uint64_t)knee)
        return (int16_t)value;

    uint64_t excess = magnitude - (uint64_t)knee;
    uint64_t compressed = (uint64_t)knee
        + (uint64_t)range * excess / (excess + (uint64_t)range);
    if (compressed > INT16_MAX)
        compressed = INT16_MAX;
    return (int16_t)(negative ? -(int64_t)compressed
                              : (int64_t)compressed);
}

static int32_t limit_input(EffectsChain *chain, int64_t value)
{
    bool overloaded = false;
    int16_t limited = soft_limit_sample(
        value, INTERNAL_SOFT_LIMIT_KNEE, &overloaded);
    if (overloaded) {
        chain->block_overloaded = true;
        chain->input_overload_events++;
    }
    return limited;
}

static int32_t limit_fdn(EffectsChain *chain, int64_t value, bool freeze)
{
    if (freeze) {
        bool overloaded = value < INT16_MIN || value > INT16_MAX;
        if (overloaded) {
            chain->block_overloaded = true;
            chain->fdn_overload_events++;
        }
        return clamp_sample(value);
    }

    bool overloaded = false;
    int16_t limited = soft_limit_sample(
        value, INTERNAL_SOFT_LIMIT_KNEE, &overloaded);
    if (overloaded) {
        chain->block_overloaded = true;
        chain->fdn_overload_events++;
    }
    return limited;
}

static int16_t limit_output(EffectsChain *chain, int64_t value)
{
    bool overloaded = false;
    int16_t limited = soft_limit_sample(
        value, EFFECTS_OUTPUT_SOFT_CLIP_KNEE, &overloaded);
    if (overloaded) {
        chain->block_overloaded = true;
        chain->output_overload_events++;
    }
    return limited;
}

int effects_lowpass_alpha_q15(int cutoff_hz, int sample_rate)
{
    cutoff_hz = clamp_int(cutoff_hz, 1, sample_rate / 2 - 1);
    int omega_q15 = (int)(((int64_t)cutoff_hz * 205887) / sample_rate);
    return (int)(((int64_t)omega_q15 << 15) / (32768 + omega_q15));
}

int effects_highpass_alpha_q15(int cutoff_hz, int sample_rate)
{
    cutoff_hz = clamp_int(cutoff_hz, 1, sample_rate / 2 - 1);
    int omega_q15 = (int)(((int64_t)cutoff_hz * 205887) / sample_rate);
    return (int)(((int64_t)32768 << 15) / (32768 + omega_q15));
}

int effects_damping_alpha_q15(int damping_percent, int sample_rate)
{
    damping_percent = clamp_int(damping_percent, 0, 100);
    sample_rate = clamp_int(sample_rate, 1, 192000);
    double ds_alpha = (32767.0 - damping_percent * 260.0) / 32768.0;
    double exponent = (double)DS_EFFECTS_SAMPLE_RATE / sample_rate;
    double native_alpha = 1.0 - pow(1.0 - ds_alpha, exponent);
    int q15 = (int)lround(native_alpha * 32768.0);
    return clamp_int(q15, 1, 32767);
}

uint32_t effects_phaser_increment(int speed_hundredths_hz, int sample_rate)
{
    speed_hundredths_hz = clamp_int(speed_hundredths_hz, 1, 100);
    sample_rate = clamp_int(sample_rate, 1, 192000);
    return (uint32_t)((UINT64_C(1) << 32)
        * (uint32_t)speed_hundredths_hz
        / ((uint64_t)sample_rate * 100));
}

static int phaser_coefficient_q15(uint32_t phase)
{
    uint32_t triangle = phase < UINT32_C(0x80000000)
        ? phase >> 15 : (UINT32_MAX - phase) >> 15;
    return 8192 + (int)((uint64_t)triangle * 16384 >> 16);
}

static int32_t process_allpass(EffectsChain *chain, int voice, int channel,
                               int32_t input, int coefficient_q15)
{
    int32_t value = input;
    for (int stage = 0; stage < EFFECTS_PHASER_STAGES; stage++) {
        EffectsAllpassState *state
            = &chain->phaser[voice][channel][stage];
        int64_t numerator = -(int64_t)coefficient_q15 * value
                          + (int64_t)state->previous_input * 32768
                          + (int64_t)coefficient_q15
                          * state->previous_output;
        int32_t output = (int32_t)(numerator >> 15);
        state->previous_input = value;
        state->previous_output = output;
        value = output;
    }
    return value;
}

static int32_t process_phaser_ensemble(EffectsChain *chain, int channel,
                                       int32_t input, int depth)
{
    int64_t ensemble = 0;
    for (int voice = 0; voice < EFFECTS_PHASER_VOICES; voice++) {
        uint32_t phase = chain->phaser_phase[voice]
                       + (uint32_t)voice * UINT32_C(0x40000000);
        if (channel != 0)
            phase += UINT32_C(0x20000000)
                   + (uint32_t)voice * UINT32_C(0x08000000);
        int coefficient = phaser_coefficient_q15(phase);
        int32_t phased = process_allpass(chain, voice, channel, input,
                                         coefficient);
        ensemble += (input + phased) / 2;
    }
    int32_t averaged = (int32_t)(ensemble / EFFECTS_PHASER_VOICES);
    return (input * (100 - depth) + averaged * depth) / 100;
}

static int16_t *delay_cell(EffectsChain *chain, int line, int position)
{
    return chain->delay + line * EFFECTS_FDN_MAX_DELAY + position;
}

static int16_t *diffuser_cell(EffectsChain *chain, int channel, int stage,
                              int position)
{
    int bank = channel * EFFECTS_DIFFUSER_STAGES + stage;
    return chain->diffuser_delay
         + bank * EFFECTS_DIFFUSER_MAX_DELAY + position;
}

static int32_t process_diffuser(EffectsChain *chain, int channel,
                                int32_t input)
{
    static const int lengths[2][EFFECTS_DIFFUSER_STAGES] = {
        { 149, 337, 521, 733 },
        { 163, 359, 547, 769 },
    };
    static const int coefficients[EFFECTS_DIFFUSER_STAGES] = {
        21627, 20316, 19333, 20972,
    };

    int32_t value = input;
    for (int stage = 0; stage < EFFECTS_DIFFUSER_STAGES; stage++) {
        int length = lengths[channel][stage];
        int position = chain->diffuser_positions[channel][stage];
        int32_t delayed = *diffuser_cell(chain, channel, stage, position);
        int coefficient = coefficients[stage];
        int32_t output = delayed - (int32_t)((int64_t)coefficient
                                           * value >> 15);
        int32_t stored = value + (int32_t)((int64_t)coefficient
                                         * output >> 15);
        *diffuser_cell(chain, channel, stage, position) = (int16_t)
            limit_fdn(chain, stored, false);
        value = limit_fdn(chain, output, false);
        position++;
        chain->diffuser_positions[channel][stage]
            = position < length ? position : 0;
    }
    return value;
}

static void hadamard8(const int32_t input[EFFECTS_FDN_LINES],
                      int32_t output[EFFECTS_FDN_LINES])
{
    int32_t a0 = input[0] + input[1];
    int32_t a1 = input[0] - input[1];
    int32_t a2 = input[2] + input[3];
    int32_t a3 = input[2] - input[3];
    int32_t a4 = input[4] + input[5];
    int32_t a5 = input[4] - input[5];
    int32_t a6 = input[6] + input[7];
    int32_t a7 = input[6] - input[7];
    int32_t b0 = a0 + a2;
    int32_t b1 = a1 + a3;
    int32_t b2 = a0 - a2;
    int32_t b3 = a1 - a3;
    int32_t b4 = a4 + a6;
    int32_t b5 = a5 + a7;
    int32_t b6 = a4 - a6;
    int32_t b7 = a5 - a7;
    const int32_t sums[EFFECTS_FDN_LINES] = {
        b0 + b4, b1 + b5, b2 + b6, b3 + b7,
        b0 - b4, b1 - b5, b2 - b6, b3 - b7,
    };
    for (int line = 0; line < EFFECTS_FDN_LINES; line++)
        output[line] = (int32_t)((int64_t)sums[line]
                               * FDN_MATRIX_Q15 >> 15);
}

static void configure(EffectsChain *chain, const EffectsConfig *config)
{
    int size = clamp_int(config->size_percent, 0, 100);
    if (size != chain->configured_size) {
        static const int ds_base_lengths[EFFECTS_FDN_LINES] = {
            421, 613, 809, 1013, 487, 701, 941, 1291,
        };
        uint64_t scale_q16;
        if (size <= 55) {
            scale_q16 = (uint64_t)(55 + size) << 16;
        } else {
            uint64_t above_default = (uint64_t)(size - 55);
            scale_q16 = UINT64_C(110) << 16;
            scale_q16 += ((UINT64_C(1490) << 16)
                        * above_default * above_default) / (45 * 45);
        }
        for (int line = 0; line < EFFECTS_FDN_LINES; line++) {
            int native_base = (ds_base_lengths[line] * EFFECTS_SAMPLE_RATE
                             + DS_EFFECTS_SAMPLE_RATE / 2)
                            / DS_EFFECTS_SAMPLE_RATE;
            int length = (int)((uint64_t)native_base * scale_q16
                             / (UINT64_C(100) << 16));
            chain->lengths[line] = clamp_int(
                length, 64, EFFECTS_FDN_MAX_DELAY);
            if (chain->positions[line] >= chain->lengths[line])
                chain->positions[line] = 0;
        }
        chain->configured_size = size;
    }

    int damping = clamp_int(config->damping_percent, 0, 100);
    if (damping != chain->configured_damping) {
        chain->damping_q15 = effects_damping_alpha_q15(
            damping, EFFECTS_SAMPLE_RATE);
        chain->configured_damping = damping;
    }
    chain->feedback_q15 = config->freeze ? 32768
        : clamp_int(config->feedback_tenths_percent, 0, 999) * 32767 / 1000;
    chain->highpass_alpha_q15 = effects_highpass_alpha_q15(
        config->highpass_hz > 0 ? config->highpass_hz : 1,
        EFFECTS_SAMPLE_RATE);
    chain->lowpass_alpha_q15 = effects_lowpass_alpha_q15(
        config->lowpass_hz, EFFECTS_SAMPLE_RATE);
}

static int32_t process_filter(EffectsFilterState *state, int32_t input,
                              const EffectsChain *chain,
                              const EffectsConfig *config)
{
    int32_t filtered = input;
    if (config->highpass_hz > 0) {
        int64_t difference = (int64_t)state->highpass_output + input
                           - state->highpass_previous_input;
        state->highpass_output = (int32_t)(difference
                                      * chain->highpass_alpha_q15 >> 15);
        state->highpass_previous_input = input;
        filtered = state->highpass_output;
    } else {
        state->highpass_previous_input = input;
        state->highpass_output = 0;
    }

    if (config->lowpass_hz < 8000) {
        state->lowpass_output += (int32_t)(((int64_t)(filtered
            - state->lowpass_output) * chain->lowpass_alpha_q15) >> 15);
        filtered = state->lowpass_output;
    } else {
        state->lowpass_output = filtered;
    }
    return filtered;
}

bool effects_chain_init(EffectsChain *chain)
{
    memset(chain, 0, sizeof(*chain));
    chain->delay = calloc(EFFECTS_FDN_LINES * EFFECTS_FDN_MAX_DELAY,
                          sizeof(*chain->delay));
    chain->diffuser_delay = calloc(2 * EFFECTS_DIFFUSER_STAGES
                                 * EFFECTS_DIFFUSER_MAX_DELAY,
                                   sizeof(*chain->diffuser_delay));
    if (chain->delay == NULL || chain->diffuser_delay == NULL) {
        free(chain->diffuser_delay);
        free(chain->delay);
        memset(chain, 0, sizeof(*chain));
        return false;
    }
    chain->configured_size = -1;
    chain->configured_damping = -1;
    chain->initialized = true;
    return true;
}

void effects_chain_reset(EffectsChain *chain)
{
    if (!chain->initialized)
        return;
    memset(chain->delay, 0, EFFECTS_FDN_LINES * EFFECTS_FDN_MAX_DELAY
                              * sizeof(*chain->delay));
    memset(chain->diffuser_delay, 0, 2 * EFFECTS_DIFFUSER_STAGES
        * EFFECTS_DIFFUSER_MAX_DELAY * sizeof(*chain->diffuser_delay));
    memset(chain->positions, 0, sizeof(chain->positions));
    memset(chain->diffuser_positions, 0,
           sizeof(chain->diffuser_positions));
    memset(chain->damped, 0, sizeof(chain->damped));
    memset(chain->phaser, 0, sizeof(chain->phaser));
    memset(chain->phaser_phase, 0, sizeof(chain->phaser_phase));
    memset(chain->filters, 0, sizeof(chain->filters));
    chain->input_overload_events = 0;
    chain->fdn_overload_events = 0;
    chain->output_overload_events = 0;
    chain->block_overloaded = false;
}

static void process(EffectsChain *chain, const int16_t *narrow_input,
                    const int32_t *wide_input, int16_t *output,
                    size_t frames, const EffectsConfig *config)
{
    if (!chain->initialized)
        return;
    chain->block_overloaded = false;
    configure(chain, config);
    int wet = clamp_int(config->wet_percent, 0, 100);
    int phaser_depth = clamp_int(config->phaser_depth_percent, 0, 100);
    uint32_t base_phaser_increment = effects_phaser_increment(
        config->phaser_speed_hundredths_hz, EFFECTS_SAMPLE_RATE);
    static const int phaser_rate_percent[EFFECTS_PHASER_VOICES] = {
        89, 100, 113, 127,
    };
    uint32_t phaser_increment[EFFECTS_PHASER_VOICES];
    for (int voice = 0; voice < EFFECTS_PHASER_VOICES; voice++) {
        phaser_increment[voice] = (uint32_t)(
            (uint64_t)base_phaser_increment
            * (uint32_t)phaser_rate_percent[voice] / 100);
        if (phaser_increment[voice] == 0)
            phaser_increment[voice] = 1;
    }

    for (size_t frame = 0; frame < frames; frame++) {
        int64_t raw_left = wide_input != NULL
            ? wide_input[frame * 2] : narrow_input[frame * 2];
        int64_t raw_right = wide_input != NULL
            ? wide_input[frame * 2 + 1] : narrow_input[frame * 2 + 1];
        int32_t dry_left = limit_input(chain, raw_left);
        int32_t dry_right = limit_input(chain, raw_right);
        int32_t source_left = dry_left;
        int32_t source_right = dry_right;
        if (phaser_depth > 0) {
            source_left = process_phaser_ensemble(chain, 0, dry_left,
                                                  phaser_depth);
            source_right = process_phaser_ensemble(chain, 1, dry_right,
                                                   phaser_depth);
            for (int voice = 0; voice < EFFECTS_PHASER_VOICES; voice++)
                chain->phaser_phase[voice] += phaser_increment[voice];
        }
        int32_t diffuser_input_left = config->freeze ? 0 : source_left;
        int32_t diffuser_input_right = config->freeze ? 0 : source_right;
        int32_t diffused_left = process_diffuser(
            chain, 0, diffuser_input_left);
        int32_t diffused_right = process_diffuser(
            chain, 1, diffuser_input_right);
        int32_t input[EFFECTS_FDN_LINES] = { 0 };
        if (!config->freeze) {
            int32_t middle = (int32_t)((int64_t)(diffused_left
                + diffused_right) * FDN_IO_Q15 >> 15);
            int32_t side = (int32_t)((int64_t)(diffused_left
                - diffused_right) * FDN_IO_Q15 >> 15);
            input[0] = middle;
            input[1] = side;
            input[2] = middle;
            input[3] = -side;
            input[4] = -middle;
            input[5] = side;
            input[6] = -middle;
            input[7] = -side;
        }

        int32_t delayed[EFFECTS_FDN_LINES];
        int32_t dense[EFFECTS_FDN_LINES];
        for (int line = 0; line < EFFECTS_FDN_LINES; line++) {
            if (chain->positions[line] >= chain->lengths[line])
                chain->positions[line] = 0;
            int position = chain->positions[line];
            int length = chain->lengths[line];
            delayed[line] = *delay_cell(chain, line, position);
            int early_position = position + length * 2 / 3;
            int middle_position = position + length / 3;
            if (early_position >= length)
                early_position -= length;
            if (middle_position >= length)
                middle_position -= length;
            int32_t early = *delay_cell(chain, line, early_position);
            int32_t middle = *delay_cell(chain, line, middle_position);
            dense[line] = delayed[line] + early / 2
                        + ((line & 1) ? -middle / 4 : middle / 4);
        }

        int32_t matrix[EFFECTS_FDN_LINES];
        hadamard8(delayed, matrix);

        for (int line = 0; line < EFFECTS_FDN_LINES; line++) {
            int32_t target = limit_fdn(chain, matrix[line], config->freeze);
            if (config->freeze)
                chain->damped[line] = target;
            else
                chain->damped[line] += (int32_t)(((int64_t)(target
                    - chain->damped[line]) * chain->damping_q15) >> 15);
            int32_t feedback = (int32_t)((int64_t)chain->damped[line]
                                       * chain->feedback_q15 >> 15);
            *delay_cell(chain, line, chain->positions[line]) = (int16_t)
                limit_fdn(chain, (int64_t)input[line] + feedback,
                          config->freeze);
            chain->positions[line]++;
        }

        int32_t left_sum = dense[0] + dense[1] + dense[2] + dense[3]
                         - dense[4] - dense[5] - dense[6] - dense[7];
        int32_t right_sum = dense[0] - dense[1] + dense[2] - dense[3]
                          + dense[4] - dense[5] + dense[6] - dense[7];
        int32_t wet_left = (int32_t)((int64_t)left_sum
                                   * FDN_MATRIX_Q15 >> 15);
        int32_t wet_right = (int32_t)((int64_t)right_sum
                                    * FDN_MATRIX_Q15 >> 15);
        int32_t mixed_left = (source_left * (100 - wet)
                            + wet_left * wet) / 100;
        int32_t mixed_right = (source_right * (100 - wet)
                             + wet_right * wet) / 100;
        mixed_left = process_filter(&chain->filters[0], mixed_left,
                                    chain, config);
        mixed_right = process_filter(&chain->filters[1], mixed_right,
                                     chain, config);
        output[frame * 2] = limit_output(chain, mixed_left);
        output[frame * 2 + 1] = limit_output(chain, mixed_right);
    }
}

void effects_chain_process(EffectsChain *chain, int16_t *samples,
                           size_t frames, const EffectsConfig *config)
{
    process(chain, samples, NULL, samples, frames, config);
}

void effects_chain_process_wide(EffectsChain *chain,
                                const int32_t *input, int16_t *output,
                                size_t frames, const EffectsConfig *config)
{
    process(chain, NULL, input, output, frames, config);
}

bool effects_chain_overloaded(const EffectsChain *chain)
{
    return chain->block_overloaded;
}

void effects_chain_exit(EffectsChain *chain)
{
    free(chain->diffuser_delay);
    free(chain->delay);
    memset(chain, 0, sizeof(*chain));
}
