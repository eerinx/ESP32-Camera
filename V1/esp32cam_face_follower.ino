/*
  ESP32-CAM (AI-Thinker board, OV5640 sensor) — Face Follower Robot
  ------------------------------------------------------------------
  What this sketch does:
    1. Connects to WiFi.
    2. Starts an MJPEG video stream on:  http://<esp32-ip>:81/stream
    3. Starts a motor-control HTTP endpoint on:
         http://<esp32-ip>/motor?dir=forward
         http://<esp32-ip>/motor?dir=backward
         http://<esp32-ip>/motor?dir=left
         http://<esp32-ip>/motor?dir=right
         http://<esp32-ip>/motor?dir=stop
    4. Drives an L298N motor driver in ON/OFF direction mode
       (ENA/ENB are wired straight to 5V, so motors always run at
       full speed — only direction is controlled from the ESP32).

  Board setting in Arduino IDE:
    Tools > Board > AI Thinker ESP32-CAM  (or "ESP32 Wrover Module"
    with PSRAM enabled if AI Thinker isn't listed)
    Tools > Partition Scheme > Huge APP (3MB No OTA) recommended

  Wiring recap:
    L298N IN1 -> GPIO 13
    L298N IN2 -> GPIO 12
    L298N IN3 -> GPIO 14
    L298N IN4 -> GPIO 15
    L298N ENA -> tied directly to 5V (NOT to the ESP32)
    L298N ENB -> tied directly to 5V (NOT to the ESP32)
    L298N GND -> common GND with ESP32 GND  (IMPORTANT)
    L298N 12V input -> boost converter output (9-12V) from powerbank
    ESP32 5V  -> powerbank (direct 5V line, separate from motor rail)

  Notes:
    - GPIO12 is a strapping pin. It is only touched by this sketch
      AFTER setup() (i.e., after boot), so it will not interfere
      with boot mode selection.
    - Fill in your WiFi SSID/password below before flashing.
*/

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include <WiFi.h>

// ==================== USER CONFIG ====================
const char *WIFI_SSID     = "erinx free WiFi";
const char *WIFI_PASSWORD = "@Carleli015";
// =======================================================

// ---------- AI-Thinker ESP32-CAM pin map (works for OV5640 too) ----------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ---------- Motor pins (free GPIOs on ESP32-CAM) ----------
#define IN1_PIN 13
#define IN2_PIN 12
#define IN3_PIN 14
#define IN4_PIN 15

// ---------- HTTP server handles ----------
httpd_handle_t stream_httpd = NULL;
httpd_handle_t control_httpd = NULL;

// ================= Motor control =================
void motorStop() {
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);
}

void motorForward() {
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, HIGH);
  digitalWrite(IN4_PIN, LOW);
}

void motorBackward() {
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, HIGH);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, HIGH);
}

// Pivot turns: one side forward, other side backward.
void motorLeft() {
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, HIGH);
  digitalWrite(IN3_PIN, HIGH);
  digitalWrite(IN4_PIN, LOW);
}

void motorRight() {
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, HIGH);
}

void setupMotorPins() {
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);
  motorStop();
}

// ================= /motor HTTP handler =================
static esp_err_t motor_handler(httpd_req_t *req) {
  char query[64];
  char dir[16] = {0};

  if (httpd_req_get_url_query_len(req) > 0 &&
      httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    httpd_query_key_value(query, "dir", dir, sizeof(dir));
  }

  String d = String(dir);
  String reply = "OK: ";

  if (d == "forward") { motorForward();  reply += "forward"; }
  else if (d == "backward") { motorBackward(); reply += "backward"; }
  else if (d == "left") { motorLeft();     reply += "left"; }
  else if (d == "right") { motorRight();    reply += "right"; }
  else if (d == "stop") { motorStop();     reply += "stop"; }
  else {
    motorStop();
    reply = "ERROR: unknown or missing 'dir' param. Use forward/backward/left/right/stop.";
    httpd_resp_set_status(req, "400 Bad Request");
  }

  httpd_resp_set_type(req, "text/plain");
  httpd_resp_send(req, reply.c_str(), reply.length());
  return ESP_OK;
}

// ================= /stream HTTP handler (MJPEG) =================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }

    if (res != ESP_OK) break;
  }
  return res;
}

// ================= Server startup =================
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t motor_uri = {
    .uri       = "/motor",
    .method    = HTTP_GET,
    .handler   = motor_handler,
    .user_ctx  = NULL
  };

  config.server_port += 0;
  if (httpd_start(&control_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(control_httpd, &motor_uri);
  }

  httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
  stream_config.server_port = 81;
  stream_config.ctrl_port = 32769; // must differ from the default control server

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&stream_httpd, &stream_config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// ================= Camera init =================
bool setupCamera() {
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;   // 640x480 — good balance for face detection
    config.jpeg_quality = 12;            // lower number = higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;  // 320x240 fallback if no PSRAM
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  // OV5640-specific tweaks (safe to leave defaults if this sensor is not OV5640)
  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV5640_PID) {
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
  }

  return true;
}

// ================= Arduino entry points =================
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  setupMotorPins();

  if (!setupCamera()) {
    Serial.println("Camera setup failed. Halting.");
    while (true) delay(1000);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();

  Serial.print("Camera stream:  http://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/stream");
  Serial.print("Motor control:  http://");
  Serial.print(WiFi.localIP());
  Serial.println("/motor?dir=forward|backward|left|right|stop");
}

void loop() {
  // Everything is handled by the HTTP server tasks; nothing needed here.
  delay(10000);
}
