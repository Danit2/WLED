#pragma once

#include "usermod_fseq.h"
#include "wled.h"

#ifdef WLED_USE_SD_SPI
#include <SD.h>
#include <SPI.h>
#elif defined(WLED_USE_SD_MMC)
#include "SD_MMC.h"
#endif

#include <AsyncUDP.h>
#include <ESPAsyncWebServer.h>

#define CTRL_PKT_SYNC   1
#define CTRL_PKT_BLANK  3
#define CTRL_PKT_PING   4

#define UDP_SYNC_PORT   32320

#pragma pack(push, 1)
struct FPPMultiSyncPacket {
  uint8_t header[4];
  uint8_t packet_type;
  uint16_t data_len;
  uint8_t sync_action;
  uint8_t sync_type;
  uint32_t frame_number;
  float seconds_elapsed;
  char filename[64];
  uint8_t raw[128];
};
#pragma pack(pop)

class UsermodFPP : public Usermod {
private:

  AsyncUDP udp;
  bool udpStarted = false;
  const IPAddress multicastAddr = IPAddress(239,70,80,80);

  String getDeviceName() { return String(serverDescription); }

  // ============================================================
  //  System Info (echtes FPP Remote Verhalten)
  // ============================================================

  String buildSystemInfoJSON() {
    DynamicJsonDocument doc(1024);

    String devName = getDeviceName();

    doc["HostName"] = devName;
    doc["HostDescription"] = devName;

    doc["Platform"] = "FPP";
    doc["Variant"]  = "Remote";
    doc["Mode"]     = "remote";

    doc["Version"] = "4.0";
    doc["majorVersion"] = 4;
    doc["minorVersion"] = 0;

    doc["typeId"] = 0;   // wichtig: echtes FPP Remote
    doc["UUID"]   = WiFi.macAddress();

    JsonArray ips = doc.createNestedArray("IPS");
    ips.add(WiFi.localIP().toString());

    String json;
    serializeJson(doc, json);
    return json;
  }

  String buildSystemStatusJSON() {
    DynamicJsonDocument doc(512);

    doc["fppd"] = "running";

    if (FSEQPlayer::isPlaying()) {
      doc["status"] = 1;
      doc["status_name"] = "playing";
      doc["seconds_elapsed"] = FSEQPlayer::getElapsedSeconds();
      doc["sequence_filename"] = FSEQPlayer::getFileName();
    } else {
      doc["status"] = 0;
      doc["status_name"] = "idle";
    }

    String json;
    serializeJson(doc, json);
    return json;
  }

  String buildMultiSyncJSON() {
    DynamicJsonDocument doc(512);
    JsonArray arr = doc.to<JsonArray>();

    JsonObject obj = arr.createNestedObject();

    String id = WiFi.macAddress();
    id.replace(":","");
    id.toUpperCase();

    obj["hostname"] = getDeviceName();
    obj["id"] = id;
    obj["ip"] = WiFi.localIP().toString();
    obj["version"] = "4.0";
    obj["hardwareType"] = "FPP Remote";
    obj["type"] = 0;
    obj["mode"] = "remote";
    obj["num_chan"] = strip.getLength()*3;

    String json;
    serializeJson(doc, json);
    return json;
  }

  // ============================================================
  //  Ping Packet (korrekt)
  // ============================================================

  void sendPingPacket(IPAddress dest) {

    uint8_t buf[301];
    memset(buf,0,sizeof(buf));

    buf[0]='F'; buf[1]='P'; buf[2]='P'; buf[3]='D';
    buf[4]=CTRL_PKT_PING;

    uint16_t len = 294;
    buf[5]= (len>>8)&0xFF;
    buf[6]= len&0xFF;

    buf[7]=0x03;
    buf[8]=0x00;

    // Hardware ID = 0 (FPP Remote)
    buf[9]=0x00;

    buf[14]=0x08; // operatingMode remote

    IPAddress ip = WiFi.localIP();
    buf[15]=ip[0];
    buf[16]=ip[1];
    buf[17]=ip[2];
    buf[18]=ip[3];

    String name = getDeviceName();
    for(int i=0;i<32;i++)
      buf[19+i] = (i<name.length())?name[i]:0;

    udp.writeTo(buf,sizeof(buf),dest,UDP_SYNC_PORT);
  }

  // ============================================================
  //  UDP Verarbeitung
  // ============================================================

  void processUdpPacket(AsyncUDPPacket packet) {

    if(packet.length()<5) return;
    if(packet.data()[0]!='F') return;

    uint8_t type = packet.data()[4];

    if(type==CTRL_PKT_PING) {
      sendPingPacket(packet.remoteIP());
      return;
    }

    if(type==CTRL_PKT_BLANK) {
      FSEQPlayer::clearLastPlayback();
      realtimeLock(10, REALTIME_MODE_INACTIVE);
      return;
    }

    if(type!=CTRL_PKT_SYNC) return;

    FPPMultiSyncPacket* p =
      (FPPMultiSyncPacket*)packet.data();

    uint8_t action = p->sync_action;

    // Big Endian Fix
    uint32_t frame =
      __builtin_bswap32(p->frame_number);

    float seconds = p->seconds_elapsed;

    String file = String(p->filename);

    int slash = file.lastIndexOf('/');
    if(slash>=0) file=file.substring(slash);
    if(!file.startsWith("/")) file="/"+file;

    switch(action) {

      // OPEN
      case 3:
        FSEQPlayer::loadRecording(file.c_str(),0,strip.getLength(),0);
        FSEQPlayer::pausePlayback();
        break;

      // START
      case 0:
        realtimeLock(65000, REALTIME_MODE_FSEQ);
        FSEQPlayer::loadRecording(file.c_str(),0,strip.getLength(),seconds);
        break;

      // STOP
      case 1:
        FSEQPlayer::clearLastPlayback();
        realtimeLock(10, REALTIME_MODE_INACTIVE);
        break;

      // SYNC
      case 2:
        if(!FSEQPlayer::isPlaying()) {
          realtimeLock(65000, REALTIME_MODE_FSEQ);
          FSEQPlayer::loadRecording(file.c_str(),0,strip.getLength(),seconds);
        } else {
          FSEQPlayer::syncPlayback(seconds);
        }
        break;
    }
  }

public:

  static const char _name[];

  void setup() {

#ifdef WLED_USE_SD_SPI
    SD.begin(UsermodFseq::getCsPin());
#elif defined(WLED_USE_SD_MMC)
    SD_MMC.begin();
#endif

    server.on("/api/system/info",HTTP_GET,
      [this](AsyncWebServerRequest *r){
        r->send(200,"application/json",buildSystemInfoJSON());
      });

    server.on("/api/system/status",HTTP_GET,
      [this](AsyncWebServerRequest *r){
        r->send(200,"application/json",buildSystemStatusJSON());
      });

    server.on("/api/fppd/multiSyncSystems",HTTP_GET,
      [this](AsyncWebServerRequest *r){
        r->send(200,"application/json",buildMultiSyncJSON());
      });

    udp.listen(UDP_SYNC_PORT);
    udp.listenMulticast(multicastAddr,UDP_SYNC_PORT);
    udp.onPacket([this](AsyncUDPPacket p){processUdpPacket(p);});
  }

  void loop() {

    static unsigned long lastPing=0;

    if(millis()-lastPing>5000) {
      sendPingPacket(IPAddress(255,255,255,255));
      sendPingPacket(multicastAddr);
      lastPing=millis();
    }

    FSEQPlayer::handlePlayRecording();
  }

  uint16_t getId(){return USERMOD_ID_SD_CARD;}
};

const char UsermodFPP::_name[] PROGMEM = "FPP Connect";
