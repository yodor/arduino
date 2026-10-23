#pragma once
#include <Arduino.h>

constexpr uint8_t REGULATOR_MODE_PIN = 23;
constexpr uint8_t DIAGNOSTIC_LED_PIN = 25;

/** 
 * TFT Display pins 
 */
constexpr uint8_t TFT_DC   = 16;
constexpr uint8_t TFT_CS   = 17;
constexpr uint8_t TFT_SCK  = 18;
constexpr uint8_t TFT_MOSI = 19;
constexpr uint8_t TFT_MISO = (uint8_t)-1;
constexpr uint8_t TFT_RST  = 20;
constexpr uint8_t TFT_BL   = 21;

/** 
 * ADC sampling pins
 */
constexpr uint8_t AUDIO_PIN_L = 28; // ADC2
constexpr uint8_t AUDIO_PIN_R = 27; // ADC1

/**
 * Keypad GPIO pins UP , DOWN , LEFT , RIGHT
 * Volume control maps to button BTN_UP_PIN, BTN_DN_PIN 
 * these are read as a continuous held/released level, so the volume keeps
 * stepping for as long as the button stays down,
 * unlike BTN_LT_PIN/BTN_RT_PIN (one action per press: a short press on
 * release -- previous/next -- or a 1 s hold: LEFT = menu, RIGHT = power)
 */
constexpr uint8_t BTN_UP_PIN = 2;
constexpr uint8_t BTN_RT_PIN = 3;
constexpr uint8_t BTN_DN_PIN = 13;
constexpr uint8_t BTN_LT_PIN = 12;


/** 
 * Additional masterboard pins: IR sensor (and the unused motor driver pins, kept for reference)
 */

// TSOP31238 output pin
constexpr uint8_t IR_PIN  = 11; 

// DRV8833 motor driver pins (motorized volume pot) -- REFERENCE ONLY: the
// motor control was removed (volume is the daughter board's), and nothing in
// the firmware drives these pins; they stay inputs.
constexpr uint8_t DRV8833_IN1_PIN = 6;
constexpr uint8_t DRV8833_IN2_PIN = 7;

// GPIO8 is free: it used to drive the master-side 4N25 mute, which was
// removed -- muting is handled entirely by the daughter board now.

/**
 * UART link to a daughter board handling mute/volume/calibration, using
 * RP2040 UART0 on its default pin location (GPIO0=TX, GPIO1=RX).
 */
constexpr uint8_t DAUGHTER_UART_TX_PIN = 0;
constexpr uint8_t DAUGHTER_UART_RX_PIN = 1;

/**
 * Power Control 
 */
constexpr uint8_t RELAY_CTRL_PIN = 5;
constexpr uint8_t PWR_BTN_LED_PIN = 9;
constexpr uint8_t PWR_BTN_PIN = 10;

/**
 * I2C ext (bus i2c1)
 */
constexpr uint8_t SDA_PIN = 14;
constexpr uint8_t SCL_PIN = 15;
