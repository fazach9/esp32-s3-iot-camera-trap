
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "SD_MMC.h"
#include "time.h"

// userconfig
const char* WIFI_SSID     = "ISI_NAMA_WIFI_MIFI";
const char* WIFI_PASSWORD = "ISI_PASSWORD_MIFI";

const char* BOT_TOKEN = "ISI_BOT_TOKEN_TELEGRAM";
const char* CHAT_ID   = "ISI_CHAT_ID_TELEGRAM";

// PIR OUT diarahkan ke GPIO42.
#define PIR_PIN 42

// threshold minimum 50%.
const float CONFIDENCE_THRESHOLD = 0.50f;

// 3-5 frame berurutan.
const uint8_t CAPTURE_COUNT = 3;
const uint32_t CAPTURE_INTERVAL_MS = 350;

// Cooldown agar tidak membanjiri Telegram.
const uint32_t EVENT_COOLDOWN_MS = 15000;

// Retry koneksi/pengiriman.
const uint8_t MAX_RETRY = 3;

// camerapin

#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

#define Y2_GPIO_NUM      11
#define Y3_GPIO_NUM       9
#define Y4_GPIO_NUM       8
#define Y5_GPIO_NUM      10
#define Y6_GPIO_NUM      12
#define Y7_GPIO_NUM      18
#define Y8_GPIO_NUM      17
#define Y9_GPIO_NUM      16

#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

// sdpin
#define SD_CLK 39
#define SD_CMD 38
#define SD_D0  40

// global
unsigned long lastEventMillis = 0;
uint32_t eventNumber = 0;

// helper
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) {
    return "1970-01-01 00:00:00";
  }

  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

String getFileTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) {
    return String(millis());
  }

  char buf[32];
  strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &timeinfo);
  return String(buf);
}

// wifi
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.printf("[WiFi] Connecting to MiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  for (uint8_t i = 0; i < 30; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] Connected");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("[WiFi] RSSI: ");
      Serial.println(WiFi.RSSI());
      return true;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connection failed");
  return false;
}

// camera
bool cameraReady = false;
bool sdReady = false;

bool initCamera() {
  if (cameraReady) return true;
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  //AI
  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA; // 800x600
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  Serial.println("[CAM] Initializing OV5640...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init FAILED: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // Pengaturan
    s->set_framesize(s, FRAMESIZE_SVGA);
    s->set_quality(s, 10);
  }

  cameraReady = true;
  Serial.println("[CAM] OV5640 initialized");
  Serial.printf("[CAM] PSRAM: %s\n", psramFound() ? "YES" : "NO");
  return true;
}

// sdcard
bool initSD() {
  if (sdReady) return true;
  Serial.println("[SD] Initializing...");

  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) {
    Serial.println("[SD] setPins failed");
    return false;
  }

  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("[SD] Mount failed");
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[SD] No card detected");
    return false;
  }

  uint64_t cardMB = SD_MMC.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("[SD] OK, size: %llu MB\n", cardMB);
  sdReady = true;
  return true;
}

// aiinterface
struct AIResult {
  bool valid;
  String label;
  float confidence;
};


AIResult runAIValidation(const uint8_t* jpg, size_t jpgLen) {
  (void)jpg;
  (void)jpgLen;

  AIResult r;
  r.valid = false;
  r.label = "MODEL_NOT_LOADED";
  r.confidence = 0.0f;
  return r;
}

//filesave
String saveFrame(camera_fb_t* fb, uint8_t index) {
  String path = "/event_" + getFileTimestamp() + "_" + String(index) + ".jpg";

  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("[SD] Failed open: %s\n", path.c_str());
    return "";
  }

  size_t written = file.write(fb->buf, fb->len);
  file.close();

  if (written != fb->len) {
    Serial.printf("[SD] Incomplete write: %u/%u\n", (unsigned)written, (unsigned)fb->len);
    SD_MMC.remove(path);
    return "";
  }

  Serial.printf("[SD] Saved: %s (%u bytes)\n", path.c_str(), (unsigned)fb->len);
  return path;
}

void deleteFileIfExists(const String& path) {
  if (path.length() && SD_MMC.exists(path)) {
    SD_MMC.remove(path);
    Serial.printf("[SD] Deleted: %s\n", path.c_str());
  }
}

//telegram
String urlEncode(const String& input) {
  const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(input.length() * 2);

  for (size_t i = 0; i < input.length(); i++) {
    uint8_t c = (uint8_t)input[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

bool sendTelegramPhoto(const String& path, const String& caption) {
  if (BOT_TOKEN[0] == '\0' || CHAT_ID[0] == '\0') {
    Serial.println("[TG] BOT_TOKEN/CHAT_ID not configured");
    return false;
  }

  if (!SD_MMC.exists(path)) {
    Serial.println("[TG] File does not exist");
    return false;
  }

  File photo = SD_MMC.open(path, FILE_READ);
  if (!photo) {
    Serial.println("[TG] Failed to open photo");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Prototype
  client.setTimeout(15000);

  String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendPhoto";
  const String boundary = "----CameraTrapBoundary7MA4YWxkTrZu0gW";

  for (uint8_t attempt = 1; attempt <= MAX_RETRY; attempt++) {
    Serial.printf("[TG] Upload attempt %u/%u\n", attempt, MAX_RETRY);

    if (!client.connect("api.telegram.org", 443)) {
      Serial.println("[TG] TLS connection failed");
      delay(500 * attempt);
      continue;
    }

    String head = "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
    head += String(CHAT_ID) + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"caption\"\r\n\r\n";
    head += caption + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"photo\"; filename=\"camera_trap.jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";
    size_t contentLength = head.length() + photo.size() + tail.length();

    client.printf("POST /bot%s/sendPhoto HTTP/1.1\r\n", BOT_TOKEN);
    client.println("Host: api.telegram.org");
    client.println("User-Agent: CameraTrap-ESP32S3");
    client.println("Connection: close");
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.printf("Content-Length: %u\r\n", (unsigned)contentLength);
    client.println();

    client.print(head);

    uint8_t buffer[1024];
    size_t remaining = photo.size();
    while (remaining > 0) {
      size_t n = photo.read(buffer, min((size_t)sizeof(buffer), remaining));
      if (n == 0) break;
      client.write(buffer, n);
      remaining -= n;
    }

    client.print(tail);
    photo.seek(0);

    unsigned long start = millis();
    while (client.connected() && millis() - start < 15000) {
      if (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/1.1")) {
          Serial.println("[TG] " + line);
          bool ok = line.indexOf("200") >= 0;
          client.stop();
          if (ok) {
            Serial.println("[TG] Photo sent successfully");
            return true;
          }
          break;
        }
      }
      delay(10);
    }

    client.stop();
    delay(500 * attempt);
  }

  Serial.println("[TG] All retries failed");
  return false;
}

// event
void processEvent() {
  unsigned long eventStart = millis();
  eventNumber++;

  Serial.println();
  Serial.println("========================================");
  Serial.printf("EVENT #%lu\n", (unsigned long)eventNumber);
  Serial.println("PIR DETECTED");
  Serial.println("Time: " + getTimestamp());

  if (!initCamera()) {
    Serial.println("[EVENT] Camera unavailable");
    return;
  }

  if (!initSD()) {
    Serial.println("[EVENT] SD unavailable");
    return;
  }

  for (uint8_t i = 1; i <= CAPTURE_COUNT; i++) {
    Serial.printf("[CAM] Capture %u/%u\n", i, CAPTURE_COUNT);

    unsigned long captureStart = millis();
    camera_fb_t* fb = esp_camera_fb_get();
    unsigned long captureMs = millis() - captureStart;

    if (!fb) {
      Serial.println("[CAM] Capture failed");
      delay(CAPTURE_INTERVAL_MS);
      continue;
    }

    Serial.printf("[CAM] JPEG size: %u bytes, capture: %lu ms\n",
                  (unsigned)fb->len, captureMs);

    // AI JPEG
    AIResult result = runAIValidation(fb->buf, fb->len);

    Serial.printf("[AI] class=%s confidence=%.1f%% valid=%s\n",
                  result.label.c_str(),
                  result.confidence * 100.0f,
                  result.valid ? "YES" : "NO");

    String path = saveFrame(fb, i);
    esp_camera_fb_return(fb);

    if (path.length() == 0) {
      delay(CAPTURE_INTERVAL_MS);
      continue;
    }

    // AI-validated 
    if (!result.valid || result.confidence < CONFIDENCE_THRESHOLD) {
      deleteFileIfExists(path);
      Serial.println("[FILTER] Non-target / model not loaded -> discarded");
    } else {
      String caption =
        "Camera Trap Gunung Muria\n" +
        String("Objek: ") + result.label + "\n" +
        String("Confidence: ") + String(result.confidence * 100.0f, 1) + "%\n" +
        String("Waktu: ") + getTimestamp();

      if (connectWiFi()) {
        sendTelegramPhoto(path, caption);
      } else {
        Serial.println("[OFFLINE] Image retained locally");
      }
    }

    delay(CAPTURE_INTERVAL_MS);
  }

  Serial.printf("[EVENT] Total processing time: %lu ms\n", millis() - eventStart);
  Serial.println("========================================");
}

// setup
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" CAMERA TRAP IoT - GUNUNG MURIA");
  Serial.println(" ESP32-S3 N16R8 + OV5640 + PIR + SD");
  Serial.println("========================================");

  pinMode(PIR_PIN, INPUT);

  // Initialize camera and SD 
  if (!initCamera()) {
    Serial.println("[BOOT] Camera init failed. Check camera ribbon/pin mapping.");
  }

  if (!initSD()) {
    Serial.println("[BOOT] SD init failed. Check card and SD pins.");
  }

  connectWiFi();

  // UTC+7 (WIB)
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Serial.println("[BOOT] System ready. Waiting for PIR...");
}

// ========================== LOOP =================================
void loop() {
  static int lastPir = LOW;
  int pir = digitalRead(PIR_PIN);

  // Rising edge only.
  if (pir == HIGH && lastPir == LOW) {
    unsigned long now = millis();

    if (now - lastEventMillis >= EVENT_COOLDOWN_MS) {
      lastEventMillis = now;
      processEvent();
    } else {
      Serial.println("[PIR] Trigger ignored: cooldown active");
    }
  }

  lastPir = pir;
  delay(50);
}
