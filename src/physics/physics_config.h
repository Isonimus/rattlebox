#ifndef PHYSICS_CONFIG_H
#define PHYSICS_CONFIG_H

#include <stdint.h>

// Every constant the physics runs on, in one place: the fairness test (invariant #2) runs the
// same values the firmware does, so tuning one here reruns that test against it. Units are
// millimetres, seconds and radians, so the contact constants can start from real dice. The
// measurements behind each value are ADR-0009's.

// ---- the step

// Four steps per 60 fps frame (ADR-0007). Results depend on the step, so the host's fairness
// test and the device must use the same one.
constexpr float PHYSICS_STEP_S = 1.0f / 240;

/**
 * @brief The number of steps in @p seconds, rounded to nearest: 1 / PHYSICS_STEP_S is 239.9999 in
 * float, so truncating would lose a step from every whole second.
 */
constexpr uint32_t stepsIn(float seconds) { return static_cast<uint32_t>(seconds / PHYSICS_STEP_S + 0.5f); }

// ---- the world

constexpr float GRAVITY_MM_PER_S2 = 9810.0f;

// Both boards' screen, which the tray is laid out on: the product stops at boot on any other.
constexpr int SCREEN_WIDTH_PX = 240;
constexpr int SCREEN_HEIGHT_PX = 135;
// The results band's strip along the screen's top edge: the doubled 6 x 8 font and a few pixels
// around it. The tray lies beneath it, so no die is ever drawn under the band (ADR-0016).
constexpr int BAND_HEIGHT_PX = 20;

// A D20 about 20 mm across has an edge of about 10.5 mm. Drawn with ADR-0007's 24 px edge,
// that is about 2.29 px/mm, so the screen's 240 px width frames a tray 105 mm wide.
constexpr float D20_EDGE_MM = 10.5f;
constexpr float D6_EDGE_MM = 16.0f;
// The D8 spans the D20's 20 mm: its vertices on the same sphere, so the dice of a set look one size
// and dice collide by the bounding spheres they already had (slice 0026). An icosahedron's
// circumradius is sin(72°) of its edge, an octahedron's 1/√2 of its edge: 14.1 mm.
constexpr float ICOSAHEDRON_CIRCUMRADIUS_PER_EDGE = 0.9510565f;
constexpr float OCTAHEDRON_EDGE_PER_CIRCUMRADIUS = 1.4142136f;
constexpr float D8_EDGE_MM = D20_EDGE_MM * ICOSAHEDRON_CIRCUMRADIUS_PER_EDGE * OCTAHEDRON_EDGE_PER_CIRCUMRADIUS;
// The D10 spans that same 20 mm, and is given its circumradius directly because a kite has two
// edge lengths and so the die has no one edge to size it by (slice 0027).
constexpr float D10_CIRCUMRADIUS_MM = D20_EDGE_MM * ICOSAHEDRON_CIRCUMRADIUS_PER_EDGE;
// The D12 spans that same 20 mm. A dodecahedron's circumradius is √3·φ/2 of its edge, so the
// edge is 7.13 mm (slice 0029).
constexpr float DODECAHEDRON_CIRCUMRADIUS_PER_EDGE = 1.4012585f;
constexpr float D12_EDGE_MM = D20_EDGE_MM * ICOSAHEDRON_CIRCUMRADIUS_PER_EDGE / DODECAHEDRON_CIRCUMRADIUS_PER_EDGE;
constexpr float TRAY_WIDTH_MM = 105.0f;
constexpr float TRAY_PX_PER_MM = SCREEN_WIDTH_PX / TRAY_WIDTH_MM;
// The screen beneath the strip, 50.3 mm: derived, so the tray's far wall stays the strip's edge.
constexpr float TRAY_DEPTH_MM = (SCREEN_HEIGHT_PX - BAND_HEIGHT_PX) / TRAY_PX_PER_MM;
// The glass over the tray. Higher than a D6's 27.7 mm space diagonal with room to spare, so no
// die can wedge between the floor and the glass.
constexpr float TRAY_HEIGHT_MM = 40.0f;

// One die type per roll, so only the dice's mass ratio matters, and it is one.
constexpr float DIE_MASS = 1.0f;
constexpr uint8_t MAX_DICE = 3;

// ---- contacts

// Bounciness and grip of a plastic die on the tray and on another die. Feel values, which the
// roll slice may retune by eye; the fairness test reruns against whatever is set here. The tray's
// restitution was chosen by how much the physics, not the launch, decides a result. Over 2000
// single-die rolls each (slice 0008), a D6 ended on the face it launched with 36.0% of the time at
// 0.35, 27.5% at 0.5 and 21.7% at 0.65, against 16.7% by chance; a D20 10.4%, 6.8% and 9.2%,
// against 5%. 0.65 favours the D6, whose six faces the launch decides most often.
constexpr float TRAY_RESTITUTION = 0.65f;
constexpr float TRAY_FRICTION = 0.35f;
constexpr float DIE_RESTITUTION = 0.5f;
constexpr float DIE_FRICTION = 0.2f;
constexpr float MAX_RESTITUTION = TRAY_RESTITUTION > DIE_RESTITUTION ? TRAY_RESTITUTION : DIE_RESTITUTION;
// Below this approach speed a contact does not bounce. Stepped in time, a contact that leaves at
// v under gravity comes back up to 2 g·dt faster, so bounces die out only if e·v + 2 g·dt < v,
// that is v > 2 g·dt / (1 − e). Below it a die hops in place for good: with a 50 mm/s threshold, a
// landed D20 bounced 0.1 mm at 21 mm/s every other step indefinitely. Half again for margin.
constexpr float BOUNCE_THRESHOLD_MARGIN = 1.5f;
constexpr float BOUNCE_MIN_APPROACH_MM_PER_S =
    BOUNCE_THRESHOLD_MARGIN * 2 * GRAVITY_MM_PER_S2 * PHYSICS_STEP_S / (1 - MAX_RESTITUTION);
// A vertex that can reach a plane this step is a speculative contact: it may close the gap but
// not pass it. Its reach is its own approach speed times the step, plus this margin, which keeps
// a resting contact a contact and covers a vertex the solver turns toward a plane mid-step.
constexpr float SPECULATIVE_MARGIN_MM = 1.0f;
// Penetration left in place by the position correction, so a resting contact stays a contact.
constexpr float PENETRATION_SLOP_MM = 0.01f;
constexpr uint8_t SOLVER_ITERATIONS = 10;

// ---- rest and the lock

// Measured by the fairness probe's trace (slice 0008): 2000 rolls of each die, left alone for 6 s.
// A die at rest moved at most 0.00016 mm/s and 0.000051 rad/s, far below either threshold here.
//
// A threshold has two sides, and slice 0008 measured only the lower one. The angular speed was
// 0.5 rad/s until the D12, which creeps over an edge slowly enough to stay under it while still
// turning: its longest "pause" read 0.1583 s at 0.5 rad/s and 0.0000 s at 0.1 rad/s, so the hold
// had been waiting out a rotation rather than a balance, on every die (ADR-0031). At 0.1 rad/s,
// still 2,000 times the resting floor, the longest pause after which a die then tipped onto
// another face is 0.0000 s (D6), 0.0000 s (D8), 0.0167 s (D10), 0.0083 s (D12) and 0.0083 s
// (D20). The hold must outlast that pause four times over, and scripts/fairness-probe.mjs fails
// a die whose traced pause it does not.
constexpr float REST_LINEAR_MM_PER_S = 5.0f;
constexpr float REST_ANGULAR_RAD_PER_S = 0.1f;
// How long a die must stay below those two speeds is not here, and is not one number: those
// pauses differ across the dice, so the hold is measured per die and carried on its body as
// DieShape::holdS (ADR-0028). One hold made every die wait out the worst of them.
// At rest, the up face's normal was at most 0.229° from straight up on dice lying flat and at
// least 1.085° on dice propped on a wall: 0.5° sits in the gap.
constexpr float RAD_PER_DEG = 3.14159265f / 180;
constexpr float COCKED_TILT_RAD = 0.5f * RAD_PER_DEG;

// ---- the launch

// The dice leave the shake at the device's own speed. Integrated from the IMU probe's captures
// (ADR-0005, both boards), a gentle shake peaked at 755 to 1031 mm/s and a hard one at 2122 to
// 3557 mm/s, along all three axes, so a throw is 750 to 3500 mm/s in any direction, glass
// included. How a given shake's energy picks a speed in this range is the roll slice's
// (ADR-0002). The spin is modest: walls and glass spin the dice up within the first contacts.
constexpr float THROW_MIN_MM_PER_S = 750.0f;
constexpr float THROW_MAX_MM_PER_S = 3500.0f;
constexpr float SPIN_MIN_RAD_PER_S = 10.0f;
constexpr float SPIN_MAX_RAD_PER_S = 30.0f;
// A cocked die's settle nudge: a hop of about 8 mm. Over 500 rolls of three D20s (slice 0008), a
// 400 mm/s nudge fired 0.18 times per die and at most 5 times in a roll, the longest roll taking
// 1.7 s; at 200 mm/s it took up to 12 tries and at 100 up to 33. A spin of 2 instead of 5 rad/s
// changed little.
constexpr float NUDGE_MM_PER_S = 400.0f;
constexpr float NUDGE_SPIN_RAD_PER_S = 5.0f;
// Dice start this far above where their lowest possible vertex would touch the floor.
constexpr float DROP_HEIGHT_MM = 5.0f;

#endif // PHYSICS_CONFIG_H
