#include "transient_analysis.h"

#include <limits.h>
#include <string.h>

#define TRANSIENT_MINIMUM_GAP_MS 70
#define TRANSIENT_MINIMUM_LEVEL 288
#define TRANSIENT_MINIMUM_RISE 112
#define TRANSIENT_PROMINENCE_PERCENT 50
#define TRANSIENT_PROMINENT_GAP_MS 300

static uint32_t absolute_sample(int16_t sample)
{
    return sample == INT16_MIN ? 32768u
                              : (uint32_t)(sample < 0 ? -sample : sample);
}

static uint32_t block_level(const int16_t *samples, uint32_t first,
                            uint32_t end)
{
    uint64_t total = 0;
    for (uint32_t index = first; index < end; index++)
        total += absolute_sample(samples[index]);
    return end > first ? (uint32_t)(total / (end - first)) : 0;
}

static uint32_t refine_position(const int16_t *samples, uint32_t first,
                                uint32_t end)
{
    uint32_t best_position = first;
    uint32_t best_rise = 0;
    uint32_t previous = first > 0 ? absolute_sample(samples[first - 1]) : 0;
    for (uint32_t index = first; index < end; index++) {
        uint32_t current = absolute_sample(samples[index]);
        uint32_t rise = current > previous ? current - previous : 0;
        if (rise > best_rise) {
            best_rise = rise;
            best_position = index;
        }
        previous = current;
    }
    return best_position;
}

static void add_candidate(TransientMap *map, uint32_t position,
                          uint32_t strength, uint32_t minimum_gap)
{
    if (map->count > 0) {
        size_t previous = map->count - 1;
        uint32_t previous_position = map->sample_positions[previous];
        if (position - previous_position < minimum_gap) {
            if (strength > map->strengths[previous]) {
                map->sample_positions[previous] = position;
                map->strengths[previous] = strength;
            }
            return;
        }
    }
    if (map->count >= TRANSIENT_MAX_COUNT)
        return;
    map->sample_positions[map->count] = position;
    map->strengths[map->count] = strength;
    map->count++;
}

static void keep_prominent_candidates(TransientMap *map,
                                      uint32_t sample_rate)
{
    if (map->count < 2)
        return;

    uint32_t strongest = 0;
    for (size_t index = 0; index < map->count; index++) {
        if (map->strengths[index] > strongest)
            strongest = map->strengths[index];
    }
    uint32_t cutoff = strongest * TRANSIENT_PROMINENCE_PERCENT / 100;
    uint32_t minimum_gap = sample_rate * TRANSIENT_PROMINENT_GAP_MS / 1000;
    size_t kept = 0;
    for (size_t index = 0; index < map->count; index++) {
        if (map->strengths[index] < cutoff)
            continue;
        if (kept > 0 && map->sample_positions[index]
                - map->sample_positions[kept - 1] < minimum_gap) {
            if (map->strengths[index] > map->strengths[kept - 1]) {
                map->sample_positions[kept - 1]
                    = map->sample_positions[index];
                map->strengths[kept - 1] = map->strengths[index];
            }
            continue;
        }
        map->sample_positions[kept] = map->sample_positions[index];
        map->strengths[kept] = map->strengths[index];
        kept++;
    }
    map->count = kept;
}

void transient_analyze(const int16_t *samples, uint32_t sample_count,
                       uint32_t sample_rate, TransientMap *map)
{
    memset(map, 0, sizeof(*map));
    if (samples == NULL || sample_count < 2 || sample_rate == 0)
        return;

    uint32_t hop = sample_rate / 250;
    if (hop < 16)
        hop = 16;
    uint32_t minimum_gap = sample_rate * TRANSIENT_MINIMUM_GAP_MS / 1000;
    uint32_t coverage_gap = sample_count / TRANSIENT_MAX_COUNT;
    if (minimum_gap < coverage_gap)
        minimum_gap = coverage_gap;
    if (minimum_gap < 1)
        minimum_gap = 1;

    uint32_t slow_level = 0;
    uint32_t previous_level = 0;
    for (uint32_t first = 0; first < sample_count; first += hop) {
        uint32_t end = first + hop;
        if (end > sample_count)
            end = sample_count;
        uint32_t level = block_level(samples, first, end);
        uint32_t rise = level > previous_level
                      ? level - previous_level : 0;
        uint32_t threshold = slow_level * 3 / 8;
        if (threshold < TRANSIENT_MINIMUM_RISE)
            threshold = TRANSIENT_MINIMUM_RISE;

        if (level >= TRANSIENT_MINIMUM_LEVEL && rise > threshold
                && level > slow_level + threshold) {
            uint32_t position = refine_position(samples, first, end);
            uint32_t strength = rise + level - slow_level;
            add_candidate(map, position, strength, minimum_gap);
        }

        slow_level = (slow_level * 31 + level) / 32;
        previous_level = level;
        if (sample_count - first <= hop)
            break;
    }
    keep_prominent_candidates(map, sample_rate);
}

bool transient_step(const TransientMap *map, uint32_t current_sample,
                    int direction, uint32_t *next_sample)
{
    if (map == NULL || next_sample == NULL || map->count == 0
            || direction == 0)
        return false;

    if (direction > 0) {
        for (size_t index = 0; index < map->count; index++) {
            if (map->sample_positions[index] > current_sample) {
                *next_sample = map->sample_positions[index];
                return true;
            }
        }
        *next_sample = map->sample_positions[0];
        return true;
    }

    for (size_t index = map->count; index > 0; index--) {
        if (map->sample_positions[index - 1] < current_sample) {
            *next_sample = map->sample_positions[index - 1];
            return true;
        }
    }
    *next_sample = map->sample_positions[map->count - 1];
    return true;
}
