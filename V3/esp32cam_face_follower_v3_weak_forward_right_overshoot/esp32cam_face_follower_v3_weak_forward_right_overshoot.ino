/*
  ESP32-CAM Face Follower Robot — V3.1
  =====================================

  Confirmed physical behavior:
  - Old "forward"  -> robot moved BACKWARD
  - Old "backward" -> robot moved FORWARD
  - "left"         -> correctly turns LEFT
  - "right"        -> correctly turns RIGHT

  V3.1 FIX:
  - FORWARD/BACKWARD logic corrected.
  - LEFT/RIGHT logic preserved.
  - ENA and ENB remain connected to 5V.
  - NO PWM yet.
  - Same HTTP API is preserved.

  MOTOR WIRING:
  IN1 -> GPIO 13
  IN2 -> GPIO 12
  IN3 -> GPIO 14
  IN4 -> GPIO 15

  ENA -> 5V
  ENB -> 5V

  HTTP:
  http://<ESP32-IP>/motor?dir=forward
  http://<ESP32-IP>/motor?dir=backward
  http://<ESP32-IP>/motor?dir=left
  http://<ESP32-IP>/motor?dir=right
  http://<ESP32-IP>/motor?dir=stop

  STREAM:
  http://<ESP32-IP>:81/stream
*/

#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include <WiFi.h>


// ======================================================
// WIFI
// ======================================================

const char *WIFI_SSID = "erinx free WiFi";
const char *WIFI_PASSWORD = "@Carleli015";


// ======================================================
// CAMERA PIN MAP
// AI Thinker ESP32-CAM / current working configuration
// ======================================================

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


// ======================================================
// L298N MOTOR PINS
// ======================================================

#define IN1_PIN 13
#define IN2_PIN 12

#define IN3_PIN 14
#define IN4_PIN 15


// ======================================================
// HTTP SERVER
// ======================================================

httpd_handle_t stream_httpd = NULL;
httpd_handle_t control_httpd = NULL;


// ======================================================
// MOTOR CONTROL
// ======================================================

void motorStop() {

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);

  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);
}


// ------------------------------------------------------
// FORWARD
//
// IMPORTANT:
// This is intentionally reversed from your old V2
// because our physical test confirmed that this
// combination actually moves YOUR robot forward.
// ------------------------------------------------------

void motorForward() {

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, HIGH);

  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, HIGH);
}


// ------------------------------------------------------
// BACKWARD
// ------------------------------------------------------

void motorBackward() {

  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);

  digitalWrite(IN3_PIN, HIGH);
  digitalWrite(IN4_PIN, LOW);
}


// ------------------------------------------------------
// LEFT
//
// KEEP THIS EXACTLY AS TESTED.
// Your physical test confirmed that this turns LEFT.
// ------------------------------------------------------

void motorLeft() {

  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, HIGH);

  digitalWrite(IN3_PIN, HIGH);
  digitalWrite(IN4_PIN, LOW);
}


// ------------------------------------------------------
// RIGHT
//
// KEEP THIS EXACTLY AS TESTED.
// Your physical test confirmed that this turns RIGHT.
// ------------------------------------------------------

void motorRight() {

  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);

  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, HIGH);
}


// ======================================================
// MOTOR INITIALIZATION
// ======================================================

void setupMotorPins() {

  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);

  motorStop();

  Serial.println("Motor pins initialized.");
}


// ======================================================
// MOTOR HTTP HANDLER
// ======================================================

static esp_err_t motor_handler(httpd_req_t *req) {

  char query[64];
  char dir[16] = {0};

  if (
      httpd_req_get_url_query_len(req) > 0 &&
      httpd_req_get_url_query_str(
        req,
        query,
        sizeof(query)
      ) == ESP_OK
  ) {

    httpd_query_key_value(
      query,
      "dir",
      dir,
      sizeof(dir)
    );
  }


  String d = String(dir);
  String reply = "OK: ";


  // ---------------- FORWARD ----------------

  if (d == "forward") {

    motorForward();

    reply += "forward";

    Serial.println("[MOTOR] FORWARD");
  }


  // ---------------- BACKWARD ----------------

  else if (d == "backward") {

    motorBackward();

    reply += "backward";

    Serial.println("[MOTOR] BACKWARD");
  }


  // ---------------- LEFT ----------------

  else if (d == "left") {

    motorLeft();

    reply += "left";

    Serial.println("[MOTOR] LEFT");
  }


  // ---------------- RIGHT ----------------

  else if (d == "right") {

    motorRight();

    reply += "right";

    Serial.println("[MOTOR] RIGHT");
  }


  // ---------------- STOP ----------------

  else if (d == "stop") {

    motorStop();

    reply += "stop";

    Serial.println("[MOTOR] STOP");
  }


  // ---------------- INVALID ----------------

  else {

    motorStop();

    reply =
      "ERROR: use "
      "forward/backward/left/right/stop";

    httpd_resp_set_status(
      req,
      "400 Bad Request"
    );
  }


  httpd_resp_set_type(
    req,
    "text/plain"
  );


  httpd_resp_send(
    req,
    reply.c_str(),
    reply.length()
  );


  return ESP_OK;
}


// ======================================================
// CAMERA STREAM
// ======================================================

#define PART_BOUNDARY \
"123456789000000000000987654321"


static const char *_STREAM_CONTENT_TYPE =
  "multipart/x-mixed-replace;boundary="
  PART_BOUNDARY;


static const char *_STREAM_BOUNDARY =
  "\r\n--"
  PART_BOUNDARY
  "\r\n";


static const char *_STREAM_PART =
  "Content-Type: image/jpeg\r\n"
  "Content-Length: %u\r\n\r\n";


// ======================================================
// STREAM HANDLER
// ======================================================

static esp_err_t stream_handler(
  httpd_req_t *req
) {

  camera_fb_t *fb = NULL;

  esp_err_t res = ESP_OK;

  size_t jpgLength = 0;

  uint8_t *jpgBuffer = NULL;

  char partBuffer[64];


  res = httpd_resp_set_type(
    req,
    _STREAM_CONTENT_TYPE
  );


  if (res != ESP_OK) {

    return res;
  }


  while (true) {

    fb = esp_camera_fb_get();


    // --------------------------------------------------
    // CAMERA FAILED
    // --------------------------------------------------

    if (!fb) {

      Serial.println(
        "Camera capture failed"
      );

      res = ESP_FAIL;
    }


    // --------------------------------------------------
    // CAMERA OK
    // --------------------------------------------------

    else {

      if (fb->format != PIXFORMAT_JPEG) {

        bool converted =
          frame2jpg(
            fb,
            80,
            &jpgBuffer,
            &jpgLength
          );


        esp_camera_fb_return(fb);

        fb = NULL;


        if (!converted) {

          Serial.println(
            "JPEG compression failed"
          );

          res = ESP_FAIL;
        }

      }

      else {

        jpgLength = fb->len;
        jpgBuffer = fb->buf;
      }
    }


    // --------------------------------------------------
    // SEND BOUNDARY
    // --------------------------------------------------

    if (res == ESP_OK) {

      res = httpd_resp_send_chunk(
        req,
        _STREAM_BOUNDARY,
        strlen(_STREAM_BOUNDARY)
      );
    }


    // --------------------------------------------------
    // SEND JPEG HEADER
    // --------------------------------------------------

    if (res == ESP_OK) {

      size_t headerLength =
        snprintf(
          partBuffer,
          sizeof(partBuffer),
          _STREAM_PART,
          jpgLength
        );


      res = httpd_resp_send_chunk(
        req,
        partBuffer,
        headerLength
      );
    }


    // --------------------------------------------------
    // SEND JPEG
    // --------------------------------------------------

    if (res == ESP_OK) {

      res = httpd_resp_send_chunk(
        req,
        (const char *)jpgBuffer,
        jpgLength
      );
    }


    // --------------------------------------------------
    // RELEASE FRAME
    // --------------------------------------------------

    if (fb) {

      esp_camera_fb_return(fb);

      fb = NULL;
      jpgBuffer = NULL;
    }

    else if (jpgBuffer) {

      free(jpgBuffer);

      jpgBuffer = NULL;
    }


    if (res != ESP_OK) {

      break;
    }
  }


  return res;
}


// ======================================================
// START HTTP SERVERS
// ======================================================

void startCameraServer() {

  // ----------------------------------------------------
  // MOTOR SERVER — PORT 80
  // ----------------------------------------------------

  httpd_config_t controlConfig =
    HTTPD_DEFAULT_CONFIG();


  controlConfig.server_port = 80;


  httpd_uri_t motorUri = {

    .uri = "/motor",

    .method = HTTP_GET,

    .handler = motor_handler,

    .user_ctx = NULL
  };


  if (
    httpd_start(
      &control_httpd,
      &controlConfig
    ) == ESP_OK
  ) {

    httpd_register_uri_handler(
      control_httpd,
      &motorUri
    );


    Serial.println(
      "Motor HTTP server started."
    );
  }


  // ----------------------------------------------------
  // CAMERA STREAM — PORT 81
  // ----------------------------------------------------

  httpd_config_t streamConfig =
    HTTPD_DEFAULT_CONFIG();


  streamConfig.server_port = 81;

  streamConfig.ctrl_port = 32769;


  httpd_uri_t streamUri = {

    .uri = "/stream",

    .method = HTTP_GET,

    .handler = stream_handler,

    .user_ctx = NULL
  };


  if (
    httpd_start(
      &stream_httpd,
      &streamConfig
    ) == ESP_OK
  ) {

    httpd_register_uri_handler(
      stream_httpd,
      &streamUri
    );


    Serial.println(
      "Camera stream server started."
    );
  }
}


// ======================================================
// CAMERA INITIALIZATION
// ======================================================

bool setupCamera() {

  camera_config_t config = {};


  // ----------------------------------------------------
  // CAMERA CLOCK
  // ----------------------------------------------------

  config.ledc_channel =
    LEDC_CHANNEL_0;

  config.ledc_timer =
    LEDC_TIMER_0;


  // ----------------------------------------------------
  // CAMERA DATA PINS
  // ----------------------------------------------------

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;

  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;


  config.pin_xclk =
    XCLK_GPIO_NUM;

  config.pin_pclk =
    PCLK_GPIO_NUM;

  config.pin_vsync =
    VSYNC_GPIO_NUM;

  config.pin_href =
    HREF_GPIO_NUM;


  config.pin_sscb_sda =
    SIOD_GPIO_NUM;

  config.pin_sscb_scl =
    SIOC_GPIO_NUM;


  config.pin_pwdn =
    PWDN_GPIO_NUM;

  config.pin_reset =
    RESET_GPIO_NUM;


  // ----------------------------------------------------
  // CAMERA SETTINGS
  // ----------------------------------------------------

  config.xclk_freq_hz =
    10000000;


  config.pixel_format =
    PIXFORMAT_JPEG;


  config.frame_size =
    FRAMESIZE_QVGA;


  config.jpeg_quality = 15;


  config.fb_count = 1;


  // ----------------------------------------------------
  // START CAMERA
  // ----------------------------------------------------

  esp_err_t err =
    esp_camera_init(&config);


  if (err != ESP_OK) {

    Serial.printf(
      "Camera init failed: 0x%x\n",
      err
    );

    return false;
  }


  Serial.println(
    "Camera initialization OK"
  );


  // ----------------------------------------------------
  // CAMERA SENSOR INFO
  // ----------------------------------------------------

  sensor_t *sensor =
    esp_camera_sensor_get();


  if (sensor == NULL) {

    Serial.println(
      "ERROR: Camera sensor NULL"
    );

    return false;
  }


  Serial.printf(
    "Camera PID: 0x%02X\n",
    sensor->id.PID
  );


  Serial.printf(
    "Camera VER: 0x%02X\n",
    sensor->id.VER
  );


  // ----------------------------------------------------
  // PSRAM CHECK
  // ----------------------------------------------------

  if (psramFound()) {

    Serial.println(
      "PSRAM: FOUND"
    );

  }

  else {

    Serial.println(
      "PSRAM: NOT FOUND"
    );
  }


  // ----------------------------------------------------
  // TEST CAMERA FRAME
  // ----------------------------------------------------

  Serial.println(
    "Testing camera..."
  );


  camera_fb_t *fb =
    esp_camera_fb_get();


  if (!fb) {

    Serial.println(
      "Camera frame test FAILED"
    );

    return false;
  }


  Serial.printf(
    "Camera frame OK: %ux%u, %u bytes\n",
    fb->width,
    fb->height,
    fb->len
  );


  esp_camera_fb_return(fb);


  return true;
}


// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  Serial.setDebugOutput(false);


  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    " FACE FOLLOWER V3.1"
  );

  Serial.println(
    " Corrected Motor Direction"
  );

  Serial.println(
    "================================"
  );


  // ----------------------------------------------------
  // MOTOR
  // ----------------------------------------------------

  setupMotorPins();


  // ----------------------------------------------------
  // CAMERA
  // ----------------------------------------------------

  if (!setupCamera()) {

    Serial.println(
      "Camera setup failed."
    );

    motorStop();


    while (true) {

      delay(1000);
    }
  }


  // ----------------------------------------------------
  // WIFI
  // ----------------------------------------------------

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );


  Serial.print(
    "Connecting to WiFi"
  );


  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(400);

    Serial.print(".");
  }


  Serial.println();

  Serial.println(
    "WiFi connected!"
  );


  // ----------------------------------------------------
  // HTTP SERVERS
  // ----------------------------------------------------

  startCameraServer();


  // ----------------------------------------------------
  // DISPLAY ADDRESS
  // ----------------------------------------------------

  Serial.println();
  Serial.println(
    "================================"
  );


  Serial.print(
    "ESP32 IP: "
  );

  Serial.println(
    WiFi.localIP()
  );


  Serial.print(
    "Camera: http://"
  );

  Serial.print(
    WiFi.localIP()
  );

  Serial.println(
    ":81/stream"
  );


  Serial.print(
    "Motor: http://"
  );

  Serial.print(
    WiFi.localIP()
  );

  Serial.println(
    "/motor?dir=forward"
  );


  Serial.println(
    "================================"
  );
}


// ======================================================
// LOOP
// ======================================================

void loop() {

  // HTTP server handles camera + motor commands.

  delay(10000);
}