// Flash Size: "1MB (FS:256KB OTA:374KB)"

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Ticker.h>  // เพิ่ม Ticker สำหรับ async task

// ===================================
// Pin Definitions for ESP8266-12E / NodeMCU
// ===================================
#define SERVER_PIN   13  // GPIO13 (D7 on NodeMCU).
#define SENSOR_PIN   14  // GPIO14 (D5 on NodeMCU).
#define LED_PIN      2   // GPIO2 (D4 on NodeMCU).

// ===================================
// ตัวแปร Global
// ===================================
bool pendingDiscordMessage = false;
String statusMsg = "";
bool sensorAlarm = false;
bool pendingSendDataToApi = false;

// เก็บสถานะเซ็นเซอร์ครั้งก่อน
bool lastSensorState = HIGH;

// ตัวแปรสำหรับ debounce
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 100; // 50ms

// ตัวแปรสำหรับจำกัดความถี่ในการส่งข้อมูล
unsigned long lastSendTime = 0;
const unsigned long MIN_SEND_INTERVAL = 2000; // ส่งห่างกันอย่างน้อย 2 วินาที

WiFiEventHandler wifiEventHandler[3];
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

// ===================================
// ฟังก์ชันประกาศล่วงหน้า
// ===================================
void serverMode();
void connectAP();
String chipID();
void save_Config(AsyncWebServerRequest *request);
bool loadConfig();
void sendMessageToDiscord(String msg);
void sendDataToAPI(String payload);
void report();
void checkSensor();
void processAsyncTasks();

void setup() {
  Serial.begin(115200);
  pinMode(SERVER_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  if(!LittleFS.begin()){    
    return;
  }

  // อ่านสถานะเริ่มต้นของเซ็นเซอร์
  // lastSensorState = digitalRead(SENSOR_PIN);

  int pinval = digitalRead(SERVER_PIN);
  if(!pinval){
    serverMode();
  }else if(loadConfig()){    
    wifiEventHandler[0] = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& evt) {
      // Connected
    });
    
    wifiEventHandler[1] = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& evt) {
      digitalWrite(LED_PIN, LOW);
    });
    
    wifiEventHandler[2] = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP& evt) {
      digitalWrite(LED_PIN, HIGH);
      pendingDiscordMessage = true;
      pendingSendDataToApi = true;
    });
    WiFi.setAutoReconnect(true); 
    connectAP();
  }
}

void serverMode(){
  isServerMode = true;
  String apName = "esp_" + chipID();
  WiFi.softAP(apName.c_str(), "");
  delay(100);

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

  server.onNotFound([](AsyncWebServerRequest *request){    
    request->send(404, "text/plain", "Not Found "+request->url());
  });
  
  server.begin();
}

bool loadConfig() {
  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    return false;
  }
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close(); 
  
  if (error) {  
    return false;
  }

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
    
    // เปลี่ยนจาก while loop เป็นการรอแบบ non-blocking
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(200);
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
      request->send(200, "text/plain", "Message queued");    
    });

    server.onNotFound([](AsyncWebServerRequest *request){    
      request->send(404, "text/plain", "Not Found "+request->url());
    });
    
    server.begin();
  }
}

void report(){
  statusMsg = "🔧 **ESP8266 Status Report**\\n";
  statusMsg += "**ID:** " + cfg.id +"\\n";
  statusMsg += "**Sensor:** " + String(digitalRead(SENSOR_PIN) ? "🟢 ปกติ":"🔴 ฉุกเฉิน")+"\\n";
  statusMsg += "**WiFi:** " + String(WiFi.isConnected() ? "Connected" : "Disconnected") + "\\n";
  statusMsg += "**IP Address:** " + WiFi.localIP().toString() + "\\n";
  statusMsg += "**Signal (RSSI):** " + String(WiFi.RSSI()) + " dBm\\n";
  statusMsg += "**Free Heap:** " + String(ESP.getFreeHeap()) + " bytes\\n";
  statusMsg += "**Uptime:** " + String(millis()/1000) + " seconds\\n";
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
  
  String id         = request->getParam("id")->value();
  String ssid       = request->getParam("ssid")->value();
  String password   = request->getParam("password")->value();
  String url        = request->getParam("url")->value();
  String discord    = "";
  String esp_now_mac = "";
  
  if (request->hasParam("discord")){
    discord = request->getParam("discord")->value();
  }
  if (request->hasParam("esp_now_mac")){
    esp_now_mac = request->getParam("esp_now_mac")->value();
  }

  File file = LittleFS.open("/config.json","w");
  if(!file){
    return request->send(400, "application/json", "{\"error\":\"Failed to create new config file\"}");
  }

  StaticJsonDocument<256> doc;
  doc["id"]           = id;
  doc["ssid"]         = ssid;
  doc["password"]     = password;
  doc["url"]          = url;  
  doc["discord"]      = discord;
  doc["esp_now_mac"]  = esp_now_mac;
  
  if(serializeJson(doc, file) == 0){
    file.close();
    return request->send(500, "application/json", "{\"error\":\"Failed to write to file\"}");
  }
  file.close();
  request->send(200, "application/json", "{\"success\":\"Configuration saved successfully\"}");
}

void sendMessageToDiscord(String msg){
  Serial.println("sendMessageToDiscord");
  if(WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure *client = new WiFiClientSecure();
  client->setInsecure();
  HTTPClient http;

  if(http.begin(*client, cfg.discord)){
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "ESP-Discord-Bot");
    String jsonPayload = "{\"content\": \"" + msg + "\"}";
    http.POST(jsonPayload);
  }
  http.end();
  delete client;  // ย้ายมาหลัง http.end()
}

void sendDataToAPI(String payload) {
  Serial.println("sendDataToAPI");
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  WiFiClient *client = nullptr;
  WiFiClientSecure *secureClient = nullptr;
  
  if (cfg.url.startsWith("https")) {
    secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    http.begin(*secureClient, cfg.url);
  } else {
    client = new WiFiClient();
    http.begin(*client, cfg.url);
  }
  
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000); // เพิ่ม timeout 5 วินาที
  http.POST(payload);
  http.end();
  
  // ลบ client หลังจาก http.end()
  if (secureClient) delete secureClient;
  if (client) delete client;
}

// ฟังก์ชันตรวจสอบเซ็นเซอร์แบบ debounce
void checkSensor() {
  bool currentState = digitalRead(SENSOR_PIN);
  //Serial.println("Check Sensor: currentState=" + String(currentState) + " lastSensorState=" + String(lastSensorState));
  
  if (currentState != lastSensorState) {
    lastDebounceTime = millis();
    Serial.println("State Change Detected - Reset debounce timer");
  }
  unsigned long currentMillis = millis();
  Serial.println("Time elapsed since state change: " + String(currentMillis - lastDebounceTime));

  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // (currentState != lastSensorState) {
      lastSensorState = currentState;
      Serial.println("State Confirmed Change");
      
      if (currentState == LOW) {
        sensorAlarm = true;
        pendingDiscordMessage = true;
        pendingSendDataToApi = true;
      } else {
        if (sensorAlarm) {
          sensorAlarm = false;
          pendingDiscordMessage = true;
          pendingSendDataToApi = true;
        }
      }
    //}
  }
}

// ฟังก์ชันจัดการ task แบบทำทีละอย่าง
void processAsyncTasks() {
  // ส่งข้อความ Discord ก่อน (ถ้ามี)
  if(pendingDiscordMessage) {
    report();
    sendMessageToDiscord(statusMsg);
    statusMsg = "";
    pendingDiscordMessage = false;
    return; // ออกจากฟังก์ชันเพื่อไม่ให้ทำงานซ้อนกัน
  }
  
  // ส่งข้อมูลไป API ทีหลัง (ถ้ามี)
  if(pendingSendDataToApi) {
    if(WiFi.isConnected()) {
      String payload = "{\"id\":\""+cfg.id+"\",\"alarm\":"+String(lastSensorState)+"}";
      sendDataToAPI(payload);
    }
    pendingSendDataToApi = false;
    return; // ออกจากฟังก์ชันเพื่อไม่ให้ทำงานซ้อนกัน
  }
}

void loop() {
  if(isServerMode){
    yield(); // ให้ ESP8266 ทำงานพื้นหลัง
    return;
  }

  // ตรวจสอบเซ็นเซอร์ทุก loop
  checkSensor();
  
  // จัดการส่งข้อมูล แต่จำกัดความถี่
  if(millis() - lastSendTime > MIN_SEND_INTERVAL) {
    if(pendingDiscordMessage || pendingSendDataToApi) {
      processAsyncTasks();
      lastSendTime = millis();
    }
  }
  
  // ให้ CPU ทำงานอื่นได้
  yield();
}
