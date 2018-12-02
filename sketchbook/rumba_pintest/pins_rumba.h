#define ORIG_X_STEP_PIN         17
#define ORIG_X_DIR_PIN          16
#define ORIG_X_ENABLE_PIN       48
#define ORIG_X_MIN_PIN          37
#define ORIG_X_MAX_PIN          36

#define ORIG_Y_STEP_PIN         54
#define ORIG_Y_DIR_PIN          47
#define ORIG_Y_ENABLE_PIN       55
#define ORIG_Y_MIN_PIN          35
#define ORIG_Y_MAX_PIN          34

#define ORIG_Z_STEP_PIN         57
#define ORIG_Z_DIR_PIN          56
#define ORIG_Z_ENABLE_PIN       62
#define ORIG_Z_MIN_PIN          33
#define ORIG_Z_MAX_PIN          32

#define ORIG_E0_STEP_PIN         23
#define ORIG_E0_DIR_PIN          22
#define ORIG_E0_ENABLE_PIN       24

#define ORIG_E1_STEP_PIN        26
#define ORIG_E1_DIR_PIN         25
#define ORIG_E1_ENABLE_PIN      27

#define ORIG_E2_STEP_PIN        29
#define ORIG_E2_DIR_PIN         28
#define ORIG_E2_ENABLE_PIN      39

#define LED_PIN            13

#define ORIG_FAN_PIN            7
#define ORIG_FAN2_PIN     8 // (e.g. useful for electronics fan or light on/off) on PIN 8

#define ORIG_PS_ON_PIN          45

 // EXTRUDER 1
#define HEATER_0_PIN       2   
// EXTRUDER 2
#define HEATER_2_PIN       3    
// EXTRUDER 3
#define HEATER_3_PIN       6    
//optional FAN1 can be used as 4th heater output: #define HEATER_4_PIN       8    // EXTRUDER 4
 // BED
#define HEATER_1_PIN       9   

// ANALOG NUMBERING
#define TEMP_0_PIN         15  
#define TEMP_2_PIN         14  
#define TEMP_3_PIN         13  
//optional for extruder 4 or chamber: #define TEMP_2_PIN         12   // ANALOG NUMBERING
#define TEMP_1_PIN       11

#define SDPOWER            -1
#define SDSS               53
#define SCK_PIN          52
#define MISO_PIN         50
#define MOSI_PIN         51

#define E0_PINS ORIG_E0_STEP_PIN,ORIG_E0_DIR_PIN,ORIG_E0_ENABLE_PIN,
#define E1_PINS ORIG_E1_STEP_PIN,ORIG_E1_DIR_PIN,ORIG_E1_ENABLE_PIN,
#define E2_PINS ORIG_E2_STEP_PIN,ORIG_E2_DIR_PIN,ORIG_E2_ENABLE_PIN,test
