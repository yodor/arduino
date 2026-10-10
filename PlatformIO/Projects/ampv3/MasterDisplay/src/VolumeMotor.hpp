#pragma once
#include "Config.hpp"

// Drives a DRV8833 H-bridge to turn a motorized volume potentiometer.
//
// This class only owns the motor itself and its safety cutoffs -- it does
// not decide *why* the motor should be moving. Call update() every loop
// iteration with the current desired direction, already resolved by the
// caller from whatever input source(s) drive volume (a physical button's
// debounced level, a recent-enough IR repeat signal, or both combined).
class VolumeMotor {
public:
    static VolumeMotor& instance();

    void init(uint8_t in1Pin, uint8_t in2Pin);

    // wantUp/wantDown reflect "should the motor be driving this direction
    // right now" -- true for as long as whatever's commanding it indicates
    // the command is still active. Call once per loop iteration; stops the
    // motor immediately once neither is true. If both are true at once,
    // treated as an ambiguous/no command (motor stops), not a short.
    void update(bool wantUp, bool wantDown);

    // Immediately stops the motor (coast, both driver inputs low).
    void stop();

    uint8_t getSpeed() const { return m_speed; }
    void increaseSpeed();
    void decreaseSpeed();

private:
    VolumeMotor() = default;

    void driveUp();
    void driveDown();

    uint8_t m_in1 = 255;
    uint8_t m_in2 = 255;

    uint8_t  m_speed    = MOTOR_DEFAULT_SPEED;
    bool     m_isMoving = false;
    uint32_t m_moveStartMs = 0;
};