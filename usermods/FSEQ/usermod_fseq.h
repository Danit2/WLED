#pragma once

#include "wled.h"

#ifdef WLED_USE_SD_SPI
#include <SD.h>
#include <SPI.h>
#endif

// Define SD_ADAPTER macro if not already defined (used by FSEQ file operations)
#ifndef SD_ADAPTER
#if defined(WLED_USE_SD_SPI)
#define SD_ADAPTER SD
#elif defined(WLED_USE_SD_MMC)
#define SD_ADAPTER SD_MMC
#endif
#endif

#include "fseq_player.h"
#include "fseq_effect.h"
#include "web_ui_manager.h"

// Usermod for FSEQ playback with UDP and web UI support.
// SD card initialisation is handled by the standard sd_card usermod.
class UsermodFseq : public Usermod {
private:
  WebUIManager webUI;        // Web UI Manager module (handles endpoints)
  static const char _name[]; // for storing usermod name in config

public:
  static uint8_t fseqEffectId; // effect ID assigned by strip.addEffect()

  // Setup function called once at startup
  void setup() {
    DEBUG_PRINTF("[%s] Usermod loaded\n", FPSTR(_name));

    // Register the FSEQ Player as a WLED effect and store its ID
    fseqEffectId = strip.addEffect(255, &mode_fseq_player, _data_FX_MODE_FSEQ_PLAYER);

    // Register web endpoints defined in WebUIManager
    webUI.registerEndpoints();
  }

  // Loop function called continuously
  void loop() {
    // FSEQ playback is now driven by the WLED effect engine via mode_fseq_player.
    // No work needed here.
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

  void addToConfig(JsonObject &root) override {}
  bool readFromConfig(JsonObject &root) override { return true; }
};