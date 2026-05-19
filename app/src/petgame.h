#ifndef XIAOZHI_PETGAME_H
#define XIAOZHI_PETGAME_H

#include <stdbool.h>
#include <stdint.h>

#define PETGAME_DATA_VERSION 4U

typedef struct
{
    uint32_t version;
    uint32_t reading_seconds;
    uint32_t ai_interaction_count;
    uint32_t manual_feed_count;
    uint32_t affection_count;
    uint32_t growth_score;
    uint8_t growth_level;
    uint8_t mood_level;
    uint8_t hunger_level;
    uint8_t energy_level;
    uint8_t cleanliness_level;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t feed_balance;
    uint32_t play_count;
    uint32_t clean_count;
    uint32_t companion_days;
    uint32_t companion_elapsed_seconds;
} petgame_state_t;

typedef enum
{
    PETGAME_ACTIVITY_TYPE_NONE = 0,
    PETGAME_ACTIVITY_TYPE_AI_INTERACTION = 1,
    PETGAME_ACTIVITY_TYPE_FEED_SMALL = 2,
    PETGAME_ACTIVITY_TYPE_FEED_BIG = 3,
    PETGAME_ACTIVITY_TYPE_AFFECTION = 4,
    PETGAME_ACTIVITY_TYPE_PLAY = 5,
    PETGAME_ACTIVITY_TYPE_CLEAN = 6,
} petgame_activity_type_t;

void petgame_init(void);
bool petgame_get_state_copy(petgame_state_t *state);
const char *petgame_get_level_name(void);
const char *petgame_get_mood_text(void);
void petgame_add_reading_seconds(uint32_t seconds);
void petgame_record_ai_interaction(void);
bool petgame_record_feed(uint32_t count);
void petgame_record_affection(uint32_t count);
void petgame_record_play(void);
void petgame_record_clean(void);
void petgame_set_reading_active(bool active);
void petgame_process(void);
uint32_t petgame_get_feed_balance(void);
uint32_t petgame_get_companion_days(void);
uint8_t petgame_drain_activity_events(petgame_activity_type_t *events, uint8_t max_events);

#endif
