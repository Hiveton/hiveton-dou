#include "petgame.h"
#include "petgame_storage.h"

#include <limits.h>

#include "rtthread.h"

#define PETGAME_AUTOSAVE_MS 30000U
#define PETGAME_LEVEL_COUNT (sizeof(s_level_names) / sizeof(s_level_names[0]))
#define PETGAME_MOOD_COUNT (sizeof(s_mood_names) / sizeof(s_mood_names[0]))
#define PETGAME_MAX_STATUS 100U
#define PETGAME_GROWTH_PER_SECOND 60U
#define PETGAME_GROWTH_PER_AI 4U
#define PETGAME_GROWTH_PER_FEED 3U
#define PETGAME_GROWTH_PER_AFFECTION 2U
#define PETGAME_FEED_GAIN_PER_READING_SECOND 60U
#define PETGAME_FEED_GAIN_PER_AI_INTERACTION 1U
#define PETGAME_FEED_BALANCE_INITIAL 3U
#define PETGAME_FEED_COST_SMALL 1U
#define PETGAME_FEED_COST_BIG 3U

#define PETGAME_HUNGER_DECAY_SECONDS 1800U
#define PETGAME_ENERGY_DECAY_SECONDS 1200U
#define PETGAME_MOOD_DECAY_SECONDS 900U
#define PETGAME_ENERGY_RECOVER_READING_SECONDS 1800U
#define PETGAME_MOOD_RECOVER_READING_SECONDS 720U

#define PETGAME_ACTIVITY_EVENT_QUEUE_CAPACITY 8U

typedef enum
{
    PETGAME_READY = 0,
    PETGAME_NOT_READY = 1,
} petgame_init_state_t;

static const char *const s_level_names[] = {
    "萌芽",
    "小萌",
    "伙伴",
    "贴心",
    "成长",
    "大师",
    "传说"
};

static const uint32_t s_level_thresholds[] = {
    0U,
    25U,
    80U,
    180U,
    360U,
    640U,
    1000U,
};

static const char *const s_mood_names[] = {
    "有点疲惫",
    "普通",
    "开心",
    "很开心",
    "超快乐",
};

static const petgame_state_t s_petgame_default_state = {
    .version = PETGAME_DATA_VERSION,
    .reading_seconds = 0U,
    .ai_interaction_count = 0U,
    .manual_feed_count = 0U,
    .affection_count = 0U,
    .growth_score = 0U,
    .growth_level = 0U,
    .mood_level = 82U,
    .hunger_level = 100U,
    .energy_level = 100U,
    .feed_balance = PETGAME_FEED_BALANCE_INITIAL,
};

static petgame_state_t s_petgame_state = {
    .version = PETGAME_DATA_VERSION,
    .reading_seconds = 0U,
    .ai_interaction_count = 0U,
    .manual_feed_count = 0U,
    .affection_count = 0U,
    .growth_score = 0U,
    .growth_level = 0U,
    .mood_level = 82U,
    .hunger_level = 100U,
    .energy_level = 100U,
    .feed_balance = PETGAME_FEED_BALANCE_INITIAL,
};
static bool s_petgame_dirty = false;
static bool s_petgame_inited = false;
static bool s_petgame_reading_mode = false;
static rt_tick_t s_petgame_last_autosave = 0U;
static rt_tick_t s_petgame_last_process_tick = 0U;
static petgame_init_state_t s_init_state = PETGAME_NOT_READY;
static uint32_t s_petgame_hunger_decay_seconds = 0U;
static uint32_t s_petgame_energy_decay_seconds = 0U;
static uint32_t s_petgame_mood_decay_seconds = 0U;
static petgame_activity_type_t s_activity_events[PETGAME_ACTIVITY_EVENT_QUEUE_CAPACITY];
static uint8_t s_activity_event_head = 0U;
static uint8_t s_activity_event_tail = 0U;
static uint8_t s_activity_event_count = 0U;
static rt_mutex_t s_activity_event_mutex = RT_NULL;

static void petgame_activity_event_push(petgame_activity_type_t event)
{
    if (!s_petgame_inited || event == PETGAME_ACTIVITY_TYPE_NONE)
    {
        return;
    }

    if (s_activity_event_mutex != RT_NULL)
    {
        rt_mutex_take(s_activity_event_mutex, RT_WAITING_FOREVER);
    }

    s_activity_events[s_activity_event_tail] = event;
    if (s_activity_event_count < PETGAME_ACTIVITY_EVENT_QUEUE_CAPACITY)
    {
        ++s_activity_event_count;
    }
    else
    {
        s_activity_event_head = (uint8_t)((s_activity_event_head + 1U) % PETGAME_ACTIVITY_EVENT_QUEUE_CAPACITY);
    }

    s_activity_event_tail = (uint8_t)((s_activity_event_tail + 1U) % PETGAME_ACTIVITY_EVENT_QUEUE_CAPACITY);

    if (s_activity_event_mutex != RT_NULL)
    {
        rt_mutex_release(s_activity_event_mutex);
    }
}

uint8_t petgame_drain_activity_events(petgame_activity_type_t *events, uint8_t max_events)
{
    uint8_t count;
    uint8_t i;

    if ((events == NULL) || (max_events == 0U))
    {
        return 0U;
    }

    if (!s_petgame_inited)
    {
        return 0U;
    }

    if (s_activity_event_mutex != RT_NULL)
    {
        rt_mutex_take(s_activity_event_mutex, RT_WAITING_FOREVER);
    }

    count = s_activity_event_count;
    if (count > max_events)
    {
        count = max_events;
    }

    for (i = 0U; i < count; ++i)
    {
        events[i] = s_activity_events[s_activity_event_head];
        s_activity_event_head = (uint8_t)((s_activity_event_head + 1U) % PETGAME_ACTIVITY_EVENT_QUEUE_CAPACITY);
    }

    s_activity_event_count = (s_activity_event_count >= count) ? (s_activity_event_count - count) : 0U;

    if (s_activity_event_mutex != RT_NULL)
    {
        rt_mutex_release(s_activity_event_mutex);
    }

    return count;
}

static void petgame_activity_event_init_mutex(void)
{
    if (s_activity_event_mutex != RT_NULL)
    {
        return;
    }

    s_activity_event_mutex = rt_mutex_create("petgame_event", RT_IPC_FLAG_FIFO);
}

static bool petgame_change_u8(uint8_t *target, int32_t delta)
{
    int32_t value;

    if (target == NULL)
    {
        return false;
    }

    value = (int32_t)(*target) + delta;
    if (value < 0)
    {
        value = 0;
    }
    else if (value > (int32_t)PETGAME_MAX_STATUS)
    {
        value = (int32_t)PETGAME_MAX_STATUS;
    }

    if ((uint8_t)value != *target)
    {
        *target = (uint8_t)value;
        return true;
    }

    return false;
}

static void petgame_mark_dirty(void)
{
    s_petgame_dirty = true;
}

static void petgame_saturating_inc_u32(uint32_t *value)
{
    if (value == NULL)
    {
        return;
    }

    if (*value != UINT32_MAX)
    {
        ++(*value);
    }
}

static void petgame_saturating_add_u32(uint32_t *target, uint32_t delta)
{
    if (target == NULL)
    {
        return;
    }

    if ((UINT32_MAX - *target) < delta)
    {
        *target = UINT32_MAX;
    }
    else
    {
        *target += delta;
    }
}

static void petgame_saturating_sub_u32(uint32_t *target, uint32_t delta)
{
    if (target == NULL)
    {
        return;
    }

    if (*target < delta)
    {
        *target = 0U;
        return;
    }

    *target -= delta;
}

static uint32_t petgame_next_level_threshold(uint32_t level)
{
    uint32_t next_index = level + 1U;

    if (next_index >= PETGAME_LEVEL_COUNT)
    {
        return s_level_thresholds[PETGAME_LEVEL_COUNT - 1U];
    }

    return s_level_thresholds[next_index];
}

static void petgame_recompute_level(petgame_state_t *state)
{
    uint32_t level;

    if (state == NULL)
    {
        return;
    }

    if (state->growth_score < s_level_thresholds[1U])
    {
        state->growth_level = 0U;
        return;
    }

    for (level = 0U; level < PETGAME_LEVEL_COUNT; ++level)
    {
        if (state->growth_score < petgame_next_level_threshold(level))
        {
            state->growth_level = (uint8_t)level;
            return;
        }
    }

    state->growth_level = (uint8_t)(PETGAME_LEVEL_COUNT - 1U);
}

static void petgame_recompute_from_state(petgame_state_t *state)
{
    if (state == NULL)
    {
        return;
    }

    if (state->mood_level > PETGAME_MAX_STATUS)
    {
        state->mood_level = PETGAME_MAX_STATUS;
    }

    if (state->hunger_level > PETGAME_MAX_STATUS)
    {
        state->hunger_level = PETGAME_MAX_STATUS;
    }

    if (state->energy_level > PETGAME_MAX_STATUS)
    {
        state->energy_level = PETGAME_MAX_STATUS;
    }

    petgame_recompute_level(state);
}

static uint8_t petgame_mood_bucket(void)
{
    uint32_t index = (uint32_t)((uint32_t)s_petgame_state.mood_level / 20U);

    if (index >= (uint32_t)(PETGAME_MOOD_COUNT))
    {
        return PETGAME_MOOD_COUNT - 1U;
    }

    return (uint8_t)index;
}

static void petgame_add_growth(uint32_t points)
{
    if (points == 0U)
    {
        return;
    }

    petgame_saturating_add_u32(&s_petgame_state.growth_score, points);
    petgame_recompute_level(&s_petgame_state);
    petgame_mark_dirty();
}

static void petgame_apply_growth_decay_or_recover(uint32_t elapsed_seconds)
{
    if (!s_petgame_inited || (elapsed_seconds == 0U))
    {
        return;
    }

    if (s_petgame_reading_mode)
    {
        s_petgame_energy_decay_seconds += elapsed_seconds;
        while (s_petgame_energy_decay_seconds >= PETGAME_ENERGY_RECOVER_READING_SECONDS)
        {
            s_petgame_energy_decay_seconds -= PETGAME_ENERGY_RECOVER_READING_SECONDS;
            if (petgame_change_u8(&s_petgame_state.energy_level, 1))
            {
                petgame_mark_dirty();
            }
        }

        s_petgame_mood_decay_seconds += elapsed_seconds;
        while (s_petgame_mood_decay_seconds >= PETGAME_MOOD_RECOVER_READING_SECONDS)
        {
            s_petgame_mood_decay_seconds -= PETGAME_MOOD_RECOVER_READING_SECONDS;
            if (petgame_change_u8(&s_petgame_state.mood_level, 1))
            {
                petgame_mark_dirty();
            }
        }
        return;
    }

    s_petgame_hunger_decay_seconds += elapsed_seconds;
    while (s_petgame_hunger_decay_seconds >= PETGAME_HUNGER_DECAY_SECONDS)
    {
        s_petgame_hunger_decay_seconds -= PETGAME_HUNGER_DECAY_SECONDS;
        if (petgame_change_u8(&s_petgame_state.hunger_level, -1))
        {
            petgame_mark_dirty();
        }
    }

    s_petgame_energy_decay_seconds += elapsed_seconds;
    while (s_petgame_energy_decay_seconds >= PETGAME_ENERGY_DECAY_SECONDS)
    {
        s_petgame_energy_decay_seconds -= PETGAME_ENERGY_DECAY_SECONDS;
        if (petgame_change_u8(&s_petgame_state.energy_level, -1))
        {
            petgame_mark_dirty();
        }
    }

    s_petgame_mood_decay_seconds += elapsed_seconds;
    while (s_petgame_mood_decay_seconds >= PETGAME_MOOD_DECAY_SECONDS)
    {
        s_petgame_mood_decay_seconds -= PETGAME_MOOD_DECAY_SECONDS;
        if (petgame_change_u8(&s_petgame_state.mood_level, -1))
        {
            petgame_mark_dirty();
        }
    }
}

const char *petgame_get_level_name(void)
{
    if (s_petgame_state.growth_level >= PETGAME_LEVEL_COUNT)
    {
        return s_level_names[PETGAME_LEVEL_COUNT - 1U];
    }

    return s_level_names[s_petgame_state.growth_level];
}

const char *petgame_get_mood_text(void)
{
    if (s_petgame_state.mood_level > PETGAME_MAX_STATUS)
    {
        return s_mood_names[0U];
    }

    return s_mood_names[petgame_mood_bucket()];
}

const petgame_state_t *petgame_get_state(void)
{
    return &s_petgame_state;
}

uint32_t petgame_get_feed_balance(void)
{
    if (!s_petgame_inited)
    {
        return 0U;
    }

    return s_petgame_state.feed_balance;
}

void petgame_set_reading_active(bool active)
{
    s_petgame_reading_mode = active;
}

static void petgame_apply_reading_gain(uint32_t reading_seconds)
{
    uint32_t growth_gain;

    if (reading_seconds == 0U)
    {
        return;
    }

    growth_gain = reading_seconds / PETGAME_GROWTH_PER_SECOND;
    if (growth_gain > 0U)
    {
        petgame_add_growth(growth_gain);
    }
}

void petgame_init(void)
{
    petgame_state_t loaded_state = s_petgame_default_state;
    bool load_ok;

    if (s_init_state == PETGAME_READY)
    {
        return;
    }

    load_ok = petgame_storage_load(&loaded_state);
    if (!load_ok)
    {
        loaded_state = s_petgame_default_state;
    }

    if (loaded_state.version < PETGAME_DATA_VERSION)
    {
        loaded_state.version = PETGAME_DATA_VERSION;
        if (loaded_state.mood_level == 0U &&
            loaded_state.hunger_level == 0U &&
            loaded_state.energy_level == 0U)
        {
            loaded_state.mood_level = s_petgame_default_state.mood_level;
            loaded_state.hunger_level = s_petgame_default_state.hunger_level;
            loaded_state.energy_level = s_petgame_default_state.energy_level;
        }

        if (loaded_state.growth_score == 0U)
        {
            loaded_state.growth_score =
                (loaded_state.reading_seconds / PETGAME_GROWTH_PER_SECOND) +
                (loaded_state.ai_interaction_count * PETGAME_GROWTH_PER_AI) +
                (loaded_state.manual_feed_count * PETGAME_GROWTH_PER_FEED) +
                (loaded_state.affection_count * PETGAME_GROWTH_PER_AFFECTION);
        }

        if (loaded_state.feed_balance == 0U)
        {
            loaded_state.feed_balance = PETGAME_FEED_BALANCE_INITIAL +
                                        (loaded_state.reading_seconds / PETGAME_FEED_GAIN_PER_READING_SECOND) +
                                        (loaded_state.ai_interaction_count * PETGAME_FEED_GAIN_PER_AI_INTERACTION);
        }
    }
    else if (loaded_state.version > PETGAME_DATA_VERSION)
    {
        loaded_state = s_petgame_default_state;
    }

    if ((loaded_state.mood_level == 0U) ||
        (loaded_state.hunger_level == 0U) ||
        (loaded_state.energy_level == 0U))
    {
        if (loaded_state.version <= PETGAME_DATA_VERSION)
        {
            if (loaded_state.mood_level == 0U)
            {
                loaded_state.mood_level = s_petgame_default_state.mood_level;
            }
            if (loaded_state.hunger_level == 0U)
            {
                loaded_state.hunger_level = s_petgame_default_state.hunger_level;
            }
            if (loaded_state.energy_level == 0U)
            {
                loaded_state.energy_level = s_petgame_default_state.energy_level;
            }
            petgame_mark_dirty();
        }
    }

    s_petgame_state = loaded_state;
    if (s_petgame_state.version != PETGAME_DATA_VERSION)
    {
        s_petgame_state.version = PETGAME_DATA_VERSION;
    }

    petgame_recompute_from_state(&s_petgame_state);
    petgame_activity_event_init_mutex();
    s_petgame_inited = true;
    s_init_state = PETGAME_READY;
    s_petgame_last_autosave = rt_tick_get();
    s_petgame_last_process_tick = s_petgame_last_autosave;
}

void petgame_add_reading_seconds(uint32_t seconds)
{
    if (!s_petgame_inited)
    {
        petgame_init();
    }

    if (seconds == 0U)
    {
        return;
    }

    petgame_saturating_add_u32(&s_petgame_state.reading_seconds, seconds);
    petgame_saturating_add_u32(&s_petgame_state.feed_balance, seconds / PETGAME_FEED_GAIN_PER_READING_SECOND);
    petgame_apply_reading_gain(seconds);
    petgame_mark_dirty();
}

void petgame_record_ai_interaction(void)
{
    if (!s_petgame_inited)
    {
        petgame_init();
    }

    petgame_saturating_inc_u32(&s_petgame_state.ai_interaction_count);
    petgame_saturating_add_u32(&s_petgame_state.feed_balance, PETGAME_FEED_GAIN_PER_AI_INTERACTION);

    petgame_add_growth(PETGAME_GROWTH_PER_AI);
    if (petgame_change_u8(&s_petgame_state.mood_level, 6))
    {
        petgame_mark_dirty();
    }
    if (petgame_change_u8(&s_petgame_state.energy_level, 1))
    {
        petgame_mark_dirty();
    }
    petgame_activity_event_push(PETGAME_ACTIVITY_TYPE_AI_INTERACTION);
}

bool petgame_record_feed(uint32_t count)
{
    uint32_t i;
    uint32_t cost;

    if (!s_petgame_inited)
    {
        petgame_init();
    }

    if (count == 0U)
    {
        return false;
    }

    cost = (count <= 1U) ? PETGAME_FEED_COST_SMALL : PETGAME_FEED_COST_BIG;
    if (s_petgame_state.hunger_level >= PETGAME_MAX_STATUS)
    {
        return false;
    }

    if (s_petgame_state.feed_balance < cost)
    {
        return false;
    }

    petgame_saturating_sub_u32(&s_petgame_state.feed_balance, cost);

    for (i = 0U; i < count; ++i)
    {
        if ((UINT32_MAX - s_petgame_state.manual_feed_count) == 0U)
        {
            s_petgame_state.manual_feed_count = UINT32_MAX;
            break;
        }
        ++s_petgame_state.manual_feed_count;
        petgame_saturating_add_u32(&s_petgame_state.growth_score, PETGAME_GROWTH_PER_FEED);
    }

    petgame_activity_event_push((count <= 1U)
                                   ? PETGAME_ACTIVITY_TYPE_FEED_SMALL
                                   : PETGAME_ACTIVITY_TYPE_FEED_BIG);

    if (petgame_change_u8(&s_petgame_state.hunger_level, (int32_t)(count * 8U)))
    {
        petgame_mark_dirty();
    }
    if (petgame_change_u8(&s_petgame_state.mood_level, (int32_t)(count * 4U)))
    {
        petgame_mark_dirty();
    }
    if (petgame_change_u8(&s_petgame_state.energy_level, (int32_t)(count * 2U)))
    {
        petgame_mark_dirty();
    }

    petgame_recompute_level(&s_petgame_state);
    petgame_mark_dirty();
    return true;
}

void petgame_record_affection(uint32_t count)
{
    uint32_t i;

    if (!s_petgame_inited)
    {
        petgame_init();
    }

    if (count == 0U)
    {
        return;
    }

    for (i = 0U; i < count; ++i)
    {
        if ((UINT32_MAX - s_petgame_state.affection_count) == 0U)
        {
            s_petgame_state.affection_count = UINT32_MAX;
            break;
        }
        ++s_petgame_state.affection_count;
        petgame_saturating_add_u32(&s_petgame_state.growth_score, PETGAME_GROWTH_PER_AFFECTION);
    }

    petgame_activity_event_push(PETGAME_ACTIVITY_TYPE_AFFECTION);

    if (petgame_change_u8(&s_petgame_state.mood_level, (int32_t)(count * 6U)))
    {
        petgame_mark_dirty();
    }
    if (petgame_change_u8(&s_petgame_state.energy_level, (int32_t)(count * 1U)))
    {
        petgame_mark_dirty();
    }

    petgame_recompute_level(&s_petgame_state);
    petgame_mark_dirty();
}

void petgame_process(void)
{
    rt_tick_t now;
    rt_tick_t one_second;
    rt_tick_t delta;
    uint32_t elapsed_seconds;

    if (!s_petgame_inited)
    {
        return;
    }

    now = rt_tick_get();
    one_second = rt_tick_from_millisecond(1000U);
    if (one_second == 0U)
    {
        one_second = 1U;
    }

    if (s_petgame_last_process_tick == 0U)
    {
        s_petgame_last_process_tick = now;
    }

    delta = now - s_petgame_last_process_tick;
    if (delta >= one_second)
    {
        elapsed_seconds = (uint32_t)(delta / one_second);
        s_petgame_last_process_tick += elapsed_seconds * one_second;
        petgame_apply_growth_decay_or_recover(elapsed_seconds);
        petgame_recompute_from_state(&s_petgame_state);
    }

    if (!s_petgame_dirty)
    {
        return;
    }

    if ((now - s_petgame_last_autosave) < rt_tick_from_millisecond(PETGAME_AUTOSAVE_MS))
    {
        return;
    }

    if (petgame_storage_save(&s_petgame_state))
    {
        s_petgame_dirty = false;
        s_petgame_last_autosave = now;
    }
}
