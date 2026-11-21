#ifndef HIT_FINDER_H
#define HIT_FINDER_H

#include <stdint.h>
#include "../physics/world.h"

/** @brief Each die against the tray, and each pair of dice: the most hits one step can make. */
constexpr uint8_t MAX_HITS_PER_STEP = MAX_DICE + MAX_DICE * (MAX_DICE - 1) / 2;

/**
 * @brief The hits of each physics step, read from the impulses the solver kept for warm starting
 * (slice 0013). A hit is a contact that pushed this step and did not the step before; a contact
 * that goes on pushing, as under a resting die, is not hit again.
 */
class HitFinder {
public:
    /**
     * @brief The hits @p world's last step made, into @p impulses: one per die for its new contacts
     * with the tray, summed, so a face landing on its vertices together is one hit, and one per pair
     * of dice that met. Each is the normal impulse in mass·mm/s. Returns how many.
     */
    uint8_t find(const World& world, float impulses[MAX_HITS_PER_STEP]);

private:
    float _trayNormal[MAX_DICE][DIE_MAX_VERTICES][TRAY_PLANE_COUNT] = {};
    float _diceNormal[MAX_DICE][MAX_DICE] = {};
};

#endif // HIT_FINDER_H
