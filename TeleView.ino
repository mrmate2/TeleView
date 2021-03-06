/*
   Copyright (c) 2020 Ahmed Maklad. All right reserved.

   Teleview- A telegram photo sending bot using the ESP32 CAM on Arduino IDE.

   This Software is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This Software is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this Software; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/* 
 * To compile and upload:
 * Required Installation:
 *    ESP32 for Arduino IDE https://dl.espressif.com/dl/package_esp32_index.json
 * Required Libraries to compile:
 *    AutoConnect
 *    ArduinoJson - V5.13.5
 *    Adafruit SSD1306 and Dependecies such as Adaruit GFX..etc
*/

/*
 * TODO:
 * 
 *  -   Utilize multiple Telegram-bot Admin IDs. 
 *  -   Utilize multiple Telegram-bot User IDs.
 *  -   Admins have abaility to control options through Telegram.
 *  -   Users can only request photos through Telegram.
 *
 *  -   TimeZone NTP API.
 *  -   Add Google Geolocation API through Wifi.
 *  -   Use clock & location in Something useful such as taking photos only between sunrise and sunset option.
 */

//****************************************************************//
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <AutoConnect.h>
#include "UniversalTelegramBot.h"
#include <Ticker.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
//#define CAMERA_MODEL_AI_THINKER         // Board definition "AI Thinker ESP32-CAM"
#define CAMERA_MODEL_TTGO_T1_CAMERA      // Board definition "ESP32 WROVER Module" or "TTGO T1" 
//////////////////////////////////////                                          // and set Tools-> Partiton Scheme --> Huge App (3MB No OTA/1MB SPIFF)
#include "camera_pins.h"
//////////////////////////////////////
String compileDate=String(__DATE__);
String compileTime=String(__TIME__);
String compileCompiler=String(__cplusplus);
int PICTURES_COUNT=0;
//////////////////////////////////////

//////////////////////////////////////
#include "persist.h"
boolean applyConfigItem (config_item* ci);
#include "webPages.h"

#if defined(I2C_DISPLAY_ADDR)
#include "display.h"
#endif

#include "telegram_utils.h"

bool bTakePhotoTick=false;
Ticker tkTimeLapse;
//****************************************************************//
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // disbale the burnout reset
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.print("Compile Date:");
  Serial.println(compileDate);
  Serial.print("Compile Time:");
  Serial.println(compileTime);
  Serial.print("Compile Compiler:");
  Serial.println(compileCompiler);
  Serial.println();
  //
  //ESP CAMERA
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;  //0-63 lower number means higher quality
    //config.jpeg_quality = 0;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }  
  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
  bCameraInitiated=true;
  ////////////////////////////  
  configItems.frameSize=config.frame_size;
  keyboardJson=formulateKeyboardJson();
  configItems=loadConfiguration();
  applyConfigItem(&configItems);  
  Serial.println(printConfiguration(&configItems,""));
  ////////////////////////////  
#if defined(I2C_DISPLAY_ADDR)
  display_init();  
#endif
  ////////////////////////////
  Portal.host().on("/",rootPage);
  Portal.host().on("/delete",deletePage);
  Portal.host().on("/capture",capturePage);
  Portal.host().on("/capture.jpg",capturePageJpeg);  
  auxPageConfig.load(AUX_CONFIGPAGE);
  populateResolutionsSelects(auxPageConfig);
  auxPageConfig.on(onPage);
  Portal.join(auxPageConfig);
  ////////////////////////////
  acConfig.apid = configItems.deviceName;
  acConfig.psk  = "tv-ei-694";
  acConfig.hostName=configItems.deviceName;  
  acConfig.autoRise=true;
  //acConfig.autoSave=AUTOCONNECT_USE_PREFERENCES;
  //acConfig.portalTimeout = 60000;  // It will time out in 60 seconds
  //#define AC_DEBUG
  Portal.config(acConfig);
  Portal.onDetect(captivePortalStarted);
  WiFi.setSleep(false);
  WiFi.setHostname(configItems.deviceName.c_str());
  /////////////////////////////
#if defined(I2C_DISPLAY_ADDR)
  line1=String(acConfig.apid);
  line2=String(acConfig.psk);
  line3=String(acConfig.apip.toString());
  display_Textlines(line1,line2,line3);
#endif
  //MDNS
  if (!MDNS.begin(configItems.deviceName.c_str())) {
      Serial.println("Error setting up MDNS responder!");
      while(1) {
          delay(1000);
      }
  }
  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
  Serial.println("mDNS responder started");
  /////////////////////////////
  if (Portal.begin()) {
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
//    if (ESPmDNS.begin(configItems.deviceName)) {
//      ESPmDNS.addService("http", "tcp", 80);
//    }
  }else{
    Serial.println("Portal not startd");
  }  
  Serial.println(WiFi.SSID());
  long rssi = WiFi.RSSI();
  Serial.print("RSSI:");
  Serial.println(rssi);
  //  
  byte mac[6];   
  WiFi.macAddress(mac);
  Serial.print("WiFi MAC: ");
  Serial.print(mac[5],HEX);
  Serial.print(":");
  Serial.print(mac[4],HEX);
  Serial.print(":");
  Serial.print(mac[3],HEX);
  Serial.print(":");
  Serial.print(mac[2],HEX);
  Serial.print(":");
  Serial.print(mac[1],HEX);
  Serial.print(":");
  Serial.println(mac[0],HEX);
  //
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  bot.setToken(configItems.botTTelegram);
  bot.sendMessage(configItems.adminChatIds, "I am Alive!!", "");
  Serial.println("I am Alive!!");
  ///////////////////
#if defined(FLASH_LAMP_PIN)
  pinMode(FLASH_LAMP_PIN, OUTPUT);
#endif

#if defined(PIR_PIN)
  pinMode(PIR_PIN, INPUT);
#endif
  bTelegramBotInitiated=true;
}
//****************************************************************//
void loop() {
  Portal.handleClient();
#if defined(PIR_PIN)
  if ( configItems.motDetectOn && digitalRead(PIR_PIN)) {
    Serial.println("Motion Detected."); 
    bot.sendMessage(configItems.adminChatIds, "Motion Detected!","" );
    String restult= sendCapturedImage2Telegram(configItems.adminChatIds);
    Serial.println("restult: "+restult);
    delay(100);
  }  
#endif
#if defined(I2C_DISPLAY_ADDR)
  line1=String(WiFi.SSID());
  line2="                    ";
  line3=WiFi.localIP().toString();
  if (displayEnabled){
    display_Textlines( line1, line2 , line3 );
  }else{
    display.clearDisplay();
    display.display();
  }
#endif
  if (millis() > Bot_lasttime + Bot_mtbs)  {
    int numNewMessages = bot.getUpdates((bot.last_message_received) + 1);
    while(numNewMessages) {
      Serial.println("got response#1");
      handleNewMessages(numNewMessages);
      Serial.println("got response#2");
      numNewMessages = bot.getUpdates((bot.last_message_received) + 1);
      Serial.println("got response#3");
    }
    if (bTakePhotoTick){
      Serial.println("Tick!"); 
      bot.sendMessage(configItems.adminChatIds, "Tick!","" );
      String restult= sendCapturedImage2Telegram(configItems.adminChatIds);
      Serial.println("restult: "+restult);   
      bTakePhotoTick=false;
    }
    Bot_lasttime = millis();
  }
}

////////////////////////////////////////////////////////////////////////////
void tick(){
  bTakePhotoTick=true;
}
////////////////////////////////////////////////////////////////////////////
boolean applyConfigItem (config_item* ci) {
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, ci->frameSize);  // UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA
  s->set_hmirror(s, ci->hMirror);
  s->set_vflip(s, ci->vFlip);
  tkTimeLapse.detach();
  if (ci->lapseTime >0){
    tkTimeLapse.attach(  ci->lapseTime*60 , tick );
  }  
#if defined(I2C_DISPLAY_ADDR)
  if (ci->screenFlip){
    display.setRotation(2);
  }else{
    display.setRotation(0);
  }
  displayEnabled=ci->screenOn;
#endif
}
