#include "kiosk_mode.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static bool allowed_pitch(int pitch)
{
    const int pitches[] = { -12, -7, 0, 7, 12 };
    for (size_t index = 0; index < sizeof(pitches) / sizeof(pitches[0]);
            index++) {
        if (pitch == pitches[index])
            return true;
    }
    return false;
}

int main(void)
{
    KioskMode kiosk;
    kiosk_mode_init(&kiosk, 0);
    assert(kiosk.active);
    assert(kiosk.phase == KIOSK_PHASE_READY);
    assert(kiosk.random_state != 0);

    bool saw_pitch[5] = { false, false, false, false, false };
    for (int iteration = 0; iteration < 1000; iteration++) {
        int pitch = kiosk_mode_choose_pitch(&kiosk);
        assert(allowed_pitch(pitch));
        if (pitch == -12)
            saw_pitch[0] = true;
        else if (pitch == -7)
            saw_pitch[1] = true;
        else if (pitch == 0)
            saw_pitch[2] = true;
        else if (pitch == 7)
            saw_pitch[3] = true;
        else if (pitch == 12)
            saw_pitch[4] = true;

        int seconds = kiosk_mode_choose_interval_seconds(&kiosk);
        assert(seconds >= KIOSK_MIN_INTERVAL_SECONDS);
        assert(seconds <= KIOSK_MAX_INTERVAL_SECONDS);

        int first = kiosk_mode_choose_sample(&kiosk, 5, 2, true);
        assert(first >= 0 && first < 5);
        int replacement = kiosk_mode_choose_sample(&kiosk, 5, 2, false);
        assert(replacement >= 0 && replacement < 5);
        assert(replacement != 2);
    }
    for (size_t index = 0; index < 5; index++)
        assert(saw_pitch[index]);
    assert(kiosk_mode_choose_sample(&kiosk, 1, 0, false) == 0);
    assert(kiosk_mode_choose_sample(&kiosk, 0, 0, false) == 0);

    kiosk_mode_cancel(&kiosk);
    assert(!kiosk.active);
    puts("kiosk_mode_test: all checks passed");
    return 0;
}
