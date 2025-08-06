#include <WiFi.h>
#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

//  Cấu hình EEPROM
#define EEPROM_SIZE 96
#define ADDR_SSID 0
#define ADDR_PASS 32

//  Gemini API Key
const char* gemini_api_key = "API-Key";
//  Gemini API Key lấy từ: https://aistudio.google.com/app/apikey

//  Bộ đệm nhập từ Serial
char inputBuffer[2048];
size_t inputPos = 0;

String ssid = "";
String password = "";

//  Lưu SSID + password vào EEPROM
void saveWiFiToEEPROM(String ssid, String pass) {
  for (int i = 0; i < 32; i++) {
    EEPROM.write(ADDR_SSID + i, i < ssid.length() ? ssid[i] : 0);
    EEPROM.write(ADDR_PASS + i, i < pass.length() ? pass[i] : 0);
  }
  EEPROM.commit();
}

//  Tải lại SSID và password từ EEPROM (có kiểm tra hợp lệ)
void loadWiFiFromEEPROM() {
  char s[33], p[33];
  for (int i = 0; i < 32; i++) {
    s[i] = EEPROM.read(ADDR_SSID + i);
    p[i] = EEPROM.read(ADDR_PASS + i);
  }
  s[32] = '\0';
  p[32] = '\0';
  ssid = String(s);
  password = String(p);

  // Kiểm tra ký tự rác (nếu EEPROM chưa từng ghi)
  if (ssid.length() == 0 || ssid.indexOf('\xff') != -1 || ssid.indexOf('?') != -1) {
    ssid = "";
    password = "";
  }
}

//  Quét Wi-Fi và chọn mạng
void scanAndSelectWiFi() {
  WiFi.disconnect(true);         // Bắt buộc xóa config trước khi scan
  WiFi.mode(WIFI_STA);
  delay(200);

  Serial.println("\n Đang quét Wi-Fi...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println(" Không tìm thấy mạng Wi-Fi nào.");
    return;
  }

  Serial.printf(" Tìm thấy %d mạng:\n", n);
  for (int i = 0; i < n; i++) {
    String secure = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "🔒";
    Serial.printf("  [%d] %-25s RSSI: %3d  %s\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), secure.c_str());
  }

  Serial.println(" Nhập số mạng Wi-Fi muốn chọn:");
  while (!Serial.available()) delay(10);
  int index = Serial.parseInt();
  Serial.read();

  if (index < 0 || index >= n) {
    Serial.println(" Số không hợp lệ!");
    return;
  }

  ssid = WiFi.SSID(index);
  Serial.println(" Đã chọn mạng: " + ssid);

  Serial.println(" Nhập mật khẩu Wi-Fi:");
  while (!Serial.available()) delay(10);
  password = Serial.readStringUntil('\n');
  password.trim();

  saveWiFiToEEPROM(ssid, password);
  Serial.println(" Đã lưu vào EEPROM.");
}

//  Kết nối Wi-Fi
void connectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  delay(100);

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("🔌 Đang kết nối tới " + ssid);
  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count++ < 20) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n Kết nối Wi-Fi thành công!");
    Serial.println(" IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n Không kết nối được Wi-Fi.");
    scanAndSelectWiFi();             // Cho người dùng chọn lại
    loadWiFiFromEEPROM();            // Load lại sau khi lưu
    connectWiFi();                   // Thử lại
  }
}

// 🤖 Gửi câu hỏi tới Gemini
void askGemini(const String& question) {
  Serial.println("\n Câu hỏi: " + question);
  Serial.println("────────────────────────────────────────────────────────────────────────────────────────────");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://generativelanguage.googleapis.com/v1/models/gemini-2.0-flash:generateContent?key=" + String(gemini_api_key);
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  DynamicJsonDocument doc(4096);
  JsonArray contents = doc.createNestedArray("contents");
  JsonObject msg = contents.createNestedObject();
  JsonArray parts = msg.createNestedArray("parts");
  parts.createNestedObject()["text"] = question;

  String body;
  serializeJson(doc, body);
  Serial.println(" Đang gửi...");

  int code = http.POST(body);

  if (code > 0) {
    String response = http.getString();
    DynamicJsonDocument res(8192);
    DeserializationError err = deserializeJson(res, response);

    if (!err) {
      JsonVariant text = res["candidates"][0]["content"]["parts"][0]["text"];
      if (!text.isNull()) {
        Serial.println("\n AI trả lời:");
        Serial.println("────────────────────────────────────────────────────────────────────────────────────────────");
        Serial.println(text.as<String>());
        Serial.println("════════════════════════════════════════════════════════════════════════════════════════════");
      } else {
        Serial.println(" Không có nội dung phản hồi từ AI.");
      }
    } else {
      Serial.println(" JSON lỗi: " + String(err.c_str()));
    }
  } else {
    Serial.println(" HTTP lỗi: " + String(code));
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  loadWiFiFromEEPROM();

  if (ssid == "" || password == "") {
    scanAndSelectWiFi();
    loadWiFiFromEEPROM();
  }

  connectWiFi();
  Serial.println(" Gõ câu hỏi và nhấn Enter:");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      inputBuffer[inputPos] = '\0';
      if (inputPos > 0) {
        askGemini(String(inputBuffer));
      }
      inputPos = 0;
      Serial.println(" Gõ câu hỏi và nhấn Enter:");
    } else {
      if (inputPos < sizeof(inputBuffer) - 1) {
        inputBuffer[inputPos++] = c;
      } else {
        Serial.println("\n Câu hỏi quá dài!");
        inputPos = 0;
      }
    }
  }
}
