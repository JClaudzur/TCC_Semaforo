#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ========== CONFIGURAÇÃO ==========
const char* ssid = "";
const char* password = "";

// Pinos dos LEDs
#define LED_VERDE_A   26
#define LED_AMARELO_A 25
#define LED_VERMELHO_A 33
#define LED_VERDE_B   19
#define LED_AMARELO_B 18
#define LED_VERMELHO_B 21

// Tempos
const int TEMPO_MIN = 10;
const int TEMPO_MAX = 40;
const int TEMPO_AMARELO = 3;
const int TEMPO_SEGURANCA = 2;

// ========== VARIÁVEIS GLOBAIS ==========
WebServer server(80);
int veiculos_via_a = 0;
int veiculos_via_b = 0;
unsigned long ultimo_update = 0;
bool ciclo_ativo = false;

// ========== FUNÇÕES DOS LEDs ==========
void desligarTodos() {
  digitalWrite(LED_VERDE_A, LOW); digitalWrite(LED_AMARELO_A, LOW); digitalWrite(LED_VERMELHO_A, LOW);
  digitalWrite(LED_VERDE_B, LOW); digitalWrite(LED_AMARELO_B, LOW); digitalWrite(LED_VERMELHO_B, LOW);
}

void setVerde(String via) {
  desligarTodos();
  if (via == "A") { digitalWrite(LED_VERDE_A, HIGH); digitalWrite(LED_VERMELHO_B, HIGH); }
  else { digitalWrite(LED_VERDE_B, HIGH); digitalWrite(LED_VERMELHO_A, HIGH); }
}

void setAmarelo(String via) {
  desligarTodos();
  if (via == "A") { digitalWrite(LED_AMARELO_A, HIGH); digitalWrite(LED_VERMELHO_B, HIGH); }
  else { digitalWrite(LED_AMARELO_B, HIGH); digitalWrite(LED_VERMELHO_A, HIGH); }
}

void setVermelho() {
  desligarTodos();
  digitalWrite(LED_VERMELHO_A, HIGH);
  digitalWrite(LED_VERMELHO_B, HIGH);
}

int calcularTempo(int veiculos) {
  if (veiculos == 0) return TEMPO_MIN;
  if (veiculos <= 3) return 15;
  if (veiculos <= 6) return 25;
  if (veiculos <= 10) return 35;
  return TEMPO_MAX;
}

void handleUpdate() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"JSON invalido\"}");
    return;
  }
  
  veiculos_via_a = doc["via_a"] | 0;
  veiculos_via_b = doc["via_b"] | 0;
  ultimo_update = millis();
  
  Serial.printf("📥 Via A: %d | Via B: %d\n", veiculos_via_a, veiculos_via_b);
  
  String resp = "{\"ok\":true,\"via_a\":" + String(veiculos_via_a) + ",\"via_b\":" + String(veiculos_via_b) + "}";
  server.send(200, "application/json", resp);
}

void handleStatus() {
  String json = "{\"via_a\":" + String(veiculos_via_a) + 
                ",\"via_b\":" + String(veiculos_via_b) + 
                ",\"uptime\":" + String(millis()/1000) + "}";
  server.send(200, "application/json", json);
}

// ========== CICLO DO SEMÁFORO ==========
void aguardar(int segundos, String fase = "") {
  for (int i = segundos; i > 0; i--) {
    server.handleClient();
    if (fase != "") Serial.printf("%s %ds\n", fase.c_str(), i);
    delay(1000);
  }
}

void executarCiclo() {
  ciclo_ativo = true;
  String prioritaria = (veiculos_via_a >= veiculos_via_b) ? "A" : "B";
  String secundaria = (prioritaria == "A") ? "B" : "A";
  int tempo_p = calcularTempo((prioritaria == "A") ? veiculos_via_a : veiculos_via_b);
  int tempo_s = calcularTempo((secundaria == "A") ? veiculos_via_a : veiculos_via_b);
  
  Serial.printf("\n🚦 CICLO | Via %s=%ds | Via %s=%ds\n", 
                prioritaria.c_str(), tempo_p, secundaria.c_str(), tempo_s);
  
  // Via prioritária
  setVerde(prioritaria); aguardar(tempo_p, "🟢");
  setAmarelo(prioritaria); aguardar(TEMPO_AMARELO, "🟡");
  setVermelho(); aguardar(TEMPO_SEGURANCA, "🔴");
  
  // Via secundária
  setVerde(secundaria); aguardar(tempo_s, "🟢");
  setAmarelo(secundaria); aguardar(TEMPO_AMARELO, "🟡");
  setVermelho(); aguardar(TEMPO_SEGURANCA, "🔴");
  
  Serial.println("✅ Ciclo completo\n");
  ciclo_ativo = false;
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n🚦 SEMÁFORO INTELIGENTE\n");
  
  pinMode(LED_VERDE_A, OUTPUT); pinMode(LED_AMARELO_A, OUTPUT); pinMode(LED_VERMELHO_A, OUTPUT);
  pinMode(LED_VERDE_B, OUTPUT); pinMode(LED_AMARELO_B, OUTPUT); pinMode(LED_VERMELHO_B, OUTPUT);
  desligarTodos();
  
  Serial.print("📡 Conectando WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  Serial.println("\n✅ Conectado!");
  Serial.printf("📱 IP: http://%s\n", WiFi.localIP().toString().c_str());
  Serial.printf("💡 No Python use: ESP32_SEMAFORO_IP = \"%s\"\n\n", WiFi.localIP().toString().c_str());
  
  server.on("/update", handleUpdate);
  server.on("/status", handleStatus);
  server.begin();
  
  setVermelho();
  Serial.println("⏳ Aguardando dados...\n");
}

// ========== LOOP ==========
void loop() {
  server.handleClient();
  
  unsigned long agora = millis();
  static unsigned long ultimo_ciclo = 0;
  
  // Remove a verificação de ultimo_update
  // Agora executa SEMPRE a cada 5 segundos
  if (!ciclo_ativo && (agora - ultimo_ciclo > 5000)) {
    Serial.printf("\n🚦 Via A: %d | Via B (simulada): %d  \n", veiculos_via_a, veiculos_via_b);
    executarCiclo();
    ultimo_ciclo = agora;
  }
  
  delay(100);
}