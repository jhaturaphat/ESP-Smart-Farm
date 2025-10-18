#include <WiFi.h>
// #include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#define siren_PIN 2

const char* ssid = "kid_2.4GHz";
const char* password = "xx3xx3xx";

bool siren = false;


AsyncWebServer server(80);

IPAddress local_IP(192, 168, 1, 90);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(siren_PIN, OUTPUT);
  // ต้อง config ก่อน begin
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }
  
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
    return;
  }
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Hello from ESP32 Async Web Server!");
  });
  
  server.on("/api", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, processing);
  
  server.begin();
  Serial.println("Server started");
}

void processing(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  // ตรวจสอบว่าได้รับข้อมูลครบหรือยัง
  if (index + len != total) {
    return; // รอให้ได้รับข้อมูลครบ
  }
  
  Serial.printf("Received data length: %d bytes\n", len);
  
  // จัดสรรหน่วยความจำเพิ่ม
  DynamicJsonDocument doc(256);
  
  // Parse JSON โดยตรงจาก buffer
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  // ตรวจสอบว่ามี field ที่ต้องการหรือไม่
  if (!doc.containsKey("id") || !doc.containsKey("alram")) {
    request->send(400, "application/json", "{\"error\":\"Missing required fields\"}");
    return;
  }
  
  int id = doc["id"];
  // const char* alram = doc["alram"];
  bool alram = doc["alram"];

  if(!alram){
    siren = true;
  }else{
    siren = false;
  }
  
  Serial.printf("Received JSON - ID: %d, Alram: %d\n", id, alram);
  
  // ส่ง response เป็น JSON
  String response = "{\"status\":\"success\",\"id\":" + String(id) + ",\"alram\":\"" + String(alram) + "\"}";
  request->send(200, "application/json", "OK");
}

void loop() {
  delay(10); // ให้ WDT มีเวลาทำงาน  
  digitalWrite(siren_PIN, !siren);  
}