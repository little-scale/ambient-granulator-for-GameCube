#ifndef GC_GRANULATOR_KIOSK_MODE_H
#define GC_GRANULATOR_KIOSK_MODE_H

#include <stdbool.h>
#include <stdint.h>

#define KIOSK_MIN_INTERVAL_SECONDS 30
#define KIOSK_MAX_INTERVAL_SECONDS 60

typedef enum {
    KIOSK_PHASE_READY,
    KIOSK_PHASE_THAWING,
    KIOSK_PHASE_SEEDING,
    KIOSK_PHASE_WAITING,
} KioskPhase;

typedef struct {
    bool active;
    bool has_started_texture;
    KioskPhase phase;
    uint32_t random_state;
    uint64_t deadline_ticks;
    uint32_t grains_before;
    int grain_count;
} KioskMode;

void kiosk_mode_init(KioskMode *kiosk, uint32_t random_seed);
void kiosk_mode_cancel(KioskMode *kiosk);
uint32_t kiosk_mode_next_random(KioskMode *kiosk);
int kiosk_mode_choose_pitch(KioskMode *kiosk);
int kiosk_mode_choose_sample(KioskMode *kiosk, int sample_count,
                             int current_sample, bool allow_current);
int kiosk_mode_choose_interval_seconds(KioskMode *kiosk);

#endif
