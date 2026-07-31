#include "kiosk_mode.h"

#include <stddef.h>

static const int kiosk_pitches[] = { -12, -7, 0, 7, 12 };

void kiosk_mode_init(KioskMode *kiosk, uint32_t random_seed)
{
    kiosk->active = true;
    kiosk->has_started_texture = false;
    kiosk->phase = KIOSK_PHASE_READY;
    kiosk->random_state = random_seed != 0
        ? random_seed : UINT32_C(0x6D2B79F5);
    kiosk->deadline_ticks = 0;
    kiosk->grains_before = 0;
    kiosk->grain_count = 0;
}

void kiosk_mode_cancel(KioskMode *kiosk)
{
    kiosk->active = false;
}

uint32_t kiosk_mode_next_random(KioskMode *kiosk)
{
    kiosk->random_state ^= kiosk->random_state << 13;
    kiosk->random_state ^= kiosk->random_state >> 17;
    kiosk->random_state ^= kiosk->random_state << 5;
    return kiosk->random_state;
}

int kiosk_mode_choose_pitch(KioskMode *kiosk)
{
    size_t count = sizeof(kiosk_pitches) / sizeof(kiosk_pitches[0]);
    return kiosk_pitches[kiosk_mode_next_random(kiosk) % count];
}

int kiosk_mode_choose_sample(KioskMode *kiosk, int sample_count,
                             int current_sample, bool allow_current)
{
    if (sample_count <= 1)
        return 0;
    uint32_t random = kiosk_mode_next_random(kiosk);
    if (allow_current)
        return (int)(random % (uint32_t)sample_count);

    int current = current_sample % sample_count;
    if (current < 0)
        current += sample_count;
    int selected = (int)(random % (uint32_t)(sample_count - 1));
    if (selected >= current)
        selected++;
    return selected;
}

int kiosk_mode_choose_interval_seconds(KioskMode *kiosk)
{
    int span = KIOSK_MAX_INTERVAL_SECONDS
             - KIOSK_MIN_INTERVAL_SECONDS + 1;
    return KIOSK_MIN_INTERVAL_SECONDS
         + (int)(kiosk_mode_next_random(kiosk) % (uint32_t)span);
}
