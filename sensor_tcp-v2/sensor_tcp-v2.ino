
// Flash Size: "1MB (FS:256KB OTA:374KB)"

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h> // For HTTPS, use WiFiClientSecure
#include <TaskScheduler.h>
// ===================================
// Pin Definitions for ESP-01 Board
// ===================================
// #define SERVER_PIN 3  //GPIO3 RX
// #define SENSOR_PIN 1  //GPIO1 TXD
// #define LED_PIN 2     // GPIO2

// ===================================
// Pin Definitions for ESP8266-12E / NodeMCU
// ===================================
#define SERVER_PIN   13  // GPIO13 (D7 on NodeMCU).
#define SENSOR_PIN   5  // GPIO5 (D1 on NodeMCU).
#define LED_PIN      2   // GPIO2 (D4 on NodeMCU - Often used for internal LED).

bool SENSOR_STAT = HIGH;
bool sensorAlarm = false;
bool isServerMode = false;

unsigned long lastSensorCheck = 0;
unsigned long lastServerAPI = 0;
unsigned long lastDiscordAPI = 0;
unsigned long lastDiscordSend = 0;
unsigned long lastApiSend = 0;

void serverMode();
void connectAP();
String chipID();
void save_Config(AsyncWebServerRequest *request);
bool loadConfig();
bool sendMessageToDiscord(String msg);
bool sendDataToAPI(String payload);
String report();

struct Api {
  bool trigger = false;
  int maxRequest = 5;
  bool test = false;  
}api;

struct Discord {
  bool trigger = false;
  int maxRequest = 5;
  bool test = false;  
}discord;
// ประกาศ server แบบ global
AsyncWebServer server(80);

struct Config {
  String id;
  String ssid;
  String password;
  String url; 
  String discord; 
  String esp_now_mac;
} cfg;

void setup() {
  Serial.begin(115200);
  // put your setup code here, to run once:
  pinMode(SERVER_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  
  if(!LittleFS.begin()){    
    return;
  }

  int pinval = digitalRead(SERVER_PIN);
  if(!pinval){
    serverMode();
  }else if(loadConfig()){   
    connectAP(); 
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
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
    }

    if(WiFi.status() == WL_CONNECTED){
      discord.test = true;
      Serial.println(WiFi.localIP());
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
      discord.test = true;
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

String report(){
      
      String statusMsg = "🔧 **ESP8266 Status Report**\\n";
      statusMsg += "**ID:** " + cfg.id +"\\n";
      statusMsg += "**Sensor:** " + String(digitalRead(SENSOR_PIN) ? "🟢 ปกติ":"🔴 ฉุกเฉิน")+"\\n";
      statusMsg += "**WiFi:** " + String(WiFi.isConnected() ? "Connected" : "Disconnected") + "\\n";
      statusMsg += "**IP Address:** " + WiFi.localIP().toString() + "\\n";
      statusMsg += "**Signal (RSSI):** " + String(WiFi.RSSI()) + " dBm\\n";
      statusMsg += "**Free Heap:** " + String(ESP.getFreeHeap()) + " bytes\\n";
      statusMsg += "**Uptime:** " + String(millis()/1000) + " seconds\\n";
      return statusMsg;
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

bool sendMessageToDiscord(String msg = ""){
  if(WiFi.status() != WL_CONNECTED){
    return false;
  }  
  if(msg == "") msg = report();
    
    WiFiClientSecure *client = new WiFiClientSecure();
    client->setInsecure(); // ไม่ตรวจสอบ SSL certificate 
    HTTPClient http;

    if(http.begin(*client, cfg.discord)){
      http.addHeader("Content-Type", "application/json");
      http.addHeader("User-Agent", "ESP-Discord-Bot");
      String jsonPayload = "{\"content\": \"" + msg + "\"}";
      // ส่ง POST request
      int httpResponseCode = http.POST(jsonPayload);    
      if(httpResponseCode > 0){
        Serial.print("Discord http Code: ");
        Serial.println(httpResponseCode);
        http.end();
        delete client;  // ย้ายมาหลัง http.end()
        return true;
      }else{
        Serial.print("Error Discord http Code: ");
        Serial.println(httpResponseCode);
        http.end();
        delete client;  // ย้ายมาหลัง http.end()
        return false;
      }
    }else{
      return false;
    }
  return false;
}

bool sendDataToAPI(String payload) {  

  Serial.println(payload);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi Not connected");
    return false;
  }

  HTTPClient http;
  WiFiClient *client = nullptr;
  WiFiClientSecure *secureClient = nullptr;
  
  if (cfg.url.startsWith("https")) {
    secureClient = new WiFiClientSecure();
    secureClient->setInsecure();
    http.begin(*secureClient, cfg.url);
  } else if(cfg.url.startsWith("http")) {
    client = new WiFiClient();
    http.begin(*client, cfg.url);
  }else{
    http.end();
    // ลบ client หลังจาก http.end()
    if (secureClient) delete secureClient;
    if (client) delete client;
    sendMessageToDiscord("⚠️ Error URL ไม่ถูกต้อง ❌️ "+cfg.url);
    return false;
  }
  
  http.addHeader("Content-Type", "application/json");
  //http.setTimeout(5000); // เพิ่ม timeout 5 วินาที
  int httpResponseCode = http.POST(payload);
  if (httpResponseCode > 0){
    // สามารถส่งไปยังเซิร์พเวอร์ได้นะ
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    http.end();  
    if (secureClient) delete secureClient;
    if (client) delete client;
    return true;  
  }else{
    // Error on sending POST. Code:
    Serial.print("Error on sending POST. Code ");
    Serial.println(httpResponseCode);
    http.end();  
    if (secureClient) delete secureClient;
    if (client) delete client;
    return false;
  }
  return false;
}


void loop() {
  unsigned long now = millis();  
//-----------------------------------------------------------------------
  if(isServerMode){
    delay(10); // ให้ ESP8266 ทำงานพื้นหลัง
    return;
  }
//-----------------------------------------------------------------------
  // Task 1: Check Sensor ทุก 100 ms
  if (now - lastSensorCheck >= 100) {
    SENSOR_STAT     = digitalRead(SENSOR_PIN);
    lastSensorCheck = now;
  }
//-----------------------------------------------------------------------
  // Task 2: Server API ทุก 500 ms
  if (now - lastServerAPI >= 500) {
//    Serial.println("Task 2: Server API ทุก 500 ms");
    discord.trigger = !SENSOR_STAT;
    api.trigger     = !SENSOR_STAT;
    lastServerAPI   = now;
  }

  //-----------------------------------------------------------------------
//  ส่งข้อความไปยังยัง Discord 1 ครั้ง
  if(discord.test && SENSOR_STAT){
    if(discord.maxRequest == 0) return;
    if(sendMessageToDiscord()){
      discord.test = false;
      Serial.println("TEST Discord");
    } 
    discord.maxRequest --;   
  }
//-----------------------------------------------------------------------
  //  ส่งข้อความไปยังยัง Local API 1 ครั้ง
  if(api.test && SENSOR_STAT){
    if(api.maxRequest == 0) return;
    String payload = "{\"id\":\""+cfg.id+"\",\"alram\":"+digitalRead(SENSOR_PIN)+"}";
    if(sendDataToAPI(payload)){
      api.test = false;
      Serial.println("TEST Local API");
    } 
    api.maxRequest --;   
  }
//-----------------------------------------------------------------------  
  // for Local API ทุกๆ 2 วินาที
  if (now - lastApiSend > 2000) {
    if(api.trigger){
      String payload = "{\"id\":\""+cfg.id+"\",\"alram\":"+digitalRead(SENSOR_PIN)+"}";
      sendDataToAPI(payload);      
    }
    //ส่ง 1 ครั้งเพื่อปิด Siren
    if(!api.test && SENSOR_STAT){ 
      Serial.println(!api.test && SENSOR_STAT);
      api.test = true; 
      api.maxRequest = 5; 
    }
    lastApiSend = now;    
  }
//-----------------------------------------------------------------------
  // for Discord ทุก 5 วินาที  
  if (now - lastDiscordSend > 5000) {
    Serial.print("Trigger Discord : ");
    Serial.println(discord.trigger);
    if(discord.trigger){
      sendMessageToDiscord();      
    }
    //ส่ง 1 ครั้ง Discord
    if(!discord.test && SENSOR_STAT){ 
      //discord.test = true;
      discord.maxRequest = 5; 
    }
    lastDiscordSend = now;
  }
}
