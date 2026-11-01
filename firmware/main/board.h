#pragma once
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32
// Original LILYGO / TTGO T-Display, NOT the S3 model.
constexpr int NodeId=1, SensorPin=27, Relay1=25, Relay2=26, RelayEnable=17;
constexpr int PeerTx=21, PeerRx=22, WatchdogPin=13;
constexpr int KeyPins[4]={32,33,36,39}; // Q1 lock, Q2 -, Q3 +, Q4 save
constexpr bool HasDisplay=true;
constexpr char BoardName[]="LILYGO TTGO T-Display (ESP32, ST7789 135x240)";
#elif CONFIG_IDF_TARGET_ESP32C6
// Seeed Studio XIAO ESP32-C6; GPIO3/14 are reserved for the RF switch.
constexpr int NodeId=2, SensorPin=0, Relay1=1, Relay2=2, RelayEnable=21;
constexpr int PeerTx=16, PeerRx=17, WatchdogPin=18;
constexpr int KeyPins[4]={-1,-1,-1,-1};
constexpr bool HasDisplay=false;
constexpr char BoardName[]="Seeed Studio XIAO ESP32-C6";
#elif CONFIG_IDF_TARGET_ESP32C3
// Generic ESP32-C3 DevKitM-1 profile. C3 replaces A; no integrated LCD.
constexpr int NodeId=1, SensorPin=3, Relay1=4, Relay2=5, RelayEnable=6;
constexpr int PeerTx=20, PeerRx=21, WatchdogPin=7;
constexpr int KeyPins[4]={-1,-1,-1,-1};
constexpr bool HasDisplay=false;
constexpr char BoardName[]="Espressif ESP32-C3-DevKitM-1 (without LCD/keypad)";
#else
#error Unsupported ESP32 target
#endif
