
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

bool pendingDiscordMessage = false;
String statusMsg = "";

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
void sendMessageToDiscord(String msg);

// ประกาศ server แบบ global
AsyncWebServer server(80);
bool isServerMode = false;

struct Config {
  String id;
  String ssid;
  String password;
  String url; 
  String discord; 
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
      digitalWrite(LED_PIN, LOW);
    });
    
    wifiEventHandler[2] = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& evt) {
      // handleWiFiEvent("Got IP", "IP: " + evt.ip.toString());
      digitalWrite(LED_PIN, HIGH);
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
      request->send(200, "text/html", "<center><br>⚠️error⚠️<h1>ESP8266 Config</h1><p>index.html not found in LittleFS "+(String)duration+"</p></center>");
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
  cfg.discord     = doc["discord"].as<String>();  
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

    server.on("/", HTTP_GET,[](AsyncWebServerRequest *request){
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<style>";
  html += "body{font-family:Arial;margin:20px;background:#f0f0f0}";
  html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
  html += "h1{color:#333;text-align:center;margin-bottom:30px}";
  html += ".status-item{padding:10px;margin:10px 0;background:#f9f9f9;border-left:4px solid #4CAF50;border-radius:4px}";
  html += ".label{font-weight:bold;color:#555}";
  html += ".value{color:#333;float:right}";
  html += ".btn{display:block;width:100%;padding:15px;margin:20px 0;font-size:16px;font-weight:bold;";
  html += "color:white;background:#4CAF50;border:none;border-radius:5px;cursor:pointer;transition:0.3s}";
  html += ".btn:hover{background:#45a049}";
  html += ".btn:active{transform:scale(0.98)}";
  html += "#result{margin-top:20px;padding:15px;border-radius:5px;display:none;text-align:center;font-weight:bold}";
  html += ".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}";
  html += ".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>🔧 ESP8266 Status</h1>";
  
  // WiFi Status
  html += "<div class='status-item'>";
  html += "<span class='label'>WiFi:</span>";
  html += "<span class='value'>" + String(WiFi.isConnected() ? "✅ Connected" : "❌ Disconnected") + "</span>";
  html += "</div>";
  
  // IP Address
  html += "<div class='status-item'>";
  html += "<span class='label'>IP Address:</span>";
  html += "<span class='value'>" + WiFi.localIP().toString() + "</span>";
  html += "</div>";
  
  // Signal Strength
  html += "<div class='status-item'>";
  html += "<span class='label'>Signal (RSSI):</span>";
  html += "<span class='value'>" + String(WiFi.RSSI()) + " dBm</span>";
  html += "</div>";
  
  // Free Memory
  html += "<div class='status-item'>";
  html += "<span class='label'>Free Heap:</span>";
  html += "<span class='value'>" + String(ESP.getFreeHeap()) + " bytes</span>";
  html += "</div>";
  
  // Uptime
  html += "<div class='status-item'>";
  html += "<span class='label'>Uptime:</span>";
  html += "<span class='value'>" + String(millis()/1000) + " seconds</span>";
  html += "</div>";
  
  // Test Button
  html += "<button class='btn' onclick='sendTest()'>📤 Send Test Message to Discord</button>";
  
  // Result message area
  html += "<div id='result'></div>";
  
  html += "</div>";
  
  // JavaScript
  html += "<script>";
  html += "function sendTest(){";
  html += "  document.getElementById('result').style.display='none';";
  html += "  fetch('/test')";
  html += "    .then(response => response.text())";
  html += "    .then(data => {";
  html += "      var resultDiv = document.getElementById('result');";
  html += "      resultDiv.className = 'success';";
  html += "      resultDiv.textContent = '✅ ' + data;";
  html += "      resultDiv.style.display = 'block';";
  html += "      setTimeout(() => location.reload(), 2000);";
  html += "    })";
  html += "    .catch(error => {";
  html += "      var resultDiv = document.getElementById('result');";
  html += "      resultDiv.className = 'error';";
  html += "      resultDiv.textContent = '❌ Error: ' + error;";
  html += "      resultDiv.style.display = 'block';";
  html += "    });";
  html += "}";
  html += "</script>";
  
  html += "</body></html>";
  
  request->send(200, "text/html", html);
});

    server.on("/test", HTTP_GET,[](AsyncWebServerRequest *request){
      pendingDiscordMessage = true;  
      statusMsg = "🔧 **ESP8266 Status Report**\\n";
      statusMsg += "**SENSOR ID:** " + cfg.id;
      statusMsg += "**WiFi:** " + String(WiFi.isConnected() ? "✅ Connected" : "❌ Disconnected") + "\\n";
      statusMsg += "**IP Address:** " + WiFi.localIP().toString() + "\\n";
      statusMsg += "**Signal (RSSI):** " + String(WiFi.RSSI()) + " dBm\\n";
      statusMsg += "**Free Heap:** " + String(ESP.getFreeHeap()) + " bytes\\n";
      statusMsg += "**Uptime:** " + String(millis()/1000) + " seconds\\n";
      
      request->send(200, "text/plain", "Message queued");    
    });

     // จัดการ request ที่ไม่ตรงกับ route ใดๆ
  server.onNotFound([](AsyncWebServerRequest *request){    
    request->send(404, "text/plain", "Not Found "+request->url());
  });
  // เริ่ม server
  server.begin();
  
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
  String discord      = "";
  String esp_now_mac  = "";
  
  if (request->hasParam("discord")){
    discord = request->getParam("discord")->value();
  }
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
  doc["discord"]      = discord;
  doc["esp_now_mac"]  = esp_now_mac;
  // เขียน JSON ลงไฟล์
  if(serializeJson(doc, file) == 0){
    file.close();
    return request->send(500, "application/json", "{\"error\":\"Failed to write to file\"}");
  }
  file.close();
  request->send(200, "application/json", "{\"success\":\"Configuration saved successfully\"}");

}

void sendMessageToDiscord(String msg){
  if(WiFi.status() != WL_CONNECTED) return; //Wifi not connected - cannor send message

  WiFiClientSecure client;
  client.setInsecure(); // ไม่ตรวจสอบ SSL certificate 
  HTTPClient http;

  if(http.begin(client, cfg.discord)){
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "ESP-Discord-Bot");
    String jsonPayload = "{\"content\": \"" + msg + "\"}";
    // ส่ง POST request
    int httpResponseCode = http.POST(jsonPayload);    
  }
  http.end();

}


void loop() {

  if(isServerMode){
    delay(10); // ให้ ESP8266 ทำงานพื้นหลัง
    return;
  }

  // ส่งข้อความ Discord ที่รอคิวอยู่
  if(pendingDiscordMessage){
    sendMessageToDiscord(statusMsg);
    pendingDiscordMessage = false;
    statusMsg = "";
  }

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
