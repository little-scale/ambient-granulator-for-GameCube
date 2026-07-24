#ifndef GC_GRANULATOR_TRANSIENT_ANALYSIS_H
#define GC_GRANULATOR_TRANSIENT_ANALYSIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRANSIENT_MAX_COUNT 256

typedef struct {
    uint32_t sample_positions[TRANSIENT_MAX_COUNT];
    uint32_t strengths[TRANSIENT_MAX_COUNT];
    size_t count;
} TransientMap;

void transient_analyze(const int16_t *samples, uint32_t sample_count,
                       uint32_t sample_rate, TransientMap *map);
bool transient_step(const TransientMap *map, uint32_t current_sample,
                    int direction, uint32_t *next_sample);

#endif
