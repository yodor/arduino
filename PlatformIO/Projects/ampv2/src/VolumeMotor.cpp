#include "VolumeMotor.hpp"

VolumeMotor& VolumeMotor::instance() {
    static VolumeMotor inst;
    return inst;
}

void VolumeMotor::init(uint8_t in1Pin, uint8_t in2Pin) {
    m_in1 = in1Pin;
    m_in2 = in2Pin;

    pinMode(m_in1, OUTPUT);
    pinMode(m_in2, OUTPUT);

    stop();
}

void VolumeMotor::update(bool wantUp, bool wantDown) {
    if (m_in1 == 255 || m_in2 == 255) return;

    // Absolute continuous-run safety cutoff, regardless of input source --
    // catches a mechanical jam, a stuck button, or a missed release signal
    // that would otherwise keep wantUp/wantDown true indefinitely.
    if (m_isMoving && (millis() - m_moveStartMs >= MOTOR_MAX_RUN_TIME_MS)) {
        stop();
        return;
    }

    if (wantUp && wantDown) {
        // Ambiguous/conflicting command -- treat as no command, not a short.
        stop();
        return;
    }

    if (wantUp) {
        driveUp();
    } else if (wantDown) {
        driveDown();
    } else {
        stop();
    }
}

void VolumeMotor::driveUp() {
    if (!m_isMoving) {
        m_moveStartMs = millis();
        m_isMoving = true;
    }
    analogWrite(m_in1, m_speed);
    analogWrite(m_in2, 0);
}

void VolumeMotor::driveDown() {
    if (!m_isMoving) {
        m_moveStartMs = millis();
        m_isMoving = true;
    }
    analogWrite(m_in1, 0);
    analogWrite(m_in2, m_speed);
}

void VolumeMotor::stop() {
    if (m_in1 != 255) analogWrite(m_in1, 0);
    if (m_in2 != 255) analogWrite(m_in2, 0);
    m_isMoving = false;
}

void VolumeMotor::increaseSpeed() {
    if (m_speed > MOTOR_MAX_SPEED - MOTOR_SPEED_STEP) {
        m_speed = MOTOR_MAX_SPEED;
    } else {
        m_speed += MOTOR_SPEED_STEP;
    }
}

void VolumeMotor::decreaseSpeed() {
    if (m_speed < MOTOR_MIN_SPEED + MOTOR_SPEED_STEP) {
        m_speed = MOTOR_MIN_SPEED;
    } else {
        m_speed -= MOTOR_SPEED_STEP;
    }
}