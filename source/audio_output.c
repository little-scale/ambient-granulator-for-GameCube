#include "audio_output.h"

#include <malloc.h>
#include <ogc/audio.h>
#include <ogc/lwp_watchdog.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_THREAD_STACK_SIZE (32 * 1024)
#define AUDIO_THREAD_PRIORITY 80
#define AUDIO_BUFFER_BYTES \
    (AUDIO_BUFFER_FRAMES * 2 * (int)sizeof(int16_t))

static AudioOutput *callback_output;

static int16_t *buffer_samples(AudioOutput *output, int index)
{
    return output->samples + index * AUDIO_BUFFER_FRAMES * 2;
}

static void publish_status(AudioOutput *output)
{
    __atomic_store_n(&output->active_voices_snapshot,
        (uint32_t)granular_engine_active_voices(&output->engine),
        __ATOMIC_RELEASE);
    __atomic_store_n(&output->grains_launched_snapshot,
        output->engine.grains_launched, __ATOMIC_RELEASE);
    __atomic_store_n(&output->peak_left_snapshot,
        output->meter.peak_left, __ATOMIC_RELEASE);
    __atomic_store_n(&output->peak_right_snapshot,
        output->meter.peak_right, __ATOMIC_RELEASE);
    __atomic_store_n(&output->clipping_snapshot,
        output_meter_clipping(&output->meter), __ATOMIC_RELEASE);
}

static void render_buffer(AudioOutput *output, int index,
                          const AudioRenderConfig *config)
{
    int16_t *samples = buffer_samples(output, index);
    int active_before = granular_engine_active_voices(&output->engine);
    if (config->effects.freeze && config->effects.wet_percent >= 100)
        granular_engine_render_silent_wide(&output->engine,
                                           output->mix_samples,
                                           AUDIO_BUFFER_FRAMES,
                                           &config->granular);
    else
        granular_engine_render_wide(&output->engine, output->mix_samples,
                                    AUDIO_BUFFER_FRAMES,
                                    &config->granular);

    bool any_grain_output = false;
    for (int frame = 0; frame < AUDIO_BUFFER_FRAMES; frame++)
        any_grain_output |= output->mix_samples[frame * 2] != 0
                         || output->mix_samples[frame * 2 + 1] != 0;
    int active_after = granular_engine_active_voices(&output->engine);
    bool intentionally_silent = config->effects.freeze
                             && config->effects.wet_percent >= 100;
    if (!any_grain_output && !intentionally_silent
            && (active_before > 0 || active_after > 0))
        __atomic_add_fetch(&output->silent_blocks, 1, __ATOMIC_RELEASE);

    effects_chain_process_wide(&output->effects, output->mix_samples,
                               samples, AUDIO_BUFFER_FRAMES,
                               &config->effects);
    output_meter_process(&output->meter, samples, AUDIO_BUFFER_FRAMES);
    if (effects_chain_overloaded(&output->effects))
        output_meter_mark_clipped(&output->meter);
    publish_status(output);
    DCFlushRange(samples, AUDIO_BUFFER_BYTES);
}

static void audio_dma_callback(void)
{
    AudioOutput *output = callback_output;
    if (output == NULL || output->quit)
        return;

    int ready = __atomic_exchange_n(&output->ready_index, -1,
                                    __ATOMIC_ACQ_REL);
    if (ready < 0) {
        __atomic_add_fetch(&output->underruns, 1, __ATOMIC_RELEASE);
        return;
    }
    AUDIO_InitDMA((u32)buffer_samples(output, ready), AUDIO_BUFFER_BYTES);
    __atomic_add_fetch(&output->buffers_submitted, 1, __ATOMIC_RELEASE);

    /* Buffer 2 was rendered before DMA started. Preserve it for the second
       callback, then refill one completed buffer per callback. This keeps the
       renderer a full 1024-frame block ahead of the hardware. */
    if (__atomic_exchange_n(&output->priming, false, __ATOMIC_ACQ_REL)) {
        __atomic_store_n(&output->ready_index, 2, __ATOMIC_RELEASE);
        return;
    }

    int freed = output->next_to_free;
    output->next_to_free = (output->next_to_free + 1)
                         % AUDIO_BUFFER_COUNT;
    int previous = __atomic_exchange_n(&output->render_request, freed,
                                       __ATOMIC_ACQ_REL);
    if (previous >= 0) {
        __atomic_store_n(&output->render_request, previous,
                         __ATOMIC_RELEASE);
        __atomic_add_fetch(&output->underruns, 1, __ATOMIC_RELEASE);
        return;
    }
    LWP_SemPost(output->render_semaphore);
}

static void *audio_thread_main(void *data)
{
    AudioOutput *output = data;
    while (!__atomic_load_n(&output->quit, __ATOMIC_ACQUIRE)) {
        LWP_SemWait(output->render_semaphore);
        if (__atomic_load_n(&output->quit, __ATOMIC_ACQUIRE))
            break;
        int index = __atomic_exchange_n(&output->render_request, -1,
                                        __ATOMIC_ACQ_REL);
        if (index < 0)
            continue;

        LWP_MutexLock(output->config_lock);
        AudioRenderConfig config = output->config;
        LWP_MutexUnlock(output->config_lock);
        u64 render_start = gettime();
        LWP_MutexLock(output->state_lock);
        render_buffer(output, index, &config);
        LWP_MutexUnlock(output->state_lock);
        uint64_t elapsed_wide = ticks_to_microsecs(
            gettime() - render_start);
        uint32_t elapsed = elapsed_wide > UINT32_MAX
            ? UINT32_MAX : (uint32_t)elapsed_wide;
        __atomic_store_n(&output->render_time_us_snapshot, elapsed,
                         __ATOMIC_RELEASE);
        uint32_t peak = __atomic_load_n(
            &output->render_time_peak_us_snapshot, __ATOMIC_ACQUIRE);
        while (elapsed > peak && !__atomic_compare_exchange_n(
                &output->render_time_peak_us_snapshot, &peak, elapsed,
                false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        }
        __atomic_store_n(&output->ready_index, index, __ATOMIC_RELEASE);
    }
    return NULL;
}

static void cleanup_partial(AudioOutput *output, bool effects_initialized,
                            bool semaphore_initialized,
                            bool state_mutex_initialized,
                            bool config_mutex_initialized)
{
    if (effects_initialized)
        effects_chain_exit(&output->effects);
    if (semaphore_initialized)
        LWP_SemDestroy(output->render_semaphore);
    if (state_mutex_initialized)
        LWP_MutexDestroy(output->state_lock);
    if (config_mutex_initialized)
        LWP_MutexDestroy(output->config_lock);
    free(output->thread_stack);
    free(output->mix_samples);
    free(output->samples);
    output->thread_stack = NULL;
    output->mix_samples = NULL;
    output->samples = NULL;
}

bool audio_output_init(AudioOutput *output,
                       const AudioRenderConfig *initial_config,
                       const int16_t *sample, uint32_t sample_count,
                       uint32_t sample_rate)
{
    memset(output, 0, sizeof(*output));
    output->thread = LWP_THREAD_NULL;
    output->render_request = -1;
    output->ready_index = -1;
    output->config = *initial_config;
    granular_engine_init(&output->engine);
    granular_engine_set_sample(&output->engine, sample, sample_count,
                               sample_rate);
    output_meter_init(&output->meter);

    bool config_mutex_initialized = false;
    bool state_mutex_initialized = false;
    bool semaphore_initialized = false;
    bool effects_initialized = false;
    if (LWP_MutexInit(&output->config_lock, false) < 0) {
        output->init_result = -1;
        return false;
    }
    config_mutex_initialized = true;
    if (LWP_MutexInit(&output->state_lock, false) < 0) {
        output->init_result = -2;
        cleanup_partial(output, false, false, false, true);
        return false;
    }
    state_mutex_initialized = true;
    if (LWP_SemInit(&output->render_semaphore, 0, 1) < 0) {
        output->init_result = -3;
        cleanup_partial(output, false, false, true, true);
        return false;
    }
    semaphore_initialized = true;
    if (!effects_chain_init(&output->effects)) {
        output->init_result = -4;
        cleanup_partial(output, false, true, true, true);
        return false;
    }
    effects_initialized = true;

    output->samples = memalign(32, AUDIO_BUFFER_COUNT
        * AUDIO_BUFFER_BYTES);
    output->mix_samples = malloc(AUDIO_BUFFER_FRAMES * 2
                               * sizeof(*output->mix_samples));
    output->thread_stack = memalign(32, AUDIO_THREAD_STACK_SIZE);
    if (output->samples == NULL || output->mix_samples == NULL
            || output->thread_stack == NULL) {
        output->init_result = -5;
        cleanup_partial(output, true, true, true, true);
        return false;
    }
    memset(output->samples, 0, AUDIO_BUFFER_COUNT * AUDIO_BUFFER_BYTES);
    for (int index = 0; index < AUDIO_BUFFER_COUNT; index++)
        render_buffer(output, index, initial_config);

    if (LWP_CreateThread(&output->thread, audio_thread_main, output,
            output->thread_stack, AUDIO_THREAD_STACK_SIZE,
            AUDIO_THREAD_PRIORITY) < 0) {
        output->thread = LWP_THREAD_NULL;
        output->init_result = -6;
        cleanup_partial(output, true, true, true, true);
        return false;
    }

    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    callback_output = output;
    output->next_to_free = 0;
    output->ready_index = 1;
    output->priming = true;
    output->buffers_submitted = 1;
    AUDIO_RegisterDMACallback(audio_dma_callback);
    AUDIO_InitDMA((u32)buffer_samples(output, 0), AUDIO_BUFFER_BYTES);
    output->ready = true;
    AUDIO_StartDMA();
    (void)effects_initialized;
    (void)semaphore_initialized;
    (void)state_mutex_initialized;
    (void)config_mutex_initialized;
    return true;
}

void audio_output_update(AudioOutput *output,
                         const AudioRenderConfig *config)
{
    if (!output->ready)
        return;
    LWP_MutexLock(output->config_lock);
    output->config = *config;
    LWP_MutexUnlock(output->config_lock);
}

void audio_output_set_sample(AudioOutput *output, const int16_t *sample,
                             uint32_t sample_count, uint32_t sample_rate)
{
    if (!output->ready)
        return;
    LWP_MutexLock(output->state_lock);
    granular_engine_set_sample(&output->engine, sample, sample_count,
                               sample_rate);
    publish_status(output);
    LWP_MutexUnlock(output->state_lock);
}

void audio_output_stop_grains(AudioOutput *output)
{
    if (!output->ready)
        return;
    LWP_MutexLock(output->state_lock);
    granular_engine_stop(&output->engine);
    publish_status(output);
    LWP_MutexUnlock(output->state_lock);
}

void audio_output_trigger(AudioOutput *output, int center_x,
                          int grain_count)
{
    if (!output->ready)
        return;
    LWP_MutexLock(output->state_lock);
    granular_engine_trigger(&output->engine, center_x, grain_count);
    LWP_MutexUnlock(output->state_lock);
}

void audio_output_trigger_sample(AudioOutput *output, uint32_t center_sample,
                                 int center_x, int grain_count)
{
    if (!output->ready)
        return;
    LWP_MutexLock(output->state_lock);
    granular_engine_trigger_sample(&output->engine, center_sample, center_x,
                                   grain_count);
    LWP_MutexUnlock(output->state_lock);
}

bool audio_output_pop_marker(AudioOutput *output, GranularMarker *marker)
{
    if (!output->ready || LWP_MutexTryLock(output->state_lock) != 0)
        return false;
    bool available = granular_engine_pop_marker(&output->engine, marker);
    LWP_MutexUnlock(output->state_lock);
    return available;
}

int audio_output_active_voices(const AudioOutput *output)
{
    return output->ready ? (int)__atomic_load_n(
        &output->active_voices_snapshot, __ATOMIC_ACQUIRE) : 0;
}

uint32_t audio_output_grains_launched(const AudioOutput *output)
{
    return output->ready ? __atomic_load_n(
        &output->grains_launched_snapshot, __ATOMIC_ACQUIRE) : 0;
}

uint16_t audio_output_peak_left(const AudioOutput *output)
{
    return output->ready ? (uint16_t)__atomic_load_n(
        &output->peak_left_snapshot, __ATOMIC_ACQUIRE) : 0;
}

uint16_t audio_output_peak_right(const AudioOutput *output)
{
    return output->ready ? (uint16_t)__atomic_load_n(
        &output->peak_right_snapshot, __ATOMIC_ACQUIRE) : 0;
}

bool audio_output_clipping(const AudioOutput *output)
{
    return output->ready && __atomic_load_n(
        &output->clipping_snapshot, __ATOMIC_ACQUIRE) != 0;
}

uint32_t audio_output_buffers_submitted(const AudioOutput *output)
{
    return output->ready ? __atomic_load_n(
        &output->buffers_submitted, __ATOMIC_ACQUIRE) : 0;
}

uint32_t audio_output_underruns(const AudioOutput *output)
{
    return output->ready ? __atomic_load_n(
        &output->underruns, __ATOMIC_ACQUIRE) : 0;
}

uint32_t audio_output_silent_blocks(const AudioOutput *output)
{
    return output->ready ? __atomic_load_n(
        &output->silent_blocks, __ATOMIC_ACQUIRE) : 0;
}

uint32_t audio_output_render_time_us(const AudioOutput *output)
{
    return output->ready ? __atomic_load_n(
        &output->render_time_us_snapshot, __ATOMIC_ACQUIRE) : 0;
}

uint32_t audio_output_render_time_peak_us(const AudioOutput *output)
{
    return output->ready ? __atomic_load_n(
        &output->render_time_peak_us_snapshot, __ATOMIC_ACQUIRE) : 0;
}

void audio_output_exit(AudioOutput *output)
{
    if (!output->ready)
        return;
    output->ready = false;
    __atomic_store_n(&output->quit, true, __ATOMIC_RELEASE);
    AUDIO_StopDMA();
    AUDIO_RegisterDMACallback(NULL);
    callback_output = NULL;
    LWP_SemPost(output->render_semaphore);
    LWP_JoinThread(output->thread, NULL);
    output->thread = LWP_THREAD_NULL;
    cleanup_partial(output, true, true, true, true);
}
