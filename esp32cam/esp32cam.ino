#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

// ===========================================
// Configuração do WiFi
// ===========================================
const char* ssid = "";           // Altere para seu WiFi
const char* password = "";      // Altere para sua senha

// ===========================================
// Definição dos pinos da câmera AI-Thinker
// ===========================================
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

httpd_handle_t camera_httpd = NULL;

// ===========================================
// Função para capturar e enviar imagem
// ===========================================
static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  
  // Captura imagem da câmera
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Falha ao capturar imagem");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  
  // Define cabeçalhos HTTP
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  // Envia imagem
  res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  
  // Libera memória do buffer
  esp_camera_fb_return(fb);
  
  return res;
}

// ===========================================
// Função para stream de vídeo (opcional)
// ===========================================
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];
  
  // Define cabeçalhos para streaming MJPEG
  res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
  if (res != ESP_OK) {
    return res;
  }
  
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Falha ao capturar frame");
      res = ESP_FAIL;
    } else {
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }
    
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, 64, 
                            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", 
                            _jpg_buf_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, "\r\n", 2);
    }
    
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    }
    
    if (res != ESP_OK) {
      break;
    }
  }
  
  return res;
}

// ===========================================
// Inicializa servidor HTTP
// ===========================================
void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  
  // Endpoint para captura única
  httpd_uri_t capture_uri = {
    .uri       = "/cam-hi.jpg",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };
  
  // Endpoint para stream de vídeo
  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  Serial.printf("Iniciando servidor web na porta: '%d'\n", config.server_port);
  
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    Serial.println("Servidor iniciado com sucesso!");
  }
}

// ===========================================
// Configuração da câmera
// ===========================================
void configCamera() {
  camera_config_t config;
  
  // Configuração dos pinos
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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
  config.pixel_format = PIXFORMAT_JPEG;  // Formato JPEG para economia de memória
  
  // Configuração de qualidade
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;    // 320x240
    config.jpeg_quality = 12;               // Qualidade boa
    config.fb_count = 1;                    // Economiza RAM
  } else {
    config.frame_size = FRAMESIZE_SVGA;  // 800x600 (qualidade média)
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Inicializa a câmera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) { 
    Serial.printf("Erro ao inicializar câmera: 0x%x\n", err);
    return;
  }
  
  // Ajustes adicionais do sensor
  sensor_t *s = esp_camera_sensor_get();
  
  // Configurações recomendadas para detecção de veículos
  s->set_brightness(s, 0);     // -2 a 2
  s->set_contrast(s, 1);       // -2 a 2
  s->set_saturation(s, 0);     // -2 a 2
  s->set_special_effect(s, 0); // 0 = sem efeito
  s->set_whitebal(s, 1);       // 0 = desligado, 1 = ligado
  s->set_awb_gain(s, 1);       // 0 = desligado, 1 = ligado
  s->set_wb_mode(s, 0);        // 0 a 4 - se awb_gain ligado
  s->set_exposure_ctrl(s, 1);  // 0 = desligado, 1 = ligado
  s->set_aec2(s, 0);           // 0 = desligado, 1 = ligado
  s->set_ae_level(s, 0);       // -2 a 2
  s->set_aec_value(s, 300);    // 0 a 1200
  s->set_gain_ctrl(s, 1);      // 0 = desligado, 1 = ligado
  s->set_agc_gain(s, 0);       // 0 a 30
  s->set_gainceiling(s, (gainceiling_t)6);  // 0 a 6
  s->set_bpc(s, 1);            // 0 = desligado, 1 = ligado
  s->set_wpc(s, 1);            // 0 = desligado, 1 = ligado
  s->set_raw_gma(s, 1);        // 0 = desligado, 1 = ligado
  s->set_lenc(s, 1);           // 0 = desligado, 1 = ligado
  s->set_hmirror(s, 0);        // 0 = desligado, 1 = ligado (espelhar horizontal)
  s->set_vflip(s, 0);          // 0 = desligado, 1 = ligado (espelhar vertical)
  s->set_dcw(s, 1);            // 0 = desligado, 1 = ligado
  s->set_colorbar(s, 0);       // 0 = desligado, 1 = ligado
  
  Serial.println("Câmera configurada com sucesso!");
}

// ===========================================
// Setup
// ===========================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\nIniciando ESP32-CAM...");
  
  // Configura câmera
  configCamera();
  
  // Conecta ao WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
 Serial.println("\n\n WiFi conectado!");
  Serial.print("IP da câmera: http://");
  Serial.println(WiFi.localIP());
  Serial.print("Captura: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/cam-hi.jpg");
  Serial.print("Stream: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/stream");
  Serial.println("\n===========================================");
  
  // Inicia servidor
  startCameraServer();
}

// ===========================================
// Loop
// ===========================================
void loop() {
  // O servidor HTTP roda em sua própria task
  delay(10000);
  
  // Opcional: imprime status a cada 10 segundos
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Sistema operando normalmente");
  } else {
    Serial.println("WiFi desconectado! Reconectando...");
    WiFi.reconnect();
  }
}