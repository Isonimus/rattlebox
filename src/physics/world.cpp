#include "world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr int8_t NO_BODY = -1;
constexpr float INVERSE_DIE_MASS = 1.0f / DIE_MASS;

// A die can touch at most three of the tray's planes at once: the floor or the glass, one end
// wall and one side wall, since the tray is wider, deeper and higher than any die. Fewer
// vertices than all of them meet a plane at rest, but a fast die can pass a plane by a step's
// travel, so only "every vertex" is a bound that cannot be broken. About 7.5 KB of static RAM.
constexpr uint8_t MAX_PLANES_TOUCHED = 3;
constexpr uint8_t MAX_TRAY_CONTACTS_PER_DIE = MAX_PLANES_TOUCHED * DIE_MAX_VERTICES;
constexpr uint8_t MAX_DIE_PAIRS = MAX_DICE * (MAX_DICE - 1) / 2;
constexpr uint8_t MAX_CONTACTS = MAX_DICE * MAX_TRAY_CONTACTS_PER_DIE + MAX_DIE_PAIRS;

/** @brief A plane of the tray: points inside satisfy dot(inward, p) >= offset. */
struct Plane {
    Vec3  inward;
    float offset;
};

/** @brief One point where body A touches body B, or the tray when B is NO_BODY. */
struct Contact {
    int8_t bodyA;
    int8_t bodyB;
    /** @brief Unit normal pointing from B into A: the direction A is pushed. */
    Vec3   normal;
    Vec3   armA;
    Vec3   armB;
    float  normalMass;
    /** @brief The lowest normal speed the solver allows: the bounce, closing the gap, or zero. */
    float  targetNormalSpeed;
    float  friction;
    float  normalImpulse;
    Vec3   tangentImpulse;
    /** @brief Where this contact's impulse is kept for the next step. */
    ContactImpulse* memory;
};

// File scope rather than the stack: the ESP32's loop task has 8 KB of stack.
Contact contacts[MAX_CONTACTS];
uint8_t contactCount;

void trayPlanes(const Tray& tray, Plane planes[TRAY_PLANE_COUNT]) {
    planes[0] = {{0, 0, 1}, 0};
    planes[1] = {{0, 0, -1}, -tray.heightMm};
    planes[2] = {{1, 0, 0}, 0};
    planes[3] = {{-1, 0, 0}, -tray.widthMm};
    planes[4] = {{0, 1, 0}, 0};
    planes[5] = {{0, -1, 0}, -tray.depthMm};
}

float inverseInertia(const DieShape& shape) { return 1.0f / (DIE_MASS * shape.inertiaPerMass); }

/** @brief Velocity of the point at @p arm from @p body's centre, or zero for the tray. */
Vec3 pointVelocity(const World& world, int8_t body, Vec3 arm) {
    if (body == NO_BODY) return {0, 0, 0};
    const DieBody& die = world.dice[body];
    return die.velocity + cross(die.angularVelocity, arm);
}

Vec3 relativeVelocity(const World& world, const Contact& contact) {
    return pointVelocity(world, contact.bodyA, contact.armA) - pointVelocity(world, contact.bodyB, contact.armB);
}

/** @brief How much impulse along @p direction it takes to change the contact's relative speed along it by one. */
float effectiveMass(const Contact& contact, Vec3 direction, float invInertia) {
    float inverse = 0;
    if (contact.bodyA != NO_BODY) {
        const Vec3 turn = cross(contact.armA, direction);
        inverse += INVERSE_DIE_MASS + invInertia * dot(turn, turn);
    }
    if (contact.bodyB != NO_BODY) {
        const Vec3 turn = cross(contact.armB, direction);
        inverse += INVERSE_DIE_MASS + invInertia * dot(turn, turn);
    }
    return 1.0f / inverse;
}

void applyImpulse(World& world, const Contact& contact, Vec3 impulse, float invInertia) {
    if (contact.bodyA != NO_BODY) {
        DieBody& a = world.dice[contact.bodyA];
        a.velocity = a.velocity + impulse * INVERSE_DIE_MASS;
        a.angularVelocity = a.angularVelocity + cross(contact.armA, impulse) * invInertia;
    }
    if (contact.bodyB != NO_BODY) {
        DieBody& b = world.dice[contact.bodyB];
        b.velocity = b.velocity - impulse * INVERSE_DIE_MASS;
        b.angularVelocity = b.angularVelocity - cross(contact.armB, impulse) * invInertia;
    }
}

/**
 * @brief The normal speed a contact may not go below.
 *
 * A contact still apart by @p separationMm is speculative: it may close its gap this step but
 * not pass it, so a vertex arrives on the surface instead of through it. Pushing a vertex back
 * out after it passed through would lift the die without slowing it, which kept a landed D20
 * rocking at about 30 mm/s for good. A contact that will touch this step and is fast enough
 * bounces; the bounce starts up to one step's travel early (ADR-0009).
 */
float targetNormalSpeed(float approach, float separationMm, float restitution) {
    const float closingSpeed = fmaxf(0.0f, separationMm) / PHYSICS_STEP_S;
    const bool touchesThisStep = -approach > closingSpeed;
    if (touchesThisStep && approach < -BOUNCE_MIN_APPROACH_MM_PER_S) return -restitution * approach;
    return -closingSpeed;
}

/** @brief How far apart a contact closing at @p normalSpeed (negative when closing) can be and still touch this step. */
float speculativeReach(float normalSpeed) { return fmaxf(0.0f, -normalSpeed) * PHYSICS_STEP_S + SPECULATIVE_MARGIN_MM; }

void addContact(World& world, Contact contact, float separationMm, float restitution, float friction,
                float invInertia) {
    // The bound above rests on the tray being larger than any die; if that ever breaks, stop
    // rather than drop a contact and let a die sink through a wall.
    if (contactCount >= MAX_CONTACTS) abort();
    contact.normalMass = effectiveMass(contact, contact.normal, invInertia);
    // The bounce is judged on the approach before any impulse, warm or not, is applied.
    const float approach = dot(relativeVelocity(world, contact), contact.normal);
    contact.targetNormalSpeed = targetNormalSpeed(approach, separationMm, restitution);
    contact.friction = friction;
    contact.normalImpulse = contact.memory->normal;
    // A die pair's normal turns between steps, so last step's friction is kept only in the new
    // tangent plane.
    const Vec3 remembered = contact.memory->tangent;
    contact.tangentImpulse = remembered - contact.normal * dot(remembered, contact.normal);
    contacts[contactCount++] = contact;
}

void warmStart(World& world, float invInertia) {
    for (uint8_t i = 0; i < contactCount; i++) {
        const Contact& contact = contacts[i];
        applyImpulse(world, contact, contact.normal * contact.normalImpulse + contact.tangentImpulse, invInertia);
    }
}

/** @brief Keep this step's impulses for the next; a contact that did not recur this step starts from zero. */
void rememberImpulses(World& world) {
    memset(&world.memory, 0, sizeof(world.memory));
    for (uint8_t i = 0; i < contactCount; i++) *contacts[i].memory = {contacts[i].normalImpulse, contacts[i].tangentImpulse};
}

void findTrayContacts(World& world, const Plane planes[TRAY_PLANE_COUNT], float invInertia) {
    const DieShape& shape = *world.shape;
    for (uint8_t die = 0; die < world.dieCount; die++) {
        const DieBody& body = world.dice[die];
        for (uint8_t vertex = 0; vertex < shape.vertexCount; vertex++) {
            const Vec3 arm = rotate(body.orientation, shape.vertices[vertex]);
            const Vec3 point = body.position + arm;
            const Vec3 pointVelocity = body.velocity + cross(body.angularVelocity, arm);
            for (uint8_t i = 0; i < TRAY_PLANE_COUNT; i++) {
                const float separation = dot(planes[i].inward, point) - planes[i].offset;
                if (separation > speculativeReach(dot(pointVelocity, planes[i].inward))) continue;
                ContactImpulse* memory = &world.memory.tray[die][vertex][i];
                addContact(world, {static_cast<int8_t>(die), NO_BODY, planes[i].inward, arm, {}, 0, 0, 0, 0, {}, memory},
                           separation, TRAY_RESTITUTION, TRAY_FRICTION, invInertia);
            }
        }
    }
}

// Two dice's centres are never at the same point, so the normal below never divides by zero: they
// start in separate strips of the tray, and the correction parts any overlap every step.
void findDieContacts(World& world, float invInertia) {
    const float touching = 2 * world.shape->circumradius;
    for (uint8_t a = 0; a < world.dieCount; a++) {
        for (uint8_t b = a + 1; b < world.dieCount; b++) {
            const Vec3 apart = world.dice[a].position - world.dice[b].position;
            const float distance = length(apart);
            const float separation = distance - touching;
            // Spin moves a sphere's contact point sideways, never along the normal.
            const Vec3 normalVelocity = world.dice[a].velocity - world.dice[b].velocity;
            if (separation > speculativeReach(dot(normalVelocity, apart) / distance)) continue;
            const Vec3 normal = apart * (1.0f / distance);
            const Vec3 armA = normal * -world.shape->circumradius;
            const Vec3 armB = normal * world.shape->circumradius;
            ContactImpulse* memory = &world.memory.dice[a][b];
            addContact(world, {static_cast<int8_t>(a), static_cast<int8_t>(b), normal, armA, armB, 0, 0, 0, 0, {}, memory},
                       separation, DIE_RESTITUTION, DIE_FRICTION, invInertia);
        }
    }
}

/**
 * @brief One pass of sequential impulses: each contact's normal impulse, accumulated and kept
 * non-negative so contacts only push, then its friction, kept inside the Coulomb cone.
 */
void solveContacts(World& world, float invInertia) {
    for (uint8_t i = 0; i < contactCount; i++) {
        Contact& contact = contacts[i];
        const float normalSpeed = dot(relativeVelocity(world, contact), contact.normal);
        const float previous = contact.normalImpulse;
        contact.normalImpulse = fmaxf(0.0f, previous + (contact.targetNormalSpeed - normalSpeed) * contact.normalMass);
        applyImpulse(world, contact, contact.normal * (contact.normalImpulse - previous), invInertia);

        const Vec3 velocity = relativeVelocity(world, contact);
        const Vec3 slip = velocity - contact.normal * dot(velocity, contact.normal);
        const float slipSpeed = length(slip);
        if (slipSpeed <= 0) continue;
        const Vec3 slipDirection = slip * (1.0f / slipSpeed);
        const Vec3 previousTangent = contact.tangentImpulse;
        Vec3 tangent = previousTangent - slip * effectiveMass(contact, slipDirection, invInertia);
        const float limit = contact.friction * contact.normalImpulse;
        const float magnitude = length(tangent);
        if (magnitude > limit) tangent = tangent * (limit / magnitude);
        contact.tangentImpulse = tangent;
        applyImpulse(world, contact, tangent - previousTangent, invInertia);
    }
}

void move(World& world) {
    for (uint8_t die = 0; die < world.dieCount; die++) {
        DieBody& body = world.dice[die];
        body.position = body.position + body.velocity * PHYSICS_STEP_S;
        body.orientation = integrate(body.orientation, body.angularVelocity, PHYSICS_STEP_S);
    }
}

/**
 * @brief Move bodies out of what they still overlap, without touching their velocities, so the
 * correction cannot add speed. Two dice that overlap part by half the overlap each; then each die
 * leaves each plane by its deepest vertex. The planes go last because they are the hard limit:
 * parting dice after them pushed a die up to 1.8 mm through a wall, while dice overlapping a little
 * is accepted (ADR-0002, ADR-0009).
 */
void correctPositions(World& world, const Plane planes[TRAY_PLANE_COUNT]) {
    const DieShape& shape = *world.shape;
    const float touching = 2 * shape.circumradius;
    for (uint8_t a = 0; a < world.dieCount; a++) {
        for (uint8_t b = a + 1; b < world.dieCount; b++) {
            const Vec3 apart = world.dice[a].position - world.dice[b].position;
            const float distance = length(apart);
            const float overlap = touching - distance;
            if (overlap <= PENETRATION_SLOP_MM) continue;
            const Vec3 push = apart * ((overlap - PENETRATION_SLOP_MM) / 2 / distance);
            world.dice[a].position = world.dice[a].position + push;
            world.dice[b].position = world.dice[b].position - push;
        }
    }
    for (uint8_t die = 0; die < world.dieCount; die++) {
        DieBody& body = world.dice[die];
        for (uint8_t i = 0; i < TRAY_PLANE_COUNT; i++) {
            float deepest = 0;
            for (uint8_t vertex = 0; vertex < shape.vertexCount; vertex++) {
                const float depth = planes[i].offset - dot(planes[i].inward, worldVertex(body, shape, vertex));
                deepest = fmaxf(deepest, depth);
            }
            if (deepest > PENETRATION_SLOP_MM) body.position = body.position + planes[i].inward * (deepest - PENETRATION_SLOP_MM);
        }
    }
}

} // namespace

Vec3 worldVertex(const DieBody& body, const DieShape& shape, uint8_t vertex) {
    return body.position + rotate(body.orientation, shape.vertices[vertex]);
}

void stepWorld(World& world, const Tray& tray) {
    Plane planes[TRAY_PLANE_COUNT];
    trayPlanes(tray, planes);
    const float invInertia = inverseInertia(*world.shape);

    for (uint8_t die = 0; die < world.dieCount; die++) {
        world.dice[die].velocity = world.dice[die].velocity + world.gravity * PHYSICS_STEP_S;
    }
    contactCount = 0;
    findTrayContacts(world, planes, invInertia);
    findDieContacts(world, invInertia);
    warmStart(world, invInertia);
    for (uint8_t iteration = 0; iteration < SOLVER_ITERATIONS; iteration++) solveContacts(world, invInertia);
    rememberImpulses(world);
    move(world);
    correctPositions(world, planes);
}

float totalEnergy(const World& world) {
    const float inertia = DIE_MASS * world.shape->inertiaPerMass;
    float energy = 0;
    for (uint8_t die = 0; die < world.dieCount; die++) {
        const DieBody& body = world.dice[die];
        energy += DIE_MASS * dot(body.velocity, body.velocity) / 2;
        energy += inertia * dot(body.angularVelocity, body.angularVelocity) / 2;
        energy -= DIE_MASS * dot(world.gravity, body.position);
    }
    return energy;
}
