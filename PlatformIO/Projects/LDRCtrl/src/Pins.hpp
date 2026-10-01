#pragma once
#include <Arduino.h>

constexpr uint8_t REGULATOR_MODE_PIN = 23;
constexpr uint8_t DIAGNOSTIC_LED_PIN = 25;

/**
 * UART link to a master board handling mute/volume/calibration commands, using
 * RP2040 UART0 alternative pin location.
 */
constexpr uint8_t UART_TX_PIN = 16;
constexpr uint8_t UART_RX_PIN = 17;

// 4n25 amp bootup/power mute pin
constexpr uint8_t AMP_MUTE_PIN  = 21; 

/**
* LDR Left Board 
*/
constexpr uint8_t PWM_SER_L  = 1; 
constexpr uint8_t PWM_SHUNT_L  = 6; 
constexpr uint8_t COIL_CTRL_L  = 3; 

/**
* I2C (bus i2c0)
*/
constexpr uint8_t SDA_LEFT_PIN = 4;
constexpr uint8_t SCL_LEFT_PIN = 5;

/**
* LDR Right Board 
*/
constexpr uint8_t PWM_SER_R  = 14; 
constexpr uint8_t PWM_SHUNT_R  = 9; 
constexpr uint8_t COIL_CTRL_R  = 12; 


/**
* I2C (bus i2c1)
*/
constexpr uint8_t SDA_RIGHT_PIN = 10;
constexpr uint8_t SCL_RIGHT_PIN = 11;