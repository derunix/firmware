// ============================================================
//  M5Stack Cardputer Advanced – variant pin definitions
//  SoC : ESP32-S3FN8 @ 240 MHz  (8 MB flash, no PSRAM)
//  LoRa: Cap LoRa868 – SX1262 via SPI2
//  GPS : Cap LoRa868 – ATGM336H-6N (AT6668) via UART @ 115200
//  Disp: ST7789V2 1.14" 240×135 via SPI2 (shared bus)
//  KBD : TCA8418 56-key matrix via I2C (bus 1)
//  IMU : BMI270 via I2C (bus 1)
//  AUD : ES8311 codec + NS4150B amp via I2S
// ============================================================

// ---- Display: ST7789V2 240×135 ---------------------------
#define USE_ST7789

#define ST7789_NSS 37
#define ST7789_RS 34  // DC
#define ST7789_SDA 35 // MOSI
#define ST7789_SCK 36
#define ST7789_RESET 33
#define ST7789_MISO -1
#define ST7789_BUSY -1
#define VTFT_LEDA 38   // backlight (powers NeoPixel rail too)
#define ST7789_SPI_HOST SPI2_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 135
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0

// ---- Buttons ---------------------------------------------
#define BUTTON_PIN 0
#define HAS_PHYSICAL_KEYBOARD 1

// ---- I2C buses -------------------------------------------
// Bus 0: expansion header (G8=SDA, G9=SCL) – ATGM336H PPS / ext modules
#define I2C_SDA 8
#define I2C_SCL 9

// Bus 1: internal (BMI270, TCA8418 keyboard controller)
#define I2C_SDA1 2
#define I2C_SCL1 1

// ---- LoRa: SX1262 (Cap LoRa868 via EXT 2.54-14P) --------
// SPI pins (shared SPI2 bus with display, chip-select separates them)
#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK  40   // G40
#define LORA_MISO 39   // G39
#define LORA_MOSI 14   // G14
#define LORA_CS   5    // NSS / G5

#define USE_SX1262
#define LORA_DIO0   -1           // not connected on SX1262
#define LORA_RESET  3            // RST / G3
#define LORA_RST    3
#define LORA_DIO1   4            // IRQ / G4
#define LORA_DIO2   6            // BUSY / G6
#define LORA_DIO3   RADIOLIB_NC  // not connected

#define SX126X_CS    LORA_CS
#define SX126X_DIO1  LORA_DIO1
#define SX126X_BUSY  LORA_DIO2
#define SX126X_RESET LORA_RESET

// SX1262 on the Cap LoRa868 uses DIO2 as TX/RX antenna switch
#define SX126X_DIO2_AS_RF_SWITCH
// TCXO is present on the Stamp LoRa-1262 but may not always respond –
// mark as optional so the driver falls back gracefully
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#define TCXO_OPTIONAL

// ---- GPS: ATGM336H-6N (AT6668), auto-detected via NMEA --
// Cap LoRa868 routes GPS UART to G13 (RX ← GPS TX) and G15 (TX → GPS RX)
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN   15  // G15 → MCU RX (GPS serial TX)
#define GPS_TX_PIN   13  // G13 → MCU TX (GPS serial RX)
#define HAS_GPS 1
#define GPS_BAUDRATE 115200
// ATGM336H supports up to 10 Hz; poll at 100 ms
#define GPS_THREAD_INTERVAL 100

// ---- I2S Audio: ES8311 codec + NS4150B amp ---------------
#define HAS_I2S
#define DAC_I2S_BCK  41
#define DAC_I2S_WS   43
#define DAC_I2S_DOUT 42
#define DAC_I2S_DIN  46
#define DAC_I2S_MCLK 45 // dummy (ES8311 derives MCLK internally)

// ---- Keyboard: TCA8418 I2C matrix controller -------------
#define I2C_NO_RESCAN
#define KB_INT 11

// ---- RGB LED: NeoPixel WS2812 ----------------------------
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA  21
#define NEOPIXEL_TYPE  (NEO_GRB + NEO_KHZ800)

// ---- Battery voltage divider (100k+100k) -----------------
#define BATTERY_PIN    10
#define ADC_CHANNEL    ADC1_GPIO10_CHANNEL
// Two equal resistors → ×2 divider, +2 % trim to reach CHARGING threshold
#define ADC_MULTIPLIER (2 * 1.02)

// ---- IMU: BMI270 (6-axis) --------------------------------
#define HAS_BMI270

// ---- Battery current estimates (µA for idle; mA for active modes) ----
// Source: Cardputer-Adv datasheet + Cap LoRa868 module datasheet
// Whole-device operating current @ 4.2 V: ~120 mA
// Wi-Fi active adds ~12 mA; BLE active adds ~35 mA; GPS adds ~17 mA
#define BAT_BASE_CURRENT_MA  55.0f  // CPU + display + keyboard, no radio
#define BAT_TX_CURRENT_MA   145.0f  // GPS on + LoRa TX max power
#define BAT_GPS_CURRENT_MA   80.0f  // GPS on, LoRa standby
#define BAT_WIFI_CURRENT_MA  90.0f  // Wi-Fi active, LoRa standby

// ---- Traffic management (packet dedup cache) -------------
#ifndef HAS_TRAFFIC_MANAGEMENT
#define HAS_TRAFFIC_MANAGEMENT 1
#endif
#ifndef TRAFFIC_MANAGEMENT_CACHE_SIZE
#define TRAFFIC_MANAGEMENT_CACHE_SIZE 1024
#endif
