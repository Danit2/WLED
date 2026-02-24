# ✨ Usermod FSEQ ✨

> **Created original by: Andrej Chrcek** **Updatet by: Danit2**

Welcome to the **Usermod FSEQ** project!  
This module extends your WLED setup by enabling FSEQ file playback from an SD card, including a web UI and UDP remote control. It combines creativity with functionality to enhance your lighting experience.

---

# FSEQ Web UI

Access the interface via:

http://yourIP/fsequi

or over the WLED Infotab

<img width="792" height="206" alt="image" src="https://github.com/user-attachments/assets/e58693ec-afe8-4c28-8343-5b3382cd10ef" />

---

# SD & FSEQ Usermod for WLED

This usermod adds support for playing FSEQ files from an SD card and provides a web interface for managing SD files and controlling FSEQ playback via HTTP and UDP.

It supports configurable SPI pin settings when using SD over SPI.

The usermod exposes several HTTP endpoints for file management and playback control.

---

## Features

- **FSEQ Playback** – Play FSEQ files from an SD card.
- **Web UI** – Manage SD files (list, upload, delete) and control playback.
- **UDP Synchronization** – Remote control via UDP packets.
- **Configurable SPI Pins** – SPI pin assignments can be configured via WLED’s Usermods settings (JSON).

---

## Installation

### Configure PlatformIO

Add the following to your `platformio_override.ini` (or `platformio.ini`):

[env:esp32dev_V4]
custom_usermods = FSEQ

---

### Storage Configuration

- If you use **SD over SPI**, the build flag  
  `-D WLED_USE_SD_SPI`  
  will be enabled automatically (default behavior).

- If you use **SD via MMC**, you must manually set the build flag:  
  `-D WLED_USE_SD_MMC`

---

## Available Endpoints

### SD Management

GET /fsequi  
Returns the main HTML interface for the SD & FSEQ Manager.

GET api/sd/list  
Displays an HTML page listing all files on the SD card, including options to delete files and upload new ones.

POST api/sd/upload  
Handles file uploads using multipart/form-data.

POST /sd/delete

Deletes the specified file from the SD card.  
Example: /sd/delete 
body: file=example.fseq

---

### FSEQ Control

GET api/fseq/list  
Returns an HTML page listing all .fseq and .FSEQ files found on the SD card. Each file includes a play button.

POST api/fseq/start
body: file=animation.fseq  
Starts playback of the selected FSEQ file.  

POST api/fseq/startloop
body: file=animation.fseq  
Starts playback of the selected FSEQ file in loop mode.  

POST api/fseq/stop  
Stops the current FSEQ playback and clears the active session.

GET /api/fseq/list  
Returns a JSON list of all FSEQ files on the SD card.

---

### FPP Control

GET /api/system/info
Returns a JSON list of the system info

GET /api/system/status
Returns a JSON list of the system status

GET /api/fppd/multiSyncSystems
Returns a JSON list of the multisyncinfos

---

## Configurable SPI Pin Settings

Default SPI pin assignments for SD over SPI:

#ifdef WLED_USE_SD_SPI
int8_t UsermodFseq::configPinSourceSelect = 5;
int8_t UsermodFseq::configPinSourceClock  = 18;
int8_t UsermodFseq::configPinPoci         = 19;
int8_t UsermodFseq::configPinPico         = 23;
#endif

These values can be modified via the WLED Usermods settings tab without recompiling the firmware.

After making changes, you must reboot the device.

---

## Summary

The SD & FSEQ Usermod for WLED enables FSEQ playback from an SD card with a full-featured web interface and UDP synchronization. With configurable SPI pin settings, it integrates seamlessly into WLED without modifying the core code.

For further customization or support, please refer to the project documentation or open an issue on GitHub.