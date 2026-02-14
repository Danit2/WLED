#pragma once

#include "usermod_fseq.h" // Contains FSEQ playback logic and getter methods for pins
#include "wled.h"

#ifdef WLED_USE_SD_SPI
#include <SD.h>
#include <SPI.h>
#elif defined(WLED_USE_SD_MMC)
#include "SD_MMC.h"
#endif

#include <AsyncUDP.h>
#include <ESPAsyncWebServer.h>

// ----- Minimal WriteBufferingStream Implementation -----
// This class buffers data before writing it to an underlying Stream.
class WriteBufferingStream : public Stream {
public:
  WriteBufferingStream(Stream &upstream, size_t capacity)
      : _upstream(upstream) {
    _capacity = capacity;
    _buffer = (uint8_t *)malloc(capacity);
    _offset = 0;
    if (!_buffer) {
      DEBUG_PRINTLN(F("[WBS] ERROR: Buffer allocation failed"));
    }
  }
  ~WriteBufferingStream() {
    flush();
    if (_buffer)
      free(_buffer);
  }
  // Write a block of data to the buffer
  size_t write(const uint8_t *buffer, size_t size) override {
    size_t total = 0;
    while (size > 0) {
      size_t space = _capacity - _offset;
      size_t toCopy = (size < space) ? size : space;
      memcpy(_buffer + _offset, buffer, toCopy);
      _offset += toCopy;
      buffer += toCopy;
      size -= toCopy;
      total += toCopy;
      if (_offset == _capacity)
        flush();
    }
    return total;
  }
  // Write a single byte
  size_t write(uint8_t b) override { return write(&b, 1); }
  // Flush the buffer to the upstream stream
  void flush() override {
    if (_offset > 0) {
      _upstream.write(_buffer, _offset);
      _offset = 0;
    }
    _upstream.flush();
  }
  int available() override { return _upstream.available(); }
  int read() override { return _upstream.read(); }
  int peek() override { return _upstream.peek(); }

private:
  Stream &_upstream;
  uint8_t *_buffer = nullptr;
  size_t _capacity = 0;
  size_t _offset = 0;
};
// ----- End WriteBufferingStream -----

#define FILE_UPLOAD_BUFFER_SIZE 8192

// Definitions for UDP (FPP) synchronization
#define CTRL_PKT_SYNC 1
#define CTRL_PKT_PING 4
#define CTRL_PKT_BLANK 3

// UDP port for FPP discovery/synchronization
const uint16_t UDP_SYNC_PORT = 32320;

unsigned long lastPingTime = 0;
const unsigned long pingInterval = 5000; // 5 Sekunden

// Inline functions to write 16-bit and 32-bit values
static inline void write16(uint8_t *dest, uint16_t value) {
  dest[0] = (value >> 8) & 0xff;
  dest[1] = value & 0xff;
}

static inline void write32(uint8_t *dest, uint32_t value) {
  dest[0] = (value >> 24) & 0xff;
  dest[1] = (value >> 16) & 0xff;
  dest[2] = (value >> 8) & 0xff;
  dest[3] = value & 0xff;
}

// Structure for the synchronization packet
// Using pragma pack to avoid any padding issues
#pragma pack(push, 1)
struct FPPMultiSyncPacket {
  uint8_t header[4];     // e.g. "FPPD"
  uint8_t packet_type;   // e.g. CTRL_PKT_SYNC
  uint16_t data_len;     // data length
  uint8_t sync_action;   // action: start, stop, sync, open, etc.
  uint8_t sync_type;     // sync type, e.g. 0 for FSEQ
  uint32_t frame_number; // current frame number
  float seconds_elapsed; // elapsed seconds
  char filename[64];     // name of the file to play
  uint8_t raw[128];      // raw packet data
};
#pragma pack(pop)

#pragma pack(push, 1)
struct FPPPingPacket {
  uint8_t header[4];
  uint8_t packet_type;
  uint16_t data_len;
  uint8_t ping_version;
  uint8_t ping_subtype;
  uint8_t ping_hardware;
  uint16_t versionMajor;
  uint16_t versionMinor;
  uint8_t operatingMode;
  uint8_t ipAddress[4];
  char hostName[32];
  char version[16];
  char hardwareType[16];
  uint8_t ranges[1];
  uint8_t raw[301];
};
#pragma pack(pop)

// UsermodFPP class: Implements FPP (FSEQ/UDP) functionality
class UsermodFPP : public Usermod {
private:
  AsyncUDP udp;            // UDP object for FPP discovery/sync
  bool udpStarted = false; // Flag to indicate UDP listener status
  const IPAddress multicastAddr =
      IPAddress(239, 70, 80, 80);         // Multicast address
  const uint16_t udpPort = UDP_SYNC_PORT; // UDP port

  // Variables for FSEQ file upload
  File currentUploadFile;
  String currentUploadFileName = "";
  unsigned long uploadStartTime = 0;
  WriteBufferingStream *uploadStream = nullptr;

  // Returns device name from server description
  String getDeviceName() { return String(serverDescription); }

  // Build JSON with system information
  String buildSystemInfoJSON() {
    DynamicJsonDocument doc(1024);
    String devName = getDeviceName();
    doc["HostName"] = devName;
    doc["HostDescription"] = devName;
    doc["Platform"] = "ESPixelStick";
    doc["Variant"] = "ESPixelStick-ESP32";
    doc["Mode"] = "remote"; // 'remote' or 'bridge'
    doc["Version"] = "4.x-dev";

    doc["majorVersion"] = 4;
    doc["minorVersion"] = 0;
    doc["typeId"] = 0xC3;
    doc["UUID"] = WiFi.macAddress(); // Use standard MAC format

    JsonObject utilization = doc.createNestedObject("Utilization");
    utilization["MemoryFree"] = ESP.getFreeHeap();
    utilization["Uptime"] = millis();

    doc["rssi"] = WiFi.RSSI();
    JsonArray ips = doc.createNestedArray("IPS");
    ips.add(WiFi.localIP().toString());

    String json;
    serializeJson(doc, json);
    return json;
  }

  // Build JSON with system status
  String buildSystemStatusJSON() {
    DynamicJsonDocument doc(1024);

    JsonObject mqtt = doc.createNestedObject("MQTT");
    mqtt["configured"] = false;
    mqtt["connected"] = false;

    JsonObject currentPlaylist = doc.createNestedObject("current_playlist");
    currentPlaylist["count"] = "0";
    currentPlaylist["description"] = "";
    currentPlaylist["index"] = "0";
    currentPlaylist["playlist"] = "";
    currentPlaylist["type"] = "";

    doc["volume"] = 70;
    doc["media_filename"] = "";
    doc["fppd"] = "running";
    doc["current_song"] = "";

    if (FSEQPlayer::isPlaying()) {
      doc["current_sequence"] =
          FSEQPlayer::getFileName(); // You need to ensure getFileName() exists
                                     // in FSEQPlayer or track it locally
      doc["playlist"] = "";
      doc["seconds_elapsed"] =
          String(FSEQPlayer::getElapsedSeconds()); // Assuming float to string
                                                   // conversion needed or
                                                   // handled by JSON
      doc["seconds_played"] = String(FSEQPlayer::getElapsedSeconds());
      // doc["seconds_remaining"] = ...;
      doc["sequence_filename"] = FSEQPlayer::getFileName();
      // doc["time_elapsed"] = ...;
      // doc["time_remaining"] = ...;
      doc["status"] = 1;
      doc["status_name"] = "playing";
      doc["mode"] = 8;
      doc["mode_name"] = "remote";
    } else {
      doc["current_sequence"] = "";
      doc["playlist"] = "";
      doc["seconds_elapsed"] = "0";
      doc["seconds_played"] = "0";
      doc["seconds_remaining"] = "0";
      doc["sequence_filename"] = "";
      doc["time_elapsed"] = "00:00";
      doc["time_remaining"] = "00:00";
      doc["status"] = 0;
      doc["status_name"] = "idle";
      doc["mode"] = 8;
      doc["mode_name"] = "remote";
    }

    // Add Advanced View - SysInfo
    // GetSysInfoJSON(doc.createNestedObject("advancedView")); // This would be
    // optimal but we can keep it simple first

    String json;
    serializeJson(doc, json);
    return json;
  }

  // Build JSON for FPP multi-sync systems
  String buildFppdMultiSyncSystemsJSON() {
	DynamicJsonDocument doc(1024);

	JsonArray arr = doc.to<JsonArray>();
	JsonObject obj = arr.createNestedObject();

	String devName = getDeviceName();
	String id = WiFi.macAddress();
	id.replace(":", "");
	id.toUpperCase();

	obj["hostname"] = devName;
	obj["id"] = "WLED-" + id;
	obj["ip"] = WiFi.localIP().toString();
	obj["version"] = "4.x-dev";
	obj["hardwareType"] = "ESPixelStick-ESP32";
	obj["type"] = 0xC3;
	obj["mode"] = "remote";
	obj["num_chan"] = strip.getLength() * 3;
	obj["NumPixelPort"] = 5;
	obj["NumSerialPort"] = 0;

	String json;
	serializeJson(doc, json);
	return json;
  }

  // UDP - send a ping packet
  void sendPingPacket(IPAddress destination = IPAddress(255,255,255,255))
  {
    FPPPingPacket packet;
    memset(&packet, 0, sizeof(packet));

    packet.header[0] = 'F';
    packet.header[1] = 'P';
    packet.header[2] = 'P';
    packet.header[3] = 'D';

    packet.packet_type = 0x04;
    packet.data_len = 294;
    packet.ping_version = 0x03;
    packet.ping_subtype = 0x00;
    packet.ping_hardware = 0xC3;

    packet.versionMajor = 0;
    packet.versionMinor = 16;

    packet.operatingMode = 0x08;   // <<< REMOTE MODE !!!

    IPAddress ip = WiFi.localIP();
    memcpy(packet.ipAddress, &ip, 4);

    String hostName = getDeviceName();
    strncpy(packet.hostName, hostName.c_str(), sizeof(packet.hostName));

    strncpy(packet.version, "4.x-dev", sizeof(packet.version));
    strncpy(packet.hardwareType, "ESPixelStick-ESP32", sizeof(packet.hardwareType));

    udp.writeTo((uint8_t*)&packet, sizeof(packet), destination, udpPort);
  }  

  // UDP - process received packet
  void processUdpPacket(AsyncUDPPacket packet) {
    // Print the raw UDP packet in hex format for debugging
    DEBUG_PRINTLN(F("[FPP] Raw UDP Packet:"));
    for (size_t i = 0; i < packet.length(); i++) {
      DEBUG_PRINTF("%02X ", packet.data()[i]);
    }
    DEBUG_PRINTLN();

    if (packet.length() < 4)
      return;
    if (packet.data()[0] != 'F' || packet.data()[1] != 'P' ||
        packet.data()[2] != 'P' || packet.data()[3] != 'D')
      return;
    uint8_t packetType = packet.data()[4];
    switch (packetType) {
    case CTRL_PKT_SYNC: {
      FPPMultiSyncPacket *syncPacket =
          reinterpret_cast<FPPMultiSyncPacket *>(packet.data());
      DEBUG_PRINTLN(F("[FPP] Received UDP sync packet"));
      // Print detailed sync packet information:
      DEBUG_PRINTF("[FPP] Sync Packet - Action: %d\n", syncPacket->sync_action);
      DEBUG_PRINT(F("[FPP] Filename: "));
      DEBUG_PRINTLN(syncPacket->filename);
      DEBUG_PRINTF("[FPP] Frame Number: %lu\n", syncPacket->frame_number);
      DEBUG_PRINTF("[FPP] Seconds Elapsed: %.2f\n",
                   syncPacket->seconds_elapsed);
      ProcessSyncPacket(syncPacket->sync_action, String(syncPacket->filename),
                        syncPacket->seconds_elapsed);
      break;
    }
    case CTRL_PKT_PING:
      DEBUG_PRINTLN(F("[FPP] Received UDP ping packet"));
      sendPingPacket(packet.remoteIP());
      break;
    case CTRL_PKT_BLANK:
      DEBUG_PRINTLN(F("[FPP] Received UDP blank packet"));
      FSEQPlayer::clearLastPlayback();
      realtimeLock(10, REALTIME_MODE_INACTIVE);
      break;
    default:
      DEBUG_PRINTLN(F("[FPP] Unknown UDP packet type"));
      break;
    }
  }

  // Process sync command with detailed debug output
  void ProcessSyncPacket(uint8_t action, String fileName,
                         float secondsElapsed) {
    // Ensure the filename is absolute
    if (!fileName.startsWith("/")) {
      fileName = "/" + fileName;
    }

    DEBUG_PRINTLN(F("[FPP] ProcessSyncPacket: Sync command received"));
    DEBUG_PRINTF("[FPP] Action: %d\n", action);
    DEBUG_PRINT(F("[FPP] FileName: "));
    DEBUG_PRINTLN(fileName);
    DEBUG_PRINTF("[FPP] Seconds Elapsed: %.2f\n", secondsElapsed);

    switch (action) {
    case 0: // SYNC_PKT_START
      FSEQPlayer::loadRecording(fileName.c_str(), 0, strip.getLength(),
                                secondsElapsed);
      break;
    case 1: // SYNC_PKT_STOP
      FSEQPlayer::clearLastPlayback();
      realtimeLock(10, REALTIME_MODE_INACTIVE);
      break;
    case 2: // SYNC_PKT_SYNC
      DEBUG_PRINTLN(F("[FPP] ProcessSyncPacket: Sync command received"));
      DEBUG_PRINTF("[FPP] Sync Packet - FileName: %s, Seconds Elapsed: %.2f\n",
                   fileName.c_str(), secondsElapsed);
      if (!FSEQPlayer::isPlaying()) {
        DEBUG_PRINTLN(F("[FPP] Sync: Playback not active, starting playback."));
        FSEQPlayer::loadRecording(fileName.c_str(), 0, strip.getLength(),
                                  secondsElapsed);
      } else {
        FSEQPlayer::syncPlayback(secondsElapsed);
      }
      break;
    case 3: // SYNC_PKT_OPEN
      DEBUG_PRINTLN(F(
          "[FPP] Open command received – metadata request (not implemented)"));
      break;
    default:
      DEBUG_PRINTLN(F("[FPP] ProcessSyncPacket: Unknown sync action"));
      break;
    }
  }

public:
  static const char _name[];

  // Setup function called once at startup
  void setup() {
    DEBUG_PRINTF("[%s] FPP Usermod loaded\n", _name);
#ifdef WLED_USE_SD_SPI
    int8_t csPin = UsermodFseq::getCsPin();
    int8_t sckPin = UsermodFseq::getSckPin();
    int8_t misoPin = UsermodFseq::getMisoPin();
    int8_t mosiPin = UsermodFseq::getMosiPin();
    if (!SD.begin(csPin)) {
      DEBUG_PRINTF("[%s] ERROR: SD.begin() failed with CS pin %d!\n", _name,
                   csPin);
    } else {
      DEBUG_PRINTF("[%s] SD card initialized (SPI) with CS pin %d\n", _name,
                   csPin);
    }
#elif defined(WLED_USE_SD_MMC)
    if (!SD_MMC.begin()) {
      DEBUG_PRINTF("[%s] ERROR: SD_MMC.begin() failed!\n", _name);
    } else {
      DEBUG_PRINTF("[%s] SD card initialized (MMC)\n", _name);
    }
#endif

    // Register API endpoints
    server.on("/api/system/info", HTTP_GET,
              [this](AsyncWebServerRequest *request) {
                String json = buildSystemInfoJSON();
                request->send(200, "application/json", json);
              });
    server.on("/api/system/status", HTTP_GET,
              [this](AsyncWebServerRequest *request) {
                String json = buildSystemStatusJSON();
                request->send(200, "application/json", json);
              });
    server.on("/api/fppd/multiSyncSystems", HTTP_GET,
			  [this](AsyncWebServerRequest *request) {
				String json = buildFppdMultiSyncSystemsJSON();
				request->send(200, "application/json", json);
			  });
    // Other API endpoints as needed...

    // Endpoint for file upload (raw, application/octet-stream)
    server.on(
    "/fpp", HTTP_POST,
    [](AsyncWebServerRequest *request) {
        // NICHT hier antworten!
        // Antwort erfolgt erst im letzten Chunk
    },
    NULL,
    [this](AsyncWebServerRequest *request,
           uint8_t *data, size_t len,
           size_t index, size_t total) {

        // Debug optional:
        DEBUG_PRINTF("[FPP] Chunk index=%u len=%u total=%u\n", index, len, total);

        // ---- Erster Chunk ----
        if (index == 0) {

            DEBUG_PRINTLN("[FPP] Starting file upload");

            // Aufräumen falls vorher etwas offen war
            if (uploadStream) {
                uploadStream->flush();
                delete uploadStream;
                uploadStream = nullptr;
            }

            if (currentUploadFile) {
                currentUploadFile.close();
            }

            // Dateiname ermitteln
            String fileParam = "";
            if (request->hasParam("filename")) {
                fileParam = request->arg("filename");
            }

            currentUploadFileName =
                (fileParam != "")
                    ? (fileParam.startsWith("/") ? fileParam : "/" + fileParam)
                    : "/default.fseq";

            DEBUG_PRINTF("[FPP] Using filename: %s\n",
                         currentUploadFileName.c_str());

            // Alte Datei löschen
            if (SD.exists(currentUploadFileName.c_str())) {
                SD.remove(currentUploadFileName.c_str());
            }

            // Neue Datei öffnen
            currentUploadFile =
                SD.open(currentUploadFileName.c_str(), FILE_WRITE);

            if (!currentUploadFile) {
                DEBUG_PRINTLN(F("[FPP] ERROR: Failed to open file"));
                request->send(500, "text/plain", "File open failed");
                return;
            }

            uploadStream = new WriteBufferingStream(
                currentUploadFile, FILE_UPLOAD_BUFFER_SIZE);

            uploadStartTime = millis();
        }

        // ---- Daten schreiben ----
        if (uploadStream) {
            uploadStream->write(data, len);
        }

        // ---- Letzter Chunk ----
        if (index + len == total) {

            DEBUG_PRINTLN("[FPP] Upload finished");

            if (uploadStream) {
                uploadStream->flush();
                delete uploadStream;
                uploadStream = nullptr;
            }

            if (currentUploadFile) {
                currentUploadFile.close();
            }

            unsigned long duration = millis() - uploadStartTime;
            DEBUG_PRINTF("[FPP] Upload complete in %lu ms\n", duration);

            currentUploadFileName = "";

            // JETZT erst HTTP Antwort senden
            request->send(200, "text/plain", "Upload complete");
        }
    });


    // Endpoint to list FSEQ files on SD card
    server.on("/fseqfilelist", HTTP_GET, [](AsyncWebServerRequest *request) {
      DynamicJsonDocument doc(1024);
      JsonArray files = doc.createNestedArray("files");

      File root = SD_ADAPTER.open("/");
      if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
          String name = file.name();
          if (name.endsWith(".fseq") || name.endsWith(".FSEQ")) {
            JsonObject fileObj = files.createNestedObject();
            fileObj["name"] = name;
            fileObj["size"] = file.size();
          }
          file.close();
          file = root.openNextFile();
        }
      } else {
        doc["error"] = "Cannot open SD root directory";
      }

      String json;
      serializeJson(doc, json);
      request->send(200, "application/json", json);
    });

    // Endpoint to start FSEQ playback
    server.on("/fpp/connect", HTTP_GET, [this](AsyncWebServerRequest *request) {
      if (!request->hasArg("file")) {
        request->send(400, "text/plain", "Missing 'file' parameter");
        return;
      }
      String filepath = request->arg("file");
      if (!filepath.startsWith("/")) {
        filepath = "/" + filepath;
      }
      // Use FSEQPlayer to start playback
      FSEQPlayer::loadRecording(filepath.c_str(), 0, strip.getLength());
      request->send(200, "text/plain", "FPP connect started: " + filepath);
    });
    // Endpoint to stop FSEQ playback
    server.on("/fpp/stop", HTTP_GET, [this](AsyncWebServerRequest *request) {
      FSEQPlayer::clearLastPlayback();
      realtimeLock(10, REALTIME_MODE_INACTIVE);
      request->send(200, "text/plain", "FPP connect stopped");
    });

    // Initialize UDP listener for synchronization and ping
    if (!udpStarted && (WiFi.status() == WL_CONNECTED)) {
      if (udp.listenMulticast(multicastAddr, udpPort)) {
        udpStarted = true;
        udp.onPacket(
            [this](AsyncUDPPacket packet) { processUdpPacket(packet); });
        DEBUG_PRINTLN(F("[FPP] UDP listener started on multicast"));
      }
    }
  }

  // Main loop function
  void loop() {
    if (!udpStarted && (WiFi.status() == WL_CONNECTED)) {
      if (udp.listenMulticast(multicastAddr, udpPort)) {
        udpStarted = true;
        udp.onPacket(
            [this](AsyncUDPPacket packet) { processUdpPacket(packet); });
        DEBUG_PRINTLN(F("[FPP] UDP listener started on multicast"));
      }
    }
	
	if (udpStarted && WiFi.status() == WL_CONNECTED) {

      if (millis() - lastPingTime > pingInterval) {
          sendPingPacket();
          lastPingTime = millis();
      }
    }
    // Process FSEQ playback
    FSEQPlayer::handlePlayRecording();
  }

  uint16_t getId() { return USERMOD_ID_SD_CARD; }
  void addToConfig(JsonObject &root) {}
  bool readFromConfig(JsonObject &root) { return true; }
};

const char UsermodFPP::_name[] PROGMEM = "FPP Connect";