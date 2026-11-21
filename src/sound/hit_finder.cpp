#include "hit_finder.h"

// The whole memory, not just the dice and vertices in play: the step clears every entry it did not
// use, so this record of the last step stays right when a roll changes the die type or count.
uint8_t HitFinder::find(const World& world, float impulses[MAX_HITS_PER_STEP]) {
    uint8_t count = 0;
    for (uint8_t die = 0; die < MAX_DICE; die++) {
        float landed = 0;
        for (uint8_t vertex = 0; vertex < DIE_MAX_VERTICES; vertex++) {
            for (uint8_t plane = 0; plane < TRAY_PLANE_COUNT; plane++) {
                const float now = world.memory.tray[die][vertex][plane].normal;
                if (_trayNormal[die][vertex][plane] == 0) landed += now;
                _trayNormal[die][vertex][plane] = now;
            }
        }
        if (landed > 0) impulses[count++] = landed;
    }
    for (uint8_t a = 0; a < MAX_DICE; a++) {
        for (uint8_t b = a + 1; b < MAX_DICE; b++) {
            const float now = world.memory.dice[a][b].normal;
            if (_diceNormal[a][b] == 0 && now > 0) impulses[count++] = now;
            _diceNormal[a][b] = now;
        }
    }
    return count;
}
