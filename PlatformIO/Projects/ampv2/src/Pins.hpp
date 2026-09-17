#pragma once
#include <Arduino.h>

constexpr uint8_t REGULATOR_MODE_PIN = 23;

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
 * these are read as a continuous held/released level so the motor keeps
 * running for as long as the button stays down 
 * unlike BTN_LT_PIN/BTN_RT_PIN (edge-triggered, one action per press)
 */
constexpr uint8_t BTN_UP_PIN = 0;
constexpr uint8_t BTN_RT_PIN = 1;
constexpr uint8_t BTN_DN_PIN = 14;
constexpr uint8_t BTN_LT_PIN = 15;


/** 
 * Additional masterboard pins for mute ctrl, IR sensor and motorized volume control 
 */

// TSOP31238 output pin
constexpr uint8_t IR_PIN  = 19; 

// DRV8833 motor driver pins, driving a motorized volume potentiometer.
constexpr uint8_t DRV8833_IN1_PIN = 16;
constexpr uint8_t DRV8833_IN2_PIN = 17;

// 4n25 mute pin
constexpr uint8_t MUTE_PIN  = 20; 

/**
 * UART link to a daughter board handling mute/volume/calibration, using
 * RP2040 UART0's alternate pin location (GPIO12=TX, GPIO13=RX) rather than
 * its default GPIO0/1.
 */
constexpr uint8_t DAUGHTER_UART_TX_PIN = 12;
constexpr uint8_t DAUGHTER_UART_RX_PIN = 13;