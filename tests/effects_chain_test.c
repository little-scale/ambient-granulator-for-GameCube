#include "effects_chain.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_FRAMES 16000

static int16_t output[TEST_FRAMES * 2];
static int32_t wide_input[TEST_FRAMES * 2];

static EffectsConfig default_config(void)
{
    EffectsConfig config = {
        .wet_percent = 25,
        .feedback_tenths_percent = 750,
        .size_percent = 55,
        .damping_percent = 45,
        .freeze = false,
        .phaser_depth_percent = 0,
        .phaser_speed_hundredths_hz = 10,
        .highpass_hz = 0,
        .lowpass_hz = 8000,
    };
    return config;
}

static void test_coefficients(void)
{
    int lowpass = effects_lowpass_alpha_q15(1000, EFFECTS_SAMPLE_RATE);
    int highpass = effects_highpass_alpha_q15(1000, EFFECTS_SAMPLE_RATE);
    int damping = effects_damping_alpha_q15(45, EFFECTS_SAMPLE_RATE);
    assert(lowpass > 0 && lowpass < 32768);
    assert(highpass > 0 && highpass < 32768);
    assert(damping > 0 && damping < 32768);
    assert(damping < 32767 - 45 * 260);
    assert(effects_lowpass_alpha_q15(1000, EFFECTS_SAMPLE_RATE)
        != effects_lowpass_alpha_q15(1010, EFFECTS_SAMPLE_RATE));
    assert(effects_highpass_alpha_q15(1000, EFFECTS_SAMPLE_RATE)
        != effects_highpass_alpha_q15(1010, EFFECTS_SAMPLE_RATE));
}

static void test_dry_bypass_and_delay_scaling(void)
{
    EffectsChain chain;
    EffectsConfig config = default_config();
    config.wet_percent = 0;
    assert(effects_chain_init(&chain));
    for (int frame = 0; frame < 1024; frame++) {
        output[frame * 2] = (int16_t)(frame - 512);
        output[frame * 2 + 1] = (int16_t)(512 - frame);
    }
    int16_t original[2048];
    memcpy(original, output, sizeof(original));
    effects_chain_process(&chain, output, 1024, &config);
    assert(memcmp(original, output, sizeof(original)) == 0);
    assert(chain.lengths[0] >= 1350 && chain.lengths[0] <= 1360);
    assert(chain.lengths[3] >= 3260 && chain.lengths[3] <= 3270);

    config.size_percent = 100;
    effects_chain_process(&chain, output, 1, &config);
    assert(EFFECTS_FDN_LINES == 8);
    assert(chain.lengths[0] >= 19720 && chain.lengths[0] <= 19735);
    assert(chain.lengths[3] >= 47480 && chain.lengths[3] <= 47500);
    assert(chain.lengths[7] >= 60500 && chain.lengths[7] <= 60520);
    effects_chain_exit(&chain);
}

static void test_stereo_reverb_tail(void)
{
    EffectsChain chain;
    EffectsConfig config = default_config();
    config.wet_percent = 100;
    config.damping_percent = 0;
    assert(effects_chain_init(&chain));
    memset(output, 0, sizeof(output));
    output[0] = 24000;
    effects_chain_process(&chain, output, TEST_FRAMES, &config);
    bool left_tail = false;
    bool right_tail = false;
    for (int frame = 1000; frame < TEST_FRAMES; frame++) {
        left_tail |= output[frame * 2] != 0;
        right_tail |= output[frame * 2 + 1] != 0;
    }
    assert(left_tail);
    assert(right_tail);
    effects_chain_exit(&chain);
}

static void test_diffused_early_reflections(void)
{
    EffectsChain chain;
    EffectsConfig config = default_config();
    config.wet_percent = 100;
    config.feedback_tenths_percent = 0;
    config.size_percent = 100;
    config.phaser_depth_percent = 0;
    assert(effects_chain_init(&chain));
    memset(output, 0, sizeof(output));
    output[0] = 24000;
    output[1] = -12000;
    effects_chain_process(&chain, output, TEST_FRAMES, &config);

    int first = -1;
    int nonzero_frames = 0;
    for (int frame = 0; frame < TEST_FRAMES; frame++) {
        bool nonzero = output[frame * 2] != 0
                    || output[frame * 2 + 1] != 0;
        if (nonzero && first < 0)
            first = frame;
        nonzero_frames += nonzero ? 1 : 0;
    }
    assert(first > 4000 && first < 10000);
    assert(nonzero_frames > 500);
    effects_chain_exit(&chain);
}

static void test_freeze_blocks_excitation_but_keeps_dry(void)
{
    EffectsChain chain;
    EffectsConfig config = default_config();
    config.freeze = true;
    config.wet_percent = 100;
    assert(effects_chain_init(&chain));
    memset(output, 0, sizeof(output));
    output[0] = 30000;
    output[1] = -30000;
    effects_chain_process(&chain, output, 8000, &config);
    assert(chain.feedback_q15 == 32768);
    for (int frame = 0; frame < 8000; frame++) {
        assert(output[frame * 2] == 0);
        assert(output[frame * 2 + 1] == 0);
    }

    effects_chain_reset(&chain);
    config.wet_percent = 0;
    memset(output, 0, 64 * 2 * sizeof(*output));
    output[0] = 12345;
    output[1] = -12345;
    effects_chain_process(&chain, output, 64, &config);
    assert(output[0] == 12345);
    assert(output[1] == -12345);

    effects_chain_reset(&chain);
    config.freeze = false;
    config.feedback_tenths_percent = 999;
    effects_chain_process(&chain, output, 1, &config);
    assert(chain.feedback_q15 == 999 * 32767 / 1000);
    effects_chain_exit(&chain);
}

static void test_output_filters(void)
{
    EffectsChain chain;
    EffectsConfig config = default_config();
    config.wet_percent = 0;
    config.highpass_hz = 1000;
    assert(effects_chain_init(&chain));
    for (int frame = 0; frame < 4096; frame++) {
        output[frame * 2] = 10000;
        output[frame * 2 + 1] = 10000;
    }
    effects_chain_process(&chain, output, 4096, &config);
    assert(output[0] > 0);
    assert(output[4095 * 2] >= 0);
    assert(output[4095 * 2] < output[0]);

    effects_chain_reset(&chain);
    config.highpass_hz = 0;
    config.lowpass_hz = 1000;
    for (int frame = 0; frame < 4096; frame++) {
        output[frame * 2] = 10000;
        output[frame * 2 + 1] = 10000;
    }
    effects_chain_process(&chain, output, 4096, &config);
    assert(output[0] > 0 && output[0] < 10000);
    assert(output[4095 * 2] > output[0]);
    assert(output[4095 * 2] <= 10000);
    effects_chain_exit(&chain);
}

static void test_subtle_stereo_phaser(void)
{
    EffectsChain chain;
    EffectsConfig config = default_config();
    config.wet_percent = 0;
    config.phaser_depth_percent = 30;
    config.phaser_speed_hundredths_hz = 10;
    assert(effects_phaser_increment(10, EFFECTS_SAMPLE_RATE) > 0);
    assert(effects_chain_init(&chain));
    for (int frame = 0; frame < 4096; frame++) {
        int16_t value = (int16_t)(((frame * 97) & 16383) - 8192);
        output[frame * 2] = value;
        output[frame * 2 + 1] = value;
    }
    effects_chain_process(&chain, output, 4096, &config);
    for (int voice = 0; voice < EFFECTS_PHASER_VOICES; voice++)
        assert(chain.phaser_phase[voice] != 0);
    for (int voice = 1; voice < EFFECTS_PHASER_VOICES; voice++)
        assert(chain.phaser_phase[voice]
            != chain.phaser_phase[voice - 1]);
    bool changed = false;
    bool stereo_motion = false;
    for (int frame = 0; frame < 4096; frame++) {
        int16_t original = (int16_t)(((frame * 97) & 16383) - 8192);
        changed |= output[frame * 2] != original;
        stereo_motion |= output[frame * 2] != output[frame * 2 + 1];
    }
    assert(changed);
    assert(stereo_motion);
    effects_chain_exit(&chain);
}

static void test_wide_soft_limit_and_internal_overload(void)
{
    EffectsChain chain;
    EffectsConfig config = default_config();
    config.wet_percent = 0;
    config.phaser_depth_percent = 0;
    assert(effects_chain_init(&chain));

    memset(wide_input, 0, sizeof(wide_input));
    wide_input[0] = 200000;
    wide_input[1] = -200000;
    effects_chain_process_wide(&chain, wide_input, output, 16, &config);
    assert(output[0] > EFFECTS_OUTPUT_SOFT_CLIP_KNEE
        && output[0] < 30000);
    assert(output[1] < -EFFECTS_OUTPUT_SOFT_CLIP_KNEE
        && output[1] > -30000);
    assert(effects_chain_overloaded(&chain));
    assert(chain.input_overload_events == 2);

    memset(wide_input, 0, 16 * 2 * sizeof(*wide_input));
    wide_input[0] = 10000;
    wide_input[1] = INT16_MIN;
    effects_chain_process_wide(&chain, wide_input, output, 16, &config);
    assert(output[0] == 10000);
    assert(output[1] < -EFFECTS_OUTPUT_SOFT_CLIP_KNEE);
    assert(output[1] > -30000);
    assert(!effects_chain_overloaded(&chain));

    config.wet_percent = 100;
    config.feedback_tenths_percent = 999;
    effects_chain_process_wide(&chain, wide_input, output, 1, &config);
    for (int line = 0; line < EFFECTS_FDN_LINES; line++)
        chain.delay[line * EFFECTS_FDN_MAX_DELAY + chain.positions[line]]
            = 32767;
    uint64_t before = chain.fdn_overload_events;
    memset(wide_input, 0, 2 * sizeof(*wide_input));
    effects_chain_process_wide(&chain, wide_input, output, 1, &config);
    assert(effects_chain_overloaded(&chain));
    assert(chain.fdn_overload_events > before);

    effects_chain_reset(&chain);
    assert(chain.input_overload_events == 0);
    assert(chain.fdn_overload_events == 0);
    assert(chain.output_overload_events == 0);
    assert(!effects_chain_overloaded(&chain));
    effects_chain_exit(&chain);
}

static void test_final_output_soft_clip(void)
{
    EffectsChain chain;
    EffectsConfig config = default_config();
    config.wet_percent = 0;
    config.phaser_depth_percent = 0;
    assert(effects_chain_init(&chain));

    memset(wide_input, 0, 8 * 2 * sizeof(*wide_input));
    wide_input[0] = 20000;
    wide_input[1] = -20000;
    wide_input[2] = EFFECTS_OUTPUT_SOFT_CLIP_KNEE;
    wide_input[3] = -EFFECTS_OUTPUT_SOFT_CLIP_KNEE;
    wide_input[4] = 28000;
    wide_input[5] = -28000;
    effects_chain_process_wide(&chain, wide_input, output, 8, &config);
    assert(output[0] == 20000);
    assert(output[1] == -20000);
    assert(output[2] == EFFECTS_OUTPUT_SOFT_CLIP_KNEE);
    assert(output[3] == -EFFECTS_OUTPUT_SOFT_CLIP_KNEE);
    assert(output[4] > EFFECTS_OUTPUT_SOFT_CLIP_KNEE);
    assert(output[4] < 28000);
    assert(output[5] < -EFFECTS_OUTPUT_SOFT_CLIP_KNEE);
    assert(output[5] > -28000);
    assert(!effects_chain_overloaded(&chain));

    int16_t non_frozen = output[4];
    effects_chain_reset(&chain);
    config.freeze = true;
    memset(wide_input, 0, 2 * sizeof(*wide_input));
    wide_input[0] = 28000;
    effects_chain_process_wide(&chain, wide_input, output, 1, &config);
    assert(output[0] == non_frozen);
    assert(!effects_chain_overloaded(&chain));
    effects_chain_exit(&chain);
}

int main(void)
{
    test_coefficients();
    test_dry_bypass_and_delay_scaling();
    test_stereo_reverb_tail();
    test_diffused_early_reflections();
    test_freeze_blocks_excitation_but_keeps_dry();
    test_subtle_stereo_phaser();
    test_wide_soft_limit_and_internal_overload();
    test_final_output_soft_clip();
    test_output_filters();
    puts("effects_chain_test: all checks passed");
    return 0;
}
