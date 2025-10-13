
// Flash Size: "1MB (FS:256KB OTA:374KB)"

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h> // For HTTPS, use WiFiClientSecure

// ===================================
// Pin Definitions for ESP-01 Board
// ===================================
//#define SERVER_PIN 3
//#define SENSOR_PIN 1
//#define LED_PIN 2

// ===================================
// Pin Definitions for ESP8266-12E / NodeMCU
// ===================================
#define SERVER_PIN   13  // GPIO13 (D7 on NodeMCU).
#define SENSOR_PIN   14  // GPIO14 (D5 on NodeMCU).
#define LED_PIN      2   // GPIO2 (D4 on NodeMCU - Often used for internal LED).

unsigned long previousMillis = 0;
unsigned long interval = 30000;
// ตัวแปรสำหรับเก็บ Event Handler
WiFiEventHandler wifiEventHandler[3];

// ฟังก์ชันนี้จะถูกเรียกเมื่อเชื่อมต่อกับ Wi-Fi สำเร็จและได้รับ IP Address แล้ว
void onWiFiEvent(WiFiEvent_t event);

void serverMode();
void connectAP();
String chipID();
void save_Config(AsyncWebServerRequest *request);
bool loadConfig();

// ประกาศ server แบบ global
AsyncWebServer server(80);
bool isServerMode = false;

struct Config {
  String id;
  String ssid;
  String password;
  String url;  
  String esp_now_mac;
} cfg;

void setup() {
  // put your setup code here, to run once:
  pinMode(SERVER_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  if(!LittleFS.begin()){    
    return;
  }

  int pinval = digitalRead(SERVER_PIN);
  if(!pinval){
    serverMode();
  }else if(loadConfig()){    
    // ลงทะเบียน Event Handlers ทั้งหมดในที่เดียว
    wifiEventHandler[0] = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& evt) {
      // handleWiFiEvent("Connected", "SSID: " + evt.ssid);
    });
    
    wifiEventHandler[1] = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& evt) {
      // handleWiFiEvent("Disconnected", "Reason: " + String(evt.reason));
    });
    
    wifiEventHandler[2] = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& evt) {
      // handleWiFiEvent("Got IP", "IP: " + evt.ip.toString());
    });
    WiFi.setAutoReconnect(true); 
    connectAP();
  }else{
    
  }

}

void serverMode(){
  isServerMode = true;
  // เริ่ม โหมด Access Point และ Webserver
  String apName = "esp_" + chipID();
  WiFi.softAP(apName.c_str(), "");
  delay(100); //หน่วงเวลา 100 ms เพื่อให้ AP เริ่มทำงาน

  server.on("/", HTTP_GET,[](AsyncWebServerRequest *request){
    if(LittleFS.exists("/index.html")){
      request->send(LittleFS, "/index.html", "text/html");
    } else {
      int duration = millis();
      request->send(200, "text/html", "<h1>ESP8266 Config</h1><p>index.html not found in LittleFS "+(String)duration+"</p>");
    }
  });

  server.on("/save_config", HTTP_GET,[](AsyncWebServerRequest *request){
    save_Config(request);
  });

  // จัดการ request ที่ไม่ตรงกับ route ใดๆ
  server.onNotFound([](AsyncWebServerRequest *request){    
    request->send(404, "text/plain", "Not Found "+request->url());
  });
  // เริ่ม server
  server.begin();

}

bool loadConfig() {
  
  // 1. เปิดไฟล์สำหรับอ่าน
  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    // คืนค่าเป็น false เพื่อบอกว่าโหลดไม่สำเร็จ
    return false;
  }
  
  // 2. แยกวิเคราะห์ JSON
  StaticJsonDocument<256> doc;
  
  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  
  // ปิดไฟล์ทันทีหลังจากอ่าน
  file.close(); 
  
  // 3. ตรวจสอบข้อผิดพลาดในการแยกวิเคราะห์
  if (error) {  
    // คืนค่าเป็น false เพื่อบอกว่าโหลดไม่สำเร็จ  
    return false;
  }

  // ดึงค่าจาก JSON ลงในสมาชิกของ Struct (Object) ที่ถูกส่งเข้ามา (cfg)
  
  cfg.id          = doc["id"].as<String>();
  cfg.ssid        = doc["ssid"].as<String>();
  cfg.password    = doc["password"].as<String>();
  cfg.url         = doc["url"].as<String>();  
  cfg.esp_now_mac = doc["esp_now_mac"].as<String>();

  return true;
}


void connectAP(){
  if(cfg.ssid != ""){
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
    WiFi.setAutoReconnect(true);
    while (WiFi.status() != WL_CONNECTED) {
      digitalWrite(LED_PIN, HIGH);
      delay(500);
      digitalWrite(LED_PIN, LOW);
      delay(500);
    }
    digitalWrite(LED_PIN, HIGH);
  }
}

String chipID(){
    uint32_t chipId = ESP.getChipId();
    String chipIDStr = String(chipId, HEX);
    chipIDStr.toUpperCase();
    return chipIDStr;
}

void save_Config(AsyncWebServerRequest *request){
  if (!request->hasParam("id")) return request->send(400, "application/json", "{\"error\":\"No parameters ID\"}");
  if (!request->hasParam("ssid")) return request->send(400, "application/json", "{\"error\":\"No parameters SSID\"}");
  if (!request->hasParam("password")) return request->send(400, "application/json", "{\"error\":\"No parameters PASSWORD\"}");
  if (!request->hasParam("url")) return request->send(400, "application/json", "{\"error\":\"No parameters URL\"}");
  
  // รับค่าจาก request มาเก็บลงตัวแปร
  String id         = request->getParam("id")->value();
  String ssid         = request->getParam("ssid")->value();
  String password     = request->getParam("password")->value();
  String url          = request->getParam("url")->value();
  String esp_now_mac  = "";
  if (request->hasParam("esp_now_mac")){
    esp_now_mac  = request->getParam("esp_now_mac")->value();
  }
  

  File file = LittleFS.open("/config.json","w");
  if(!file){
    return request->send(400, "application/json", "{\"error\":\"Failed to create new config file\"}");
  }

  // ใช้ ArduinoJson
  StaticJsonDocument<256> doc;
  doc["id"]           = id;
  doc["ssid"]         = ssid;
  doc["password"]     = password;
  doc["url"]          = url;  
  doc["esp_now_mac"]  = esp_now_mac;
  // เขียน JSON ลงไฟล์
  if(serializeJson(doc, file) == 0){
    file.close();
    return request->send(500, "application/json", "{\"error\":\"Failed to write to file\"}");
  }
  file.close();
  request->send(200, "application/json", "{\"success\":\"Configuration saved successfully\"}");

}



void loop() {

  if(isServerMode){
    delay(10); // ให้ ESP8266 ทำงานพื้นหลัง
    return;
  }
  // ใน loop() คุณสามารถใส่โค้ดการทำงานหลักของคุณได้เลย 
  // โดยไม่ต้องมีโค้ดตรวจสอบสถานะการเชื่อมต่อ Wi-Fi ซ้ำๆ 
  // เพราะ Event Handlers จะจัดการการเชื่อมต่อ/ตัดการเชื่อมต่อในพื้นหลังแล้ว

  // ตัวอย่าง: โค้ดสำหรับทำอะไรสักอย่างทุก 5 วินาที
  static unsigned long lastTime = 0;
  if (millis() - lastTime > 5000) {
    if (WiFi.isConnected()) {
      // ทำงานที่ต้องใช้ Wi-Fi
      // Serial.println("Service running... WiFi is connected.");
    } else {
      // ทำงานในกรณีที่ Wi-Fi ยังไม่เชื่อมต่อ
      // Serial.println("Service paused... Waiting for WiFi connection.");
    }
    lastTime = millis();
  }

}
