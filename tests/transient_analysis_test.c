#include "transient_analysis.h"
#include "sample_bank.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SAMPLE_RATE 48000
#define SAMPLE_COUNT (SAMPLE_RATE * 2)

static int16_t samples[SAMPLE_COUNT];

static void add_impulse(uint32_t position, int16_t amplitude)
{
    for (uint32_t offset = 0; offset < 64; offset++) {
        int32_t value = (int32_t)amplitude * (int32_t)(64 - offset) / 64;
        samples[position + offset] = (int16_t)value;
    }
}

static void test_silence_has_no_transients(void)
{
    TransientMap map;
    memset(samples, 0, sizeof(samples));
    transient_analyze(samples, SAMPLE_COUNT, SAMPLE_RATE, &map);
    assert(map.count == 0);
    uint32_t next = 1234;
    assert(!transient_step(&map, 0, 1, &next));
    assert(next == 1234);
}

static void test_impulses_and_wrapped_stepping(void)
{
    TransientMap map;
    memset(samples, 0, sizeof(samples));
    add_impulse(12000, 18000);
    add_impulse(36000, 24000);
    add_impulse(72000, 20000);
    transient_analyze(samples, SAMPLE_COUNT, SAMPLE_RATE, &map);
    assert(map.count == 3);
    assert(map.sample_positions[0] == 12000);
    assert(map.sample_positions[1] == 36000);
    assert(map.sample_positions[2] == 72000);

    uint32_t next;
    assert(transient_step(&map, 12000, 1, &next) && next == 36000);
    assert(transient_step(&map, 72000, 1, &next) && next == 12000);
    assert(transient_step(&map, 36000, -1, &next) && next == 12000);
    assert(transient_step(&map, 12000, -1, &next) && next == 72000);
}

static void test_close_hits_collapse_to_stronger_one(void)
{
    TransientMap map;
    memset(samples, 0, sizeof(samples));
    add_impulse(12000, 10000);
    add_impulse(13000, 28000);
    transient_analyze(samples, SAMPLE_COUNT, SAMPLE_RATE, &map);
    assert(map.count == 1);
    assert(map.sample_positions[0] == 13000);
}

static void test_sample_bank(const char *bank_path, bool require_first_hit)
{
    SampleBank bank;
    memset(&bank, 0, sizeof(bank));
    assert(sample_bank_open(&bank, bank_path));
    for (uint32_t index = 0; index < bank.sample_count; index++) {
        LoadedSample sample;
        TransientMap map;
        memset(&sample, 0, sizeof(sample));
        assert(sample_bank_load(&bank, index, &sample));
        transient_analyze(sample.samples, sample.sample_count,
                          sample.sample_rate, &map);
        if (require_first_hit && index == 0) {
            assert(map.count >= 6);
            assert(map.count <= 12);
        }
        printf("sample %lu (%s) transient count: %lu\n",
               (unsigned long)(index + 1), sample.name,
               (unsigned long)map.count);
        loaded_sample_free(&sample);
    }
    sample_bank_close(&bank);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    test_silence_has_no_transients();
    test_impulses_and_wrapped_stepping();
    test_close_hits_collapse_to_stronger_one();
    test_sample_bank(argv[1], true);
    test_sample_bank(argv[2], false);
    puts("transient_analysis_test: all checks passed");
    return 0;
}
