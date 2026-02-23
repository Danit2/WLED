#pragma once

#ifndef USED_STORAGE_FILESYSTEMS
#ifdef WLED_USE_SD_SPI
#define USED_STORAGE_FILESYSTEMS "SD SPI, LittleFS"
#else
#define USED_STORAGE_FILESYSTEMS "SD MMC, LittleFS"
#endif
#endif

#include "wled.h"
#ifdef WLED_USE_SD_SPI
#include <SD.h>
#include <SPI.h>
#endif

#ifndef SD_ADAPTER
#if defined(WLED_USE_SD) || defined(WLED_USE_SD_SPI)
#ifdef WLED_USE_SD_SPI
#ifndef WLED_USE_SD
#define WLED_USE_SD
#endif
#ifndef WLED_PIN_SCK
#define WLED_PIN_SCK SCK
#endif
#ifndef WLED_PIN_MISO
#define WLED_PIN_MISO MISO
#endif
#ifndef WLED_PIN_MOSI
#define WLED_PIN_MOSI MOSI
#endif
#ifndef WLED_PIN_SS
#define WLED_PIN_SS SS
#endif
#define SD_ADAPTER SD
#else
#define SD_ADAPTER SD_MMC
#endif
#endif
#endif

#ifdef WLED_USE_SD_SPI
#ifndef SPI_PORT_DEFINED
#if CONFIG_IDF_TARGET_ESP32
inline SPIClass spiPort = SPIClass(VSPI);
#elif CONFIG_IDF_TARGET_ESP32S3
inline SPIClass spiPort = SPI;
#else
inline SPIClass spiPort = SPI;
#endif
#define SPI_PORT_DEFINED
#endif
#endif

#include "fseq_player.h"
#include "sd_manager.h"
#include "web_ui_manager.h"

// Usermod for FSEQ playback with UDP and web UI support
class UsermodFseq : public Usermod {
private:
  WebUIManager webUI;        // Web UI Manager module (handles endpoints)
  static const char _name[]; // for storing usermod name in config

public:
  // Setup function called once at startup
  void setup() {
    DEBUG_PRINTF("[%s] Usermod loaded\n", FPSTR(_name));

    // Initialize SD card using SDManager
    SDManager sd;
    if (!sd.begin()) {
      DEBUG_PRINTF("[%s] SD initialization FAILED.\n", FPSTR(_name));
    } else {
      DEBUG_PRINTF("[%s] SD initialization successful.\n", FPSTR(_name));
    }

    // Register web endpoints defined in WebUIManager
    webUI.registerEndpoints();
  }

  // Loop function called continuously
  void loop() {
    // Process FSEQ playback (includes UDP sync commands)
    FSEQPlayer::handlePlayRecording();
  }

  // Unique ID for the usermod
  uint16_t getId() override { return USERMOD_ID_SD_CARD; }

  // Add a link in the Info tab to your SD
  void addToJsonInfo(JsonObject &root) override {
    JsonObject user = root["u"];
    if (user.isNull()) {
      user = root.createNestedObject("u");
    }
    JsonArray arr = user.createNestedArray("FSEQ UI");
      
    String button = R"rawliteral(
                   <button class="btn ibtn" style="width:100px;" onclick="window.open(getURL('/fsequi'),'_self');" id="updBt">Open UI</button>
                    )rawliteral";
      
    arr.add(button);
  }

  // Save your SPI pins to WLED config 
  void addToConfig(JsonObject &root) override {

  #ifdef WLED_USE_SD_SPI

    JsonObject top = root.createNestedObject(FPSTR(_name));

    top["csPin"]   = configPinSourceSelect;
    top["sckPin"]  = configPinSourceClock;
    top["misoPin"] = configPinPoci;
    top["mosiPin"] = configPinPico;

  #endif
  }
  
  // Read your SPI pins from WLED config JSON
  bool readFromConfig(JsonObject &root) override {
#ifdef WLED_USE_SD_SPI
    JsonObject top = root[FPSTR(_name)];
    if (top.isNull())
      return false;
  
    int8_t oldCs   = configPinSourceSelect;
    int8_t oldSck  = configPinSourceClock;
    int8_t oldMiso = configPinPoci;
    int8_t oldMosi = configPinPico;

    if (top["csPin"].is<int>())
      configPinSourceSelect = top["csPin"].as<int>();
    if (top["sckPin"].is<int>())
      configPinSourceClock = top["sckPin"].as<int>();
    if (top["misoPin"].is<int>())
      configPinPoci = top["misoPin"].as<int>();
    if (top["mosiPin"].is<int>())
      configPinPico = top["mosiPin"].as<int>();

    reinit_SD_SPI(oldCs, oldSck, oldMiso, oldMosi); // reinitialize SD with new pins
    return true;
#else
    return false;
#endif
  }

#ifdef WLED_USE_SD_SPI
  // Reinitialize SD SPI with updated pins
  void reinit_SD_SPI(int8_t oldCs, int8_t oldSck, int8_t oldMiso, int8_t oldMosi) {
    // Deinit SD if needed
    SD_ADAPTER.end();
    // Reallocate pins
    PinManager::deallocatePin(oldCs, PinOwner::UM_SdCard);
    PinManager::deallocatePin(oldSck, PinOwner::UM_SdCard);
    PinManager::deallocatePin(oldMiso, PinOwner::UM_SdCard);
    PinManager::deallocatePin(oldMosi, PinOwner::UM_SdCard);

    PinManagerPinType pins[4] = {{configPinSourceSelect, true},
                                 {configPinSourceClock, true},
                                 {configPinPoci, false},
                                 { configPinPico,
                                   true }};
    if (!PinManager::allocateMultiplePins(pins, 4, PinOwner::UM_SdCard)) {
      DEBUG_PRINTF("[%s] SPI pin allocation failed!\n", FPSTR(_name));
      return;
    }

    // Reinit SPI with new pins
    spiPort.begin(configPinSourceClock, configPinPoci, configPinPico,
                  configPinSourceSelect);

    // Try to begin SD again
    if (!SD_ADAPTER.begin(configPinSourceSelect, spiPort)) {
      DEBUG_PRINTF("[%s] SPI begin failed!\n", FPSTR(_name));
    } else {
      DEBUG_PRINTF("[%s] SD SPI reinitialized with new pins\n", FPSTR(_name));
    }
  }

  // Getter methods and static variables for SD pins
  static int8_t getCsPin() { return configPinSourceSelect; }
  static int8_t getSckPin() { return configPinSourceClock; }
  static int8_t getMisoPin() { return configPinPoci; }
  static int8_t getMosiPin() { return configPinPico; }

  static int8_t configPinSourceSelect;
  static int8_t configPinSourceClock;
  static int8_t configPinPoci;
  static int8_t configPinPico;
#endif
};