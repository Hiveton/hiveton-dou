#ifndef XIAOZHI_PETGAME_STORAGE_H
#define XIAOZHI_PETGAME_STORAGE_H

#include "petgame.h"

bool petgame_storage_load(petgame_state_t *state);
bool petgame_storage_save(const petgame_state_t *state);

#endif
