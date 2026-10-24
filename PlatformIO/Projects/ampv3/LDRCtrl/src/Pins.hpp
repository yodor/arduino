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

// ---- Driver board v1b (aluminium block, NTC + TL431; its DS18B20 connector is unused) -- the only supported board ----
constexpr const char *DRIVER_BOARD_NAME = "v1b";
// 4n25 amp bootup/power mute pin
constexpr uint8_t AMP_MUTE_PIN  = 18;
/**
* LDR Left Board
*/
constexpr uint8_t PWM_SER_L    = 0;
constexpr uint8_t PWM_SHUNT_L  = 3;
constexpr uint8_t COIL_CTRL_L  = 1;
// Sensors
constexpr uint8_t ADC_NTC_PIN   = 26; // ADC0: 100k B3950 on the transistor block, 100k from ADC_VREF
constexpr uint8_t ADC_GUARD_PIN = 27; // ADC1: tied to AGND on the board -- never drive it
constexpr uint8_t ADC_REF_PIN   = 28; // ADC2: TL431 (2.495 V), tracks the 3.3 V rail

/**
* I2C (bus i2c0)
*/
constexpr uint8_t SDA_LEFT_PIN = 4;
constexpr uint8_t SCL_LEFT_PIN = 5;

/**
* LDR Right Board
*/
constexpr uint8_t PWM_SER_R    = 9;
constexpr uint8_t PWM_SHUNT_R  = 14;
constexpr uint8_t COIL_CTRL_R  = 13;


/**
* I2C (bus i2c1)
*/
constexpr uint8_t SDA_RIGHT_PIN = 10;
constexpr uint8_t SCL_RIGHT_PIN = 11;