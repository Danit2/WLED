#include "sd_manager.h"
#include "usermod_fseq.h"

bool SDManager::begin() {
#if !defined(WLED_USE_SD_SPI) && !defined(WLED_USE_SD_MMC)
#error "FSEQ requires SD backend (WLED_USE_SD_SPI or WLED_USE_SD_MMC)"
#endif

#ifdef WLED_USE_SD_SPI
  if (!SD_ADAPTER.begin(WLED_PIN_SS, spiPort))
    return false;
#elif defined(WLED_USE_SD_MMC)
  if (!SD_ADAPTER.begin())
    return false;
#endif
  return true;
}

void SDManager::end() { SD_ADAPTER.end(); }

bool SDManager::deleteFile(const char *path) { return SD_ADAPTER.remove(path); }