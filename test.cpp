// Isahang code - OV5640 Camera Live Stream (OPTIMIZED for low latency)
// AI Thinker ESP32-CAM pinout

#include "esp_camera.h"
#include <WiFi.h>

// Pin definitions para sa AI Thinker ESP32-CAM board
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

const char* ssid = "erinx free WiFi";
const char* password = "@Carleli015";

WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Initializing camera...");

  // Mas mataas na CPU speed para mas mabilis mag-process
  setCpuFrequencyMhz(240);

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
  config.grab_mode = CAMERA_GRAB_LATEST;  // laging kunin ang PINAKABAGONG frame, hindi yung nakapila

  // Mas maliit na resolution = mas mabilis, mas kaunting delay
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;    // 640x480 - balanced speed/quality
    config.jpeg_quality = 15;             // mas mataas na number = mas maliit na file = mas mabilis
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;   // 320x240 - pinaka-mabilis
    config.jpeg_quality = 18;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED with error 0x%x\n", err);
    return;
  }
  Serial.println("Camera init SUCCESS!");

  sensor_t * s = esp_camera_sensor_get();
  Serial.printf("Detected sensor PID: 0x%x\n", s->id.PID);

  // MAHALAGA: i-off ang WiFi power-saving mode - ito ang pinaka-malaking
  // cause ng delay/lag sa ESP32 streaming. Nagpapatulog ang modem para
  // makatipid ng battery, pero nagdudulot ito ng malaking latency.
  WiFi.setSleep(false);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! Open stream sa browser: http://");
  Serial.println(WiFi.localIP());

  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  client.setNoDelay(true);  // i-disable ang Nagle's algorithm - direktang ipadala
                             // ang data imbes na hintayin mag-buffer pa

  Serial.println("New client connected");

  // Isang malaking write imbes na maraming maliliit na println
  // (bawat println ay separate network packet, may overhead)
  client.print("HTTP/1.1 200 OK\r\n"
               "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      break;
    }

    // Header + image sa mas kaunting writes
    char header[128];
    int headerLen = snprintf(header, sizeof(header),
                              "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                              fb->len);
    client.write((const uint8_t*)header, headerLen);
    client.write(fb->buf, fb->len);
    client.write((const uint8_t*)"\r\n", 2);

    esp_camera_fb_return(fb);

    if (!client.connected()) break;
  }

  client.stop();
  Serial.println("Client disconnected");
}