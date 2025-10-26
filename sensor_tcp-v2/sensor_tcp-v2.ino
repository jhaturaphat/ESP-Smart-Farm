
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

// buffer สำหรับ Discord
#define JSON_BUFFER_SIZE 300
char jsonPayload[JSON_BUFFER_SIZE];
// buffer สำหรับ Local API
#define BUFFER_SIZE_API 64
#define BUFFER_COUNT_API 5
char bufferPool[BUFFER_COUNT_API][BUFFER_SIZE_API];
int currentIndex = 0;

// ===================================
// #define HTTPC_ERROR_CONNECTION_FAILED   (-1)
// #define HTTPC_ERROR_SEND_HEADER_FAILED  (-2)
// #define HTTPC_ERROR_SEND_PAYLOAD_FAILED (-3)
// #define HTTPC_ERROR_NOT_CONNECTED       (-4)
// #define HTTPC_ERROR_CONNECTION_LOST     (-5)
// #define HTTPC_ERROR_NO_STREAM           (-6)
// #define HTTPC_ERROR_NO_HTTP_SERVER      (-7)
// #define HTTPC_ERROR_TOO_LESS_RAM        (-8)
// #define HTTPC_ERROR_ENCODING            (-9)
// #define HTTPC_ERROR_STREAM_WRITE        (-10)
// #define HTTPC_ERROR_READ_TIMEOUT        (-11)
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
bool sendDiscordReport();
bool sendMessageToDiscord(String msg);
bool sendDataToAPI();
// String report();

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
  char id[3];
  char ip[16];
  char ssid[32];
  char password[32];
  char url_api[64];
  String discord_api;
  char esp_now_mac[18];
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
// void checkSystemHealth() {
//     static unsigned long lastCheck = 0;
//     if(millis() - lastCheck > 30000) { // ทุก 30 วินาที
//         Serial.printf("Free Heap: %d\n", ESP.getFreeHeap());
//         Serial.printf("Heap Fragmentation: %d%%\n", ESP.getHeapFragmentation());
//         Serial.printf("Max Free Block: %d\n", ESP.getMaxFreeBlockSize());
//         lastCheck = millis();
//     }
// }

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
      request->send(200, F("text/html"), "<center><br>⚠️error⚠️<h1>ESP8266 Config</h1><p>index.html not found in LittleFS "+(String)duration+"</p></center>");
    }
  });

  server.on("/save_config", HTTP_GET,[](AsyncWebServerRequest *request){
    save_Config(request);
  });

  // จัดการ request ที่ไม่ตรงกับ route ใดๆ
  server.onNotFound([](AsyncWebServerRequest *request){    
    request->send(404, F("text/plain"), "Not Found "+request->url());
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
  
 // ใช้ strcpy() เพื่อคัดลอก string ไปยัง char array
  strcpy(cfg.id, doc["id"] | "");
  strcpy(cfg.ssid, doc["ssid"] | "");
  strcpy(cfg.password, doc["password"] | "");
  strcpy(cfg.url_api, doc["url"] | "");
  cfg.discord_api = doc["discord"] | "";
  strcpy(cfg.esp_now_mac, doc["esp_now_mac"] | "");

  // แปลง IP เป็น string แล้ว copy ลง char array 
  String ipStr = WiFi.localIP().toString();
  strncpy(cfg.ip, ipStr.c_str(), sizeof(cfg.ip));
  cfg.ip[sizeof(cfg.ip) - 1] = '\0'; // ปิดท้ายด้วย null

  return true;
}


void connectAP(){
  if(cfg.ssid != ""){
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid, cfg.password);
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

server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
  String html;
  html += F("<!DOCTYPE html><html><head>");
  html += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  html += F("<meta charset='UTF-8'>");
  html += F("<style>");
  html += F("body{font-family:Arial;margin:20px;background:#f0f0f0}");
  html += F(".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}");
  html += F("h1{color:#333;text-align:center;margin-bottom:30px}");
  html += F(".status-item{padding:10px;margin:10px 0;background:#f9f9f9;border-left:4px solid #4CAF50;border-radius:4px}");
  html += F(".label{font-weight:bold;color:#555}");
  html += F(".value{color:#333;float:right}");
  html += F(".btn{display:block;width:100%;padding:15px;margin:20px 0;font-size:16px;font-weight:bold;");
  html += F("color:white;background:#4CAF50;border:none;border-radius:5px;cursor:pointer;transition:0.3s}");
  html += F(".btn:hover{background:#45a049}");
  html += F(".btn:active{transform:scale(0.98)}");
  html += F("#result{margin-top:20px;padding:15px;border-radius:5px;display:none;text-align:center;font-weight:bold}");
  html += F(".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}");
  html += F(".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}");
  html += F("</style></head><body>");
  
  html += F("<div class='container'>");
  html += F("<h1>🔧 Sensor Status</h1>");
  
  html += F("<div class='status-item'><span class='label'>ID:</span><span class='value'>");
  html += cfg.id;
  html += F("</span></div>");

  html += F("<div class='status-item'><span class='label'>Sensor:</span><span class='value'>");
  html += digitalRead(SENSOR_PIN) ? F("🟢 Normal") : F("🚨 Emergency");
  html += F("</span></div>");
  

  html += F("<div class='status-item'><span class='label'>WiFi:</span><span class='value'>");
  html += WiFi.isConnected() ? F("✅ Connected") : F("❌ Disconnected");
  html += F("</span></div>");
  
  html += F("<div class='status-item'><span class='label'>IP Address:</span><span class='value'>");
  html += WiFi.localIP().toString();
  html += F("</span></div>");
  
  html += F("<div class='status-item'><span class='label'>Signal (RSSI):</span><span class='value'>");
  html += String(WiFi.RSSI()) + F(" dBm</span></div>");
  
  html += F("<div class='status-item'><span class='label'>Free Heap:</span><span class='value'>");
  html += String(ESP.getFreeHeap()) + F(" bytes</span></div>");
  
  html += F("<div class='status-item'><span class='label'>Free Block:</span><span class='value'>");
  html += String(ESP.getMaxFreeBlockSize()) + F(" bytes</span></div>");
  
  html += F("<div class='status-item'><span class='label'>Heap Fragmentation:</span><span class='value'>");
  html += String(ESP.getHeapFragmentation()) + F(" %</span></div>");
  
  html += F("<div class='status-item'><span class='label'>Uptime:</span><span class='value'>");
  html += String(millis()/1000) + F(" seconds</span></div>");
  
  html += F("<button class='btn' onclick='sendTest()'>📤 Send Test Message to Discord</button>");
  html += F("<div id='result'></div>");
  html += F("</div>");
  
  html += F("<script>");
  html += F("function sendTest(){");
  html += F("document.getElementById('result').style.display='none';");
  html += F("fetch('/test')");
  html += F(".then(response => response.text())");
  html += F(".then(data => {");
  html += F("var resultDiv = document.getElementById('result');");
  html += F("resultDiv.className = 'success';");
  html += F("resultDiv.textContent = '✅ ' + data;");
  html += F("resultDiv.style.display = 'block';");
  html += F("setTimeout(() => location.reload(), 2000);");
  html += F("})");
  html += F(".catch(error => {");
  html += F("var resultDiv = document.getElementById('result');");
  html += F("resultDiv.className = 'error';");
  html += F("resultDiv.textContent = '❌ Error: ' + error;");
  html += F("resultDiv.style.display = 'block';");
  html += F("});}");
  html += F("</script>");
  
  html += F("</body></html>");
  
  request->send(200, F("text/html"), html);
});


server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
  StaticJsonDocument<256> doc;
  doc["id"] = cfg.id;
  doc["alram"] = digitalRead(SENSOR_PIN);
  // doc["wifi"] = WiFi.isConnected();
  doc["ip"] = WiFi.localIP().toString();
  // doc["rssi"] = WiFi.RSSI();
  // doc["heap"] = ESP.getFreeHeap();

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
});


    server.on("/test", HTTP_GET,[](AsyncWebServerRequest *request){
      discord.test = true;
      api.test = true;
      request->send(200, F("text/plain"), "Message queued");    
    });

     // จัดการ request ที่ไม่ตรงกับ route ใดๆ
  server.onNotFound([](AsyncWebServerRequest *request){    
    request->send(404, F("text/plain"), "Not Found "+request->url());
  });
  // เริ่ม server
  server.begin();
  
  }
}

// String report(){
      
//       String statusMsg = "🔧 **ESP8266 Status Report**\\n";
//       statusMsg += "**ID:** " + cfg.id +"\\n";
//       statusMsg += "**Sensor:** " + String(digitalRead(SENSOR_PIN) ? "🟢 ปกติ":"🔴 ฉุกเฉิน")+"\\n";
//       statusMsg += "**WiFi:** " + String(WiFi.isConnected() ? "Connected" : "Disconnected") + "\\n";
//       statusMsg += "**IP Address:** " + WiFi.localIP().toString() + "\\n";
//       statusMsg += "**Signal (RSSI):** " + String(WiFi.RSSI()) + " dBm\\n";
//       statusMsg += "**Free Heap:** " + String(ESP.getFreeHeap()) + " bytes\\n";
//       statusMsg += "**Uptime:** " + String(millis()/1000) + " seconds\\n";
//       return statusMsg;
// }

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
  String id           = request->getParam("id")->value();  
  String ssid         = request->getParam("ssid")->value();  
  String password     = request->getParam("password")->value();
  String url_api      = request->getParam("url")->value();
  String discord_api  = "";
  String esp_now_mac  = "";
  
    
  if (request->hasParam("discord")){
    discord_api = request->getParam("discord")->value();    
  }
  if (request->hasParam("esp_now_mac")){
    esp_now_mac  = request->getParam("esp_now_mac")->value();    
  }
  

  File file = LittleFS.open("/config.json","w");
  if(!file){
    return request->send(400, F("application/json"), F("{\"error\":\"Failed to create new config file\"}"));
  }

  // ใช้ ArduinoJson
  StaticJsonDocument<256> doc;
  doc["id"]           = id;
  doc["ssid"]         = ssid;
  doc["password"]     = password;
  doc["url_api"]      = url_api;  
  doc["discord_api"]  = discord_api;
  doc["esp_now_mac"]  = esp_now_mac;
  // เขียน JSON ลงไฟล์
  if(serializeJson(doc, file) == 0){
    file.close();
    return request->send(500, F("application/json"), F("{\"error\":\"Failed to write to file\"}"));
  }
  file.close();
  request->send(200, F("application/json"), F("{\"success\":\"Configuration saved successfully\"}"));

}

bool sendDiscordReport() {
  if(WiFi.status() != WL_CONNECTED){
    return false;
  }  
  snprintf(jsonPayload, JSON_BUFFER_SIZE,
    "{\"content\":\"🔧 **ESP8266 Status Report**\\n"
    "**ID:** %s\\n"
    "**Sensor:** %s\\n"
    "**WiFi:** %s\\n"
    "**IP Address:** %s\\n"
    "**Signal (RSSI):** %d dBm\\n"
    "**Free Heap:** %d bytes\\n"
    "**Uptime:** %lu seconds\"}",
    cfg.id,
    digitalRead(SENSOR_PIN) ? "🟢 ปกติ" : "🔴 ฉุกเฉิน",
    WiFi.isConnected() ? "Connected" : "Disconnected",
    WiFi.localIP().toString().c_str(),
    WiFi.RSSI(),
    ESP.getFreeHeap(),
    millis() / 1000
  );

  WiFiClientSecure client;
  client.setInsecure(); // ใช้แบบไม่ตรวจสอบ certificate (ง่าย แต่ไม่ปลอดภัยที่สุด)

  HTTPClient https;
  https.begin(client, cfg.discord_api);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("User-Agent", "ESP-Discord-Bot");
  int httpCode = https.POST(jsonPayload);
  https.end();  
  
  if (httpCode > 0) { // ส่งข้อความไปยัง discord สำเร็จ
    Serial.println("✅ Discord report sent successfully.");
    return true;
  } else {
    Serial.println("❌ Failed to send Discord report. Code: " + String(httpCode));
    return false;
  }
  return false;
}

bool sendMessageToDiscord(String msg = ""){
  if(WiFi.status() != WL_CONNECTED){
    return false;
  } 
 
    WiFiClientSecure *client = new WiFiClientSecure();
    client->setInsecure(); // ไม่ตรวจสอบ SSL certificate 
    HTTPClient http;

    if(http.begin(*client, cfg.discord_api)){
      http.addHeader(F("Content-Type"), F("application/json"));
      http.addHeader(F("User-Agent"), F("ESP-Discord-Bot"));
      http.setTimeout(2000);
      String jsonPayload = "{\"content\": \"" + msg + "\"}";
      ESP.wdtFeed(); // เพื่อป้องกันรีเซ็ต
      yield(); // ให้ระบบพื้นฐานทำงาน เช่น WiFi 
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


bool quickServerCheck() {
 
    String hostname = cfg.url_api;
    
    // ทำความสะอาด URL อย่างรวดเร็ว
    if(hostname.startsWith("http://")) hostname = hostname.substring(7);
    else if(hostname.startsWith("https://")) hostname = hostname.substring(8);
    
    int slashPos = hostname.indexOf('/');
    if(slashPos > 0) hostname = hostname.substring(0, slashPos);
    
    WiFiClient client;
    client.setTimeout(800); // ✅ timeout 0.8 วินาที    
    yield(); // ให้ระบบพื้นฐานทำงาน เช่น WiFi
    return client.connect(hostname.c_str(), 80); // ✅ เช็คแค่ port 80
}

bool sendDataToAPI() {  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wifi Not connected");
    return false;
  }
  IPAddress localIp = WiFi.localIP();
  char* playload =  bufferPool[currentIndex];
  // เคลียร์ buffer ก่อนใช้งาน (ป้องกันข้อมูลค้าง)
  memset(playload, 0, BUFFER_SIZE_API);
  // สร้าง payload JSON ลงใน buffer
  snprintf(playload, BUFFER_SIZE_API, "{\"id\":%d,\"alram\":%d,\"ip\":\"%d.%d.%d.%d\"}", 
           cfg.id, 
           digitalRead(SENSOR_PIN), 
           localIp[0], 
           localIp[1], 
           localIp[2], 
           localIp[3]);
  Serial.println(playload);
  HTTPClient http;
  WiFiClient client;  
  unsigned long startTime = millis();
  
  if(strncmp(cfg.url_api, "http",4) == 0) {    
    http.begin(client, cfg.url_api);
  }else{
    http.end();  
    yield(); // ให้ระบบพื้นฐานทำงาน เช่น WiFi  
    sendMessageToDiscord("⚠️ Error URL ไม่ถูกต้อง ❌️ "+String(cfg.url_api));
    return false;
  }
  
  http.addHeader(F("Content-Type"), F("application/json"));  
  ESP.wdtFeed(); // เพื่อป้องกันรีเซ็ต   
  int httpResponseCode = http.POST(playload);
  if (httpResponseCode > 0){
    // สามารถส่งไปยังเซิร์พเวอร์ได้นะ
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    http.end(); 
    return true;  
  }else{
    // Error on sending POST. Code:
    ESP.wdtFeed(); // ✅ Reset watchdog หลังส่ง
    yield(); // ให้ระบบพื้นฐานทำงาน เช่น WiFi
    http.end();  
    
    Serial.print("Error on sending POST. Code ");
    Serial.println(httpResponseCode);    
    
    return false;
  }
  // วนไปใช้ buffer ถัดไป
  currentIndex = (currentIndex + 1) % BUFFER_COUNT_API;
  
  return false;
}


void loop() {
  
  unsigned long now = millis();  
  ESP.wdtFeed();  // reset timer 
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
  // for Local API ทุกๆ 2 วินาที
  if (now - lastApiSend > 2000) {
    if(api.trigger){
      if(quickServerCheck()){        
        if(sendDataToAPI()){
          api.test = true;
        }       
        ESP.wdtFeed();  // reset timer    
      }else{
        Serial.println("❌ API Server OFFLINE");
      }
    }
    //ส่ง 1 ครั้งเพื่อปิด Siren
    if(api.test && SENSOR_STAT){ 
      Serial.println(!api.test && SENSOR_STAT);
      // api.test = true; 
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
      if(sendDiscordReport()){
        discord.test = true;
      }   
      ESP.wdtFeed();  // reset timer
    }
    //ส่ง 1 ครั้ง Discord
    if(discord.test && SENSOR_STAT){       
      discord.maxRequest = 5; 
    }
    lastDiscordSend = now;
  }

  //-----------------------------------------------------------------------
//  ส่งข้อความไปยังยัง Discord 1 ครั้ง
  if(discord.test && SENSOR_STAT){
    if(discord.maxRequest == 0) return;
    if(sendDiscordReport()){
      discord.test = false;
      Serial.println("TEST Discord");
    }     
    discord.maxRequest --;  
    ESP.wdtFeed();  // reset timer 
  }
//-----------------------------------------------------------------------
  //  ส่งข้อความไปยังยัง Local API 1 ครั้ง
  if(api.test && SENSOR_STAT){
    if(api.maxRequest == 0) return;    
    if(sendDataToAPI()){
      api.test = false;
      Serial.println("TEST Local API");
    } 
    api.maxRequest --;   
    ESP.wdtFeed();  // reset timer
  }
  ESP.wdtFeed();  // reset timer 
}
