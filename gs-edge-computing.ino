#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include "DHTesp.h"

// -------------------- CONFIGURAÇÃO DE REDE / MQTT --------------------
// Rede usada no simulador Wokwi (pode ser alterada para testes reais)
const char* ssid       = "Wokwi-GUEST";
const char* password   = "";

// Broker público MQTT para demonstração (pode ser substituído por um privado)
const char* mqttServer = "broker.hivemq.com";
const int   mqttPort   = 1883;

// Identificação do dispositivo e tópicos de publicação
const char* mqttClientId     = "skilldesk_esp32_oled";
const char* mqttTopicMetrics = "gs2025/skilldesk/grupoX/metrics"; // métricas gerais
const char* mqttTopicAlerts  = "gs2025/skilldesk/grupoX/alerts";  // eventos de alerta

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// -------------------- PINAGEM FÍSICA --------------------
#define DHT_PIN      14   // DHT22: temperatura e umidade
#define LDR_PIN      34   // LDR: luminosidade ambiente (entrada analógica)
#define BUTTON_PIN   4    // Botão físico multifunção
#define NEOPIXEL_PIN 27   // Anel NeoPixel RGB
#define NUM_LEDS     16   // Número de LEDs no anel

// -------------------- OLED I2C --------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// -------------------- OBJETOS DE HARDWARE --------------------
DHTesp dht;
Adafruit_NeoPixel ring(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// -------------------- ESTADOS DA MÁQUINA PRINCIPAL --------------------
enum SystemState {
  STATE_IDLE,   // Sem sessão de foco ativa
  STATE_FOCUS,  // Sessão de foco em andamento
  STATE_BREAK   // Pausa em andamento
};

SystemState currentState = STATE_IDLE;
bool deepFocusMode = false; // Modo de foco profundo (menos variações visuais)

// -------------------- CONFIGURAÇÃO DE TEMPO (POMODORO) --------------------
// Duração das sessões em milissegundos
unsigned long focusDurationMs = 25UL * 60UL * 1000UL; // 25 minutos de foco
unsigned long breakDurationMs = 5UL  * 60UL * 1000UL; // 5 minutos de pausa
unsigned long stateStartTime  = 0;                    // instante em que o estado atual começou

// -------------------- VARIÁVEIS DOS SENSORES --------------------
float currentTemp = 0.0;
float currentHum  = 0.0;
int   currentLight = 0;       // valor de luminosidade corrigido (maior = mais claro)
int   currentLightRaw = 0;    // leitura analógica bruta do LDR

bool  envOk       = true;     // indica se o ambiente está dentro de limites confortáveis
String envStatus  = "OK";     // "OK" ou "BAD" para enviar ao MQTT
String envMessage = "Conforto OK"; // explicação do status no display

unsigned long lastSensorReadMs = 0;
const unsigned long SENSOR_INTERVAL_MS = 2000; // intervalo de leitura dos sensores

unsigned long lastMqttMs = 0;
const unsigned long MQTT_INTERVAL_MS = 5000;   // intervalo de publicação MQTT

// -------------------- VARIÁVEIS DO BOTÃO / GESTOS --------------------
// Botão físico usado para clique simples, duplo clique e clique longo
bool buttonWasDown = false;
unsigned long buttonDownTime = 0;
unsigned long lastReleaseTime = 0;
bool singleClickPending = false;

const unsigned long LONG_PRESS_MS        = 1200; // tempo mínimo para clique longo (ms)
const unsigned long DOUBLE_TAP_WINDOW_MS = 350;  // janela para detectar duplo clique (ms)

// -------------------- REDE / MQTT --------------------

// Conecta ao Wi-Fi configurado
void setupWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");
  Serial.print("IP obtido: ");
  Serial.println(WiFi.localIP());
}

// Callback MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Neste projeto, a comunicação é unidirecional (publicação de métricas)
}

// Garante que o ESP32 esteja conectado ao broker MQTT
void ensureMqttConnection() {
  if (mqttClient.connected()) return;

  Serial.print("Conectando ao MQTT...");
  while (!mqttClient.connected()) {
    if (mqttClient.connect(mqttClientId)) {
      Serial.println(" conectado");
      mqttClient.subscribe("gs2025/skilldesk/grupoX/commands"); // reservado para evolução futura
    } else {
      Serial.print(" falhou, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" tentando novamente em 2s...");
      delay(2000);
    }
  }
}

// -------------------- CONTROLE DO ANEL NEOPIXEL --------------------

// Define a mesma cor em todos os LEDs do anel
void setRingColor(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_LEDS; i++) {
    ring.setPixelColor(i, ring.Color(r, g, b));
  }
  ring.show();
}

// Preenche o anel proporcionalmente ao progresso da sessão (barra de progresso circular)
void setRingProgress(float progress, uint8_t r, uint8_t g, uint8_t b) {
  int lit = (int)(progress * NUM_LEDS);
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < lit) {
      ring.setPixelColor(i, ring.Color(r, g, b));
    } else {
      ring.setPixelColor(i, ring.Color(0, 0, 0));
    }
  }
  ring.show();
}

// -------------------- DISPLAY OLED --------------------

// Desenha o layout fixo (título e linha separadora)
void drawBaseScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("SkillDesk");

  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  display.display();
}

// Converte o estado atual em texto para uso no display/MQTT
String stateToString() {
  switch (currentState) {
    case STATE_IDLE:  return "idle";
    case STATE_FOCUS: return "focus";
    case STATE_BREAK: return "break";
  }
  return "unknown";
}

// Atualiza a linha de status no display (estado + Deep Focus)
void updateStatusLine() {
  display.fillRect(0, 11, SCREEN_WIDTH, 8, SSD1306_BLACK);
  display.setCursor(0, 11);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.print("State: ");
  display.print(stateToString());
  if (deepFocusMode) display.print(" DF");

  display.display();
}

// Mostra o tempo restante da sessão atual no formato MM:SS
void updateTimerLine() {
  unsigned long now = millis();
  unsigned long elapsed = now - stateStartTime;

  unsigned long baseMs = 0;
  if (currentState == STATE_FOCUS) baseMs = focusDurationMs;
  else if (currentState == STATE_BREAK) baseMs = breakDurationMs;

  unsigned long remaining = 0;
  if (baseMs > 0 && elapsed < baseMs) remaining = baseMs - elapsed;

  unsigned long seconds = remaining / 1000UL;
  unsigned int mm = seconds / 60;
  unsigned int ss = seconds % 60;

  char buf[6];
  sprintf(buf, "%02u:%02u", mm, ss);

  display.fillRect(0, 20, SCREEN_WIDTH, 16, SSD1306_BLACK);
  display.setCursor(0, 20);
  display.setTextSize(2);
  display.print(buf);
  display.display();
}

// Exibe temperatura, umidade, luminosidade e mensagem de conforto no display
void updateEnvLines() {
  display.fillRect(0, 36, SCREEN_WIDTH, 28, SSD1306_BLACK);
  display.setCursor(0, 36);
  display.setTextSize(1);

  display.print("T:");
  display.print(currentTemp, 1);
  display.print("C H:");
  display.print(currentHum, 0);
  display.println("%");

  display.print("L:");
  display.print(currentLight);
  display.print(" | ");
  display.print(envStatus);

  display.setCursor(0, 48);
  display.print(envMessage);

  display.display();
}

// -------------------- LÓGICA VISUAL DO ANEL (ESTADO + AMBIENTE) --------------------

// Define a cor do anel a partir da combinação: ambiente, modo e estado de sessão
void updateRingVisual() {
  unsigned long now = millis();

  // Prioriza alerta ambiental: se o ambiente está ruim, anel vermelho independente do estado
  if (!envOk) {
    setRingColor(80, 0, 0);
    return;
  }

  // Em modo de foco profundo, o anel fica fixo e discreto (azul) para reduzir distrações
  if (deepFocusMode) {
    setRingColor(0, 0, 80);
    return;
  }

  // Ambiente ok e Deep Focus desligado → cores conforme o estado de trabalho
  if (currentState == STATE_IDLE) {
    setRingColor(5, 5, 5); // quase apagado, aguardando início de foco
  } else if (currentState == STATE_FOCUS) {
    // anel funciona como barra de progresso do ciclo de foco
    float progress = float(now - stateStartTime) / float(focusDurationMs);
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;
    setRingProgress(progress, 0, 80, 0); // verde preenchendo
  } else if (currentState == STATE_BREAK) {
    // pausa 
    setRingColor(0, 60, 0);
  }
}

// -------------------- LEITURA DE SENSORES / CONFORTO AMBIENTAL --------------------

// Lê DHT22 e LDR e atualiza o diagnóstico de conforto (temp/umidade/luz)
void readSensors() {
  TempAndHumidity data = dht.getTempAndHumidity();
  if (!isnan(data.temperature)) currentTemp = data.temperature;
  if (!isnan(data.humidity))    currentHum  = data.humidity;

  // Leitura bruta do LDR
  currentLightRaw = analogRead(LDR_PIN);

  // Correção de inversão: se a ligação elétrica faz o valor diminuir quando clareia,
  // então invertemos via software para facilitar a interpretação (maior = mais claro).
  currentLight = 4095 - currentLightRaw;

  // Valores padrão assumindo ambiente confortável
  envOk      = true;
  envStatus  = "OK";
  envMessage = "Conforto OK";

  // Limiares de conforto:
  // Temperatura: 21–27 ºC
  // Umidade:     35–65 %
  // Luz:         1000–3200 (0–4095 após a inversão)
  bool tempOk  = (currentTemp >= 21 && currentTemp <= 27);
  bool humOk   = (currentHum  >= 35 && currentHum  <= 65);
  bool lightOk = (currentLight >= 1000 && currentLight <= 3200);

  if (!tempOk || !humOk || !lightOk) {
    envOk = false;
    envStatus = "BAD";

    // Explica o principal motivo do desconforto
    if (!tempOk) {
      if (currentTemp < 21)       envMessage = "Frio demais";
      else if (currentTemp > 27)  envMessage = "Calor demais";
    } else if (!humOk) {
      if (currentHum < 35)        envMessage = "Ar seco";
      else if (currentHum > 65)   envMessage = "Ar muito umido";
    } else if (!lightOk) {
      if (currentLight < 1000)    envMessage = "Muito escuro";
      else if (currentLight > 3200) envMessage = "Luz forte demais";
    } else {
      envMessage = "Ambiente desconfortavel";
    }
  }

  updateEnvLines();
  updateRingVisual();
}

// -------------------- PUBLICAÇÃO MQTT --------------------

// Envia as principais métricas do dispositivo para o broker MQTT
void publishMetrics() {
  unsigned long now = millis();
  unsigned long focusElapsedMin = 0;

  if (currentState == STATE_FOCUS) {
    focusElapsedMin = (now - stateStartTime) / 60000UL;
  }

  String payload = "{";
  payload += "\"temp\":" + String(currentTemp, 1) + ",";
  payload += "\"humidity\":" + String(currentHum, 1) + ",";
  payload += "\"light\":" + String(currentLight) + ",";
  payload += "\"envStatus\":\"" + envStatus + "\",";
  payload += "\"state\":\"" + stateToString() + "\",";
  payload += "\"deepFocus\":" + String(deepFocusMode ? "true" : "false") + ",";
  payload += "\"focusMinutes\":" + String(focusElapsedMin);
  payload += "}";

  mqttClient.publish(mqttTopicMetrics, payload.c_str());

  // Também dispara em tópico de alerta quando o ambiente está ruim ou durante pausas
  if (!envOk || currentState == STATE_BREAK) {
    mqttClient.publish(mqttTopicAlerts, payload.c_str());
  }
}

// -------------------- MÁQUINA DE ESTADOS (FOCO / PAUSA / IDLE) --------------------

// Entra em um novo estado (IDLE, FOCUS ou BREAK), reseta temporizador e atualiza UI
void enterState(SystemState newState) {
  currentState = newState;
  stateStartTime = millis();

  updateStatusLine();
  updateTimerLine();
  updateRingVisual();
}

// Atalhos para leitura / legibilidade
void startFocus() { enterState(STATE_FOCUS); }
void startBreak() { enterState(STATE_BREAK); }
void goIdle()     { enterState(STATE_IDLE); }

// Alterna o modo de Deep Focus (feedback mais estável e menos chamativo)
void toggleDeepFocus() {
  deepFocusMode = !deepFocusMode;
  updateStatusLine();
  updateRingVisual();
}

// Ajusta o tempo de foco (long press do botão) entre 25, 35 e 45 minutos
void cycleFocusTime() {
  if (focusDurationMs == 25UL * 60UL * 1000UL)      focusDurationMs = 35UL * 60UL * 1000UL;
  else if (focusDurationMs == 35UL * 60UL * 1000UL) focusDurationMs = 45UL * 60UL * 1000UL;
  else                                              focusDurationMs = 25UL * 60UL * 1000UL;

  display.fillRect(0, 36, SCREEN_WIDTH, 10, SSD1306_BLACK);
  display.setCursor(0, 36);
  display.setTextSize(1);
  display.print("Focus: ");
  display.print(focusDurationMs / 60000UL);
  display.println(" min");
  display.display();
}

// -------------------- TRATAMENTO DO BOTÃO FÍSICO --------------------

// Clique simples: inicia/alternar foco/pausa
void handleSingleClick() {
  if (currentState == STATE_IDLE)      startFocus();
  else if (currentState == STATE_FOCUS) startBreak();
  else if (currentState == STATE_BREAK) goIdle();
}

// Duplo clique: alterna modo Deep Focus
void handleDoubleClick() {
  toggleDeepFocus();
}

// Clique longo: altera a duração da sessão de foco
void handleLongPress() {
  cycleFocusTime();
}

// Lê o estado do botão e identifica padrão: clique simples, duplo ou longo
void processButton() {
  unsigned long now = millis();
  bool pressed = (digitalRead(BUTTON_PIN) == LOW);

  if (pressed && !buttonWasDown) {
    buttonWasDown = true;
    buttonDownTime = now;
  }

  if (!pressed && buttonWasDown) {
    buttonWasDown = false;
    unsigned long duration = now - buttonDownTime;

    if (duration >= LONG_PRESS_MS) {
      singleClickPending = false;
      handleLongPress();
    } else {
      // clique rápido: pode ser single ou o primeiro clique de um double
      if (singleClickPending && (now - lastReleaseTime <= DOUBLE_TAP_WINDOW_MS)) {
        singleClickPending = false;
        handleDoubleClick();
      } else {
        singleClickPending = true;
        lastReleaseTime = now;
      }
    }
  }

  // Se passou da janela de double tap e ainda há clique pendente, trata como single
  if (singleClickPending && (now - lastReleaseTime > DOUBLE_TAP_WINDOW_MS)) {
    singleClickPending = false;
    handleSingleClick();
  }
}

// -------------------- SETUP / LOOP PRINCIPAL --------------------

void setup() {
  Serial.begin(115200);

  // Configura sensores
  dht.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(LDR_PIN, INPUT);

  // Configura botão multifunção
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Inicializa anel NeoPixel
  ring.begin();
  ring.clear();
  ring.show();

  // Inicializa interface I2C e display OLED
  Wire.begin(21, 22); // SDA, SCL do ESP32
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Falha ao iniciar OLED");
    for (;;); // trava aqui caso o display não inicialize
  }
  drawBaseScreen();
  updateStatusLine();
  updateTimerLine();
  updateEnvLines();

  // Configura Wi-Fi e MQTT
  setupWiFi();
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);

  // Estado inicial: ociosidade (sem foco ativo)
  enterState(STATE_IDLE);
}

void loop() {
  unsigned long now = millis();

  ensureMqttConnection();
  mqttClient.loop();

  // Atualiza sensores periodicamente
  if (now - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = now;
    readSensors();
  }

  // Publica dados MQTT periodicamente
  if (now - lastMqttMs >= MQTT_INTERVAL_MS) {
    lastMqttMs = now;
    publishMetrics();
  }

  // Controle de tempo de foco e pausa
  if (currentState == STATE_FOCUS) {
    updateTimerLine();
    updateRingVisual();

    if (now - stateStartTime >= focusDurationMs) {
      startBreak(); // foco terminou → inicia pausa automaticamente
    }
  } else if (currentState == STATE_BREAK) {
    updateTimerLine();
    updateRingVisual();

    if (now - stateStartTime >= breakDurationMs) {
      goIdle();     // pausa terminou → volta para estado ocioso
    }
  }

  // Lê o botão e interpreta os gestos
  processButton();
}