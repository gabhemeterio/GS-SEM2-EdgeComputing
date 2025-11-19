## Integrantes

- Felippe Nascimento Silva | RM 562123
- Gabriel S. Hemeterio | RM 566243
- Matheus Hideki Doroszewski Yoshimura | RM 564970
  
---

# SkillDesk – Estação IoT de Bem-Estar e Produtividade  
GS 2025 – Edge Computing & Computer Systems

A **SkillDesk** é uma estação de mesa baseada em **ESP32** que monitora conforto ambiental (temperatura, umidade e luz), organiza ciclos de foco e pausa, dá feedback em tempo real com um **anel de LEDs RGB** e um **display OLED**, e envia métricas para um **dashboard em Node-RED via MQTT**.  
A proposta é tornar o **futuro do trabalho** mais saudável, produtivo e orientado a dados.

---

## 1. Contexto e problema

No cenário atual do trabalho (home office, modelo híbrido, escritórios abertos e coworkings), é comum:

- Trabalhar em ambientes improvisados, sem controle de conforto;
- Ficar longos períodos sem pausas estruturadas;
- Não ter dados concretos sobre o bem-estar de quem está trabalhando.

Isso leva a:

- Aumento de estresse e fadiga;
- Queda de produtividade;
- Possíveis afastamentos por saúde;
- Falta de informações para políticas de bem-estar e ergonomia.

A SkillDesk nasce para **medir** e **organizar** esses fatores de forma simples, usando IoT.

---

## 2. Solução – o que é a SkillDesk

A **SkillDesk** é um pequeno dispositivo de mesa (estação IoT) que:

- Monitora o ambiente de trabalho em tempo real:
  - Temperatura e umidade (sensor DHT22);
  - Luminosidade (LDR).

- Organiza sessões de foco e pausa (estilo Pomodoro):
  - Sessões de foco com duração configurável (25/35/45 minutos);
  - Pausas padrão de 5 minutos;
  - Toda a lógica roda localmente no ESP32 (edge computing).

- Dá feedback visual e textual:
  - Anel NeoPixel RGB:
    - Verde: foco;
    - Azul: pausa;
    - Vermelho: ambiente desconfortável;
    - Azul discreto: modo “Deep Focus” (menos distrações).
  - Display OLED 128x64:
    - Estado (idle/focus/break);
    - Indicador “DF” para Deep Focus;
    - Timer da sessão (MM:SS);
    - Temperatura, umidade e luz;
    - Mensagem de conforto.

- Envia os dados para um dashboard:
  - Usa MQTT para publicar métricas e alertas;
  - O Node-RED consome esses dados e exibe em um dashboard web com gauges e textos.

Assim, a SkillDesk conecta **bem-estar**, **produtividade** e **IoT** de forma prática e mensurável.

## 3. Arquitetura geral

Visão em alto nível:

- Sensores → DHT22 (temp/umid) e LDR (luz);
- Interface local → botão, display OLED, anel NeoPixel;
- Processamento → ESP32 (lógica de foco, conforto e timer);
- Comunicação → MQTT (broker, ex.: HiveMQ);
- Visualização → Node-RED + Dashboard Web.

Pontos-chave:

- As decisões (estado de conforto, transição foco/pausa, alertas) acontecem no ESP32;
- O broker MQTT recebe os dados em JSON;
- O Node-RED apenas escuta os tópicos e exibe tudo no dashboard.

---

## 4. Hardware

### 4.1. Componentes

- ESP32 DevKit V1  
- Sensor DHT22 (temperatura/umidade)  
- LDR (fotoresistor)  
- Resistor 10 kΩ (para o divisor de tensão do LDR)  
- Botão push-button (botão multifunção)  
- Anel NeoPixel RGB (WS2812, ~16 LEDs)  
- Display OLED 128x64 I2C (SSD1306)  
- Protoboard e jumpers (ou PCB em uma versão física)

### 4.2. Ligações de pinos (ESP32)

DHT22:

- VCC → 3V3  
- GND → GND  
- DATA → GPIO 14  

LDR + resistor 10 kΩ (divisor de tensão):

- Um lado da LDR → 3V3  
- Outro lado da LDR → nó comum  
- Nó comum → GPIO 34 (entrada analógica do ESP32)  
- Nó comum → resistor 10 kΩ → GND  

Observação: o valor do LDR é invertido (mais luz = valor analógico menor), então no código fazemos:

    currentLight = 4095 - currentLightRaw;

Botão:

- Um terminal → GPIO 4  
- Outro terminal → GND  
- No firmware: entrada com pull-up interno (INPUT_PULLUP)
  - Pressionado = LOW  
  - Solto = HIGH

Anel NeoPixel:

- VCC → 5V  
- GND → GND  
- DIN → GPIO 27  

OLED 128x64 (SSD1306, I2C):

- VCC → 3V3  
- GND → GND  
- SDA → GPIO 21  
- SCL → GPIO 22  

## 5. Firmware – funcionamento lógico

### 5.1. Leitura de sensores

A cada ~2 segundos o ESP32:

- Lê temperatura e umidade via DHT22;
- Lê o valor analógico do LDR;
- Ajusta o valor da luz:

    currentLight = 4095 - currentLightRaw;

### 5.2. Cálculo de conforto

Faixas consideradas confortáveis:

- Temperatura: 21°C a 27°C  
- Umidade: 35% a 65%  
- Luz (0–4095): 1000 a 3200 (após correção)

Lógica básica:

- Se todos estiverem na faixa:
  - envOk = true  
  - envStatus = "OK"  
  - envMessage = "Conforto OK"

- Se algum estiver fora:
  - envOk = false  
  - envStatus = "BAD"  
  - envMessage indica o principal problema (ex.: "Calor demais", "Muito escuro").

### 5.3. Máquina de estados de foco

Estados:

- IDLE – ociosidade (sem sessão de foco ativa);  
- FOCUS – sessão de foco em andamento;  
- BREAK – pausa em andamento.

Durações padrão:

- Foco: 25, 35 ou 45 minutos (ajustados pelo clique longo);  
- Pausa: 5 minutos.

Transições:

- IDLE → FOCUS: clique simples no botão;  
- FOCUS → BREAK: clique simples ou fim do tempo de foco;  
- BREAK → IDLE: clique simples ou fim da pausa.

O firmware também mantém um contador de `focusMinutes` (minutos de foco na sessão atual) para enviar via MQTT.

### 5.4. Interface local (display + LEDs)

Display OLED:

- Mostra:
  - Estado: idle / focus / break;
  - Indicador DF quando o modo Deep Focus está ativo;
  - Timer da sessão em MM:SS;
  - Temperatura, umidade e luz;
  - Mensagem de conforto.

Anel NeoPixel:

- Ambiente ok (envOk = true):
  - IDLE: anel quase apagado;
  - FOCUS: verde, com progresso circular ao longo do tempo;
  - BREAK: azul, indicando pausa.
- Ambiente desconfortável (envOk = false):
  - Anel vermelho, independente do estado.
- Deep Focus ON:
  - Anel azul fixo discreto (quando ambiente está ok).

---

## 6. MQTT – comunicação com o dashboard

### 6.1. Broker

Exemplo de broker usado no projeto (didático):

- Host: broker.hivemq.com  
- Porta: 1883  

Esses valores podem ser alterados tanto no firmware quanto no Node-RED.

### 6.2. Tópicos

Sugestão de tópicos usados:

- Métricas gerais:  
  gs2025/skilldesk/grupoX/metrics

- Alertas:  
  gs2025/skilldesk/grupoX/alerts

(`grupoX` pode ser substituído pelo identificador real do grupo.)

### 6.3. Formato das mensagens (JSON)

Exemplo de payload publicado no tópico de métricas:

    {
      "temp": 24.3,
      "humidity": 48.0,
      "light": 2300,
      "envStatus": "OK",
      "state": "focus",
      "deepFocus": true,
      "focusMinutes": 12
    }

Campos:

- temp: temperatura (°C)  
- humidity: umidade (%)  
- light: luminosidade (0–4095)  
- envStatus: "OK" ou "BAD"  
- state: "idle", "focus" ou "break"  
- deepFocus: true/false (modo Deep Focus)  
- focusMinutes: minutos de foco acumulados na sessão

O tópico de alertas reutiliza esse mesmo formato, sendo disparado em situações críticas (ambiente ruim, pausas etc.).

## 7. Simulação no Wokwi

Passos gerais:

1. Acessar https://wokwi.com  
2. Criar um novo projeto com ESP32;  
3. Adicionar DHT22, LDR + resistor 10k, botão, anel NeoPixel e OLED SSD1306;  
4. Fazer a fiação conforme descrito na seção de hardware;  
5. Colar o código .ino do projeto no editor do Wokwi;  
6. Clicar em Run;  
7. Usar o monitor serial para ver logs de Wi-Fi, MQTT e publicações.

Se houver um link público da simulação, pode ser incluído, por exemplo:

- Link Wokwi do projeto: https://wokwi.com/projects/448010425007264769

---

## 8. Dashboard no Node-RED

### 8.1. O que o dashboard exibe

- Resumo geral:
  - Estado atual e Deep Focus (ex.: "Estado: Foco | Deep Focus: ON").

- Ambiente de trabalho:
  - Gauge de temperatura (°C);
  - Gauge de umidade (%);
  - Gauge de luminosidade (0–4095);
  - Texto: "Ambiente OK" ou "Ambiente DESCONFORTÁVEL".

- Sessão de foco:
  - Gauge com minutos de foco da sessão atual.

- Alertas:
  - Texto com o último alerta (estado, envStatus, temp, hum, luz).

### 8.2. Importando o fluxo

1. Abrir o editor do Node-RED (por ex.: http://localhost:1880);  
2. Menu → Import → Clipboard;  
3. Colar o fluxo JSON (skilldesk-dashboard.json);  
4. Clicar em Import;  
5. Configurar o broker MQTT no nó correspondente (host/porta);  
6. Clicar em Deploy;  
7. Acessar o dashboard em:

- http://<IP_DO_NODE_RED>:1880/ui

---

## 9. Como rodar o projeto (resumo prático)

### 9.1. ESP32 – firmware

1. Instalar Arduino IDE;  
2. Instalar o pacote de placas ESP32 (Espressif);  
3. Instalar as bibliotecas:
   - PubSubClient;
   - Adafruit GFX;
   - Adafruit SSD1306;
   - Adafruit NeoPixel;
   - DHTesp (ou similar);
4. Abrir o arquivo .ino da SkillDesk;  
5. Ajustar Wi-Fi (ssid/password) e broker (mqttServer) se necessário;  
6. Selecionar a placa ESP32 Dev Module;  
7. Fazer upload;  
8. Verificar no monitor serial:
   - Conexão ao Wi-Fi;
   - Conexão ao broker;
   - Publicação de métricas.

### 9.2. Node-RED – dashboard

1. Instalar Node-RED;  
2. Instalar node-red-dashboard (npm install node-red-dashboard na pasta ~/.node-red);  
3. Reiniciar Node-RED;  
4. Importar o fluxo do dashboard;  
5. Configurar o broker MQTT igual ao do ESP32;  
6. Deploy;  
7. Acessar o dashboard em http://<IP_DO_NODE_RED>:1880/ui.

---

## 10. Uso do ponto de vista do usuário

### 10.1. Botão multifunção

- Clique simples:
  - idle → inicia foco;
  - focus → termina foco e inicia pausa;
  - break → termina pausa e volta para idle.

- Duplo clique:
  - Liga/desliga modo Deep Focus.

- Clique longo (~1,2 s):
  - Alterna a duração da sessão de foco (25/35/45 min).

### 10.2. Interpretação rápida

- Anel:
  - Verde: foco;
  - Azul: pausa;
  - Azul discreto: Deep Focus ativo;
  - Vermelho: ambiente desconfortável.

- Display:
  - Estado (idle/focus/break);
  - DF quando Deep Focus ativo;
  - Timer MM:SS da sessão;
  - Temp, umidade e luz;
  - Mensagem de conforto ("Conforto OK", "Calor demais", "Muito escuro"...).

## 11. Vídeo de apresentação (GS 2025)

Assista ao vídeo de apresentação da SkillDesk no link abaixo:

- Vídeo da solução no YouTube: [clique aqui](https://youtu.be/mltJLWFgUPk)
