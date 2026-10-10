#pragma once
#include <Arduino.h>

#ifdef LDR_BOARD_IS_PICO_W
// On Pico W, GPIO23/25 are the wireless chip's own power-on/SPI-CS lines
// (see the LDR_BOARD_IS_PICO_W note in platformio.ini) -- the equivalent
// functions move to the wireless chip's own GPIO extension instead,
// reached through Arduino-Pico's pin>=32 numbering scheme (confirmed by
// the core's own maintainer: https://github.com/earlephilhower/arduino-pico/discussions/835).
// digitalWrite() on these routes through cyw43_arch_gpio_put() internally
// -- including lazily bringing up the wireless chip's SPI interface and
// firmware the first time it's used. That's a real, deliberate tradeoff:
// it means the CYW43 is no longer genuinely dormant, just never joined to
// a network -- worth knowing, not just an implementation detail.
constexpr uint8_t REGULATOR_MODE_PIN = 33; // WL_GPIO1 -- SMPS power-save control
constexpr uint8_t DIAGNOSTIC_LED_PIN = 32; // WL_GPIO0 -- onboard LED
#else
constexpr uint8_t REGULATOR_MODE_PIN = 23;
constexpr uint8_t DIAGNOSTIC_LED_PIN = 25;
#endif

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