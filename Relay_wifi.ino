/*
 * ESP8266 + Relay WiFi
 * Copyright (C) 2017-2018 Giannidb
 * 
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

 
 The blue LED on the ESP-01 module is connected to GPIO1 
 (which is also the TXD pin; so we cannot use Serial.print() at the same time)
 
 Note that this sketch uses LED_BUILTIN to find the pin with the internal LED
 */

extern "C" {
#include "user_interface.h"
}

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WebSocketsServer.h>
#include <FS.h>


#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#include "Field.h"
#include "Fields.h"

//const bool apMode = true;

ESP8266WebServer webServer(80);
WebSocketsServer webSocketsServer = WebSocketsServer(81);
ESP8266HTTPUpdateServer httpUpdateServer;


#include "WiFi.h"
#include "FSBrowser.h"

#define DATA_PIN      D1



void setup() {
  
  Serial.begin(115200);
  delay(100);
  Serial.setDebugOutput(true);
  
  EEPROM.begin(512);
  loadSettings();

  Serial.println();
  Serial.print( F("Heap: ") ); Serial.println(system_get_free_heap_size());
  Serial.print( F("Boot Vers: ") ); Serial.println(system_get_boot_version());
  Serial.print( F("CPU: ") ); Serial.println(system_get_cpu_freq());
  Serial.print( F("SDK: ") ); Serial.println(system_get_sdk_version());
  Serial.print( F("Chip ID: ") ); Serial.println(system_get_chip_id());
  Serial.print( F("Flash ID: ") ); Serial.println(spi_flash_get_id());
  Serial.print( F("Flash Size: ") ); Serial.println(ESP.getFlashChipRealSize());
  Serial.print( F("Vcc: ") ); Serial.println(ESP.getVcc());
  Serial.println();

  SPIFFS.begin();
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), String(fileSize).c_str());
    }
    Serial.printf("\n");
  }
  
  //disabled due to https://github.com/jasoncoon/esp8266-fastled-webserver/issues/62
  
  initializeWiFi();

//  if (apMode)
//  {
//    WiFi.mode(WIFI_AP);
//
//    // Do a little work to get a unique-ish name. Append the
//    // last two bytes of the MAC (HEX'd) to "Thing-":
//    uint8_t mac[WL_MAC_ADDR_LENGTH];
//    WiFi.softAPmacAddress(mac);
//    String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
//                   String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
//    macID.toUpperCase();
//    String AP_NameString = "ESP8266 Thing " + macID;
//
//    char AP_NameChar[AP_NameString.length() + 1];
//    memset(AP_NameChar, 0, AP_NameString.length() + 1);
//
//    for (int i = 0; i < AP_NameString.length(); i++)
//      AP_NameChar[i] = AP_NameString.charAt(i);
//
//    WiFi.softAP(AP_NameChar, WiFiAPPSK);
//
//    Serial.printf("Connect to Wi-Fi access point: %s\n", AP_NameChar);
//    Serial.println("and open http://192.168.4.1 in your browser");
//  }
//  else
//  {
//    WiFi.mode(WIFI_STA);
//    Serial.printf("Connecting to %s\n", ssid);
//    if (String(WiFi.SSID()) != String(ssid)) {
//      WiFi.begin(ssid, password);
//    }
//
//    while (WiFi.status() != WL_CONNECTED) {
//      delay(500);
//      Serial.print(".");
//    }
//
//    Serial.print("Connected! Open http://");
//    Serial.print(WiFi.localIP());
//    Serial.println(" in your browser");
//  }
  
  checkWiFi();

  httpUpdateServer.setup(&webServer);
  
  webServer.on("/all", HTTP_GET, []() {
    String json = getFieldsJson(fields, fieldCount);
    webServer.send(200, "text/json", json);
  });
  
  webServer.on("/fieldValue", HTTP_GET, []() {
    String name = webServer.arg("name");
    String value = getFieldValue(name, fields, fieldCount);
    webServer.send(200, "text/json", value);
  });

  webServer.on("/fieldValue", HTTP_POST, []() {
    String name = webServer.arg("name");
    String value = webServer.arg("value");
    String newValue = setFieldValue(name, value, fields, fieldCount);
    webServer.send(200, "text/json", newValue);
  });  

    webServer.on("/power", HTTP_POST, []() {
    String value = webServer.arg("value");
    setPower(value.toInt());
    sendInt(power);
  });
   
    webServer.on("/brightness", HTTP_POST, []() {
    String value = webServer.arg("value");
    setBrightness(value.toInt());
    sendInt(brightness);
  });
   
  //list directory
  webServer.on("/list", HTTP_GET, handleFileList);
  //load editor
  webServer.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) webServer.send(404, "text/plain", "FileNotFound");
  });
  //create file
  webServer.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  webServer.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  webServer.on("/edit", HTTP_POST, []() {
    webServer.send(200, "text/plain", "");
  }, handleFileUpload);

  webServer.serveStatic("/", SPIFFS, "/", "max-age=86400"); 
   
  webServer.begin();
  Serial.println("HTTP web server started");

  webSocketsServer.begin();
  webSocketsServer.onEvent(webSocketEvent);
  Serial.println("Web socket server started");

  
  pinMode(DATA_PIN, OUTPUT);
}

void sendInt(uint8_t value)
{
  sendString(String(value));
}

void sendString(String value)
{
  webServer.send(200, "text/plain", value);
}

void broadcastInt(String name, uint8_t value)
{
  String json = "{\"name\":\"" + name + "\",\"value\":" + String(value) + "}";
  webSocketsServer.broadcastTXT(json);
}

void broadcastString(String name, String value)
{
  String json = "{\"name\":\"" + name + "\",\"value\":\"" + String(value) + "\"}";
  webSocketsServer.broadcastTXT(json);
}

void loop()
{


  webSocketsServer.loop();
  webServer.handleClient();
  
    Serial.print("*Power: "); Serial.println(power);
    digitalWrite(DATA_PIN, power);
    delay(1000);                  // waits for a second

}

void loadSettings()
{
   brightness = EEPROM.read(0);
   power = EEPROM.read(5);
}

void setPower(uint8_t value)
{
  power = value == 0 ? 0 : 1;

  EEPROM.write(5, power);
  EEPROM.commit();

  broadcastInt("power", power);
}

void setBrightness(uint8_t value)
{
  if (value > 180)
    value = 180;
  else if (value < 0) value = 0;

  brightness = value;

  //FastLED.setBrightness(brightness);

  EEPROM.write(0, brightness);
  EEPROM.commit();

  broadcastInt("brightness", brightness);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocketsServer.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

        // send message to client
        // webSocketsServer.sendTXT(num, "Connected");
      }
      break;

    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", num, payload);

      // send message to client
      // webSocketsServer.sendTXT(num, "message here");

      // send data to all connected clients
      // webSocketsServer.broadcastTXT("message here");
      break;

    case WStype_BIN:
      Serial.printf("[%u] get binary length: %u\n", num, length);
      hexdump(payload, length);

      // send message to client
      // webSocketsServer.sendBIN(num, payload, lenght);
      break;
  }
}

