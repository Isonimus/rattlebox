#ifndef STARTABLE_H
#define STARTABLE_H

/**
 * @brief A part of the hardware that boot brings up, one step at a time (slice 0003).
 *
 * begin() reports failure instead of logging and carrying on, so boot can stop at the
 * first part that did not come up and say which one and why.
 */
class Startable {
public:
    virtual ~Startable() = default;

    /** @brief Bring the part up. False when it did not, with failureReason() saying why. */
    virtual bool begin() = 0;

    /** @brief Why the last begin() failed; only meaningful after it returned false. */
    virtual const char* failureReason() const = 0;

    /** @brief What came up, for the boot log line, e.g. the chip and its configuration. */
    virtual const char* description() const = 0;
};

#endif // STARTABLE_H
