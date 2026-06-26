// ============================================================
// PV-Davies01 - BastWAN v2.0 + Davis 7345.440
// LoRaWAN ABP - ChirpStack CENGICANA
// Payload: 2 bytes big-endian (0.1 mm por unidad - codec ChirpStack)
// 1 pulso = 0.2 mm = 2 unidades de 0.1 mm
// ============================================================
#include <lorawan.h>

// --- Credenciales ABP ---
const char *devAddr = "260ccc34";
const char *nwkSKey = "05ee44efb4045dbcd942fdfaa8c2546c";
const char *appSKey = "8b7921986ff1b498ae383c0cd2f6c44f";

// --- Pin pluviometro ---
#define RAIN_PIN 9
#define DEBOUNCE_MS 200

// --- Timing ---
#define INTERVALO_MS 900000UL

// --- Variables lluvia ---
unsigned long ultimoPulso = 0;
unsigned long pulsos = 0;
unsigned long ultimoReporte = 0;
unsigned long minuto = 1;

// --- Estado de pin para deteccion de flancos ---
int estadoAnterior = HIGH;

// --- LoRaWAN ---
const sRFM_pins RFM_pins = {
  .CS = SS,
  .RST = RFM_RST,
  .DIO0 = RFM_DIO0,
  .DIO1 = RFM_DIO1,
  .DIO2 = RFM_DIO2,
  .DIO5 = RFM_DIO5,
};

char outStr[255];
byte recvStatus = 0;

void setup() {
  Serial.begin(115200);
  delay(3000);

  pinMode(RAIN_PIN, INPUT_PULLUP);
  delay(100);
  estadoAnterior = digitalRead(RAIN_PIN);

  if (!lora.init()) {
    Serial.println("ERROR: LoRa no detectado");
    while (1) {
    }
  }

  lora.setDeviceClass(CLASS_A);
  lora.setDataRate(SF8BW125);
  lora.setChannel(MULTI);
  lora.setNwkSKey(nwkSKey);
  lora.setAppSKey(appSKey);
  lora.setDevAddr(devAddr);

  Serial.println("========================================");
  Serial.println("  PV-Davies01 - CENGICANA [v2.1]");
  Serial.println("  Davis 7345.440 + BastWAN ABP");
  Serial.println("  0.2 mm/pulso = 2 unidades (0.1 mm)");
  Serial.println("========================================");

  ultimoReporte = millis();
}

void loop() {
  int estadoActual = digitalRead(RAIN_PIN);

  if (estadoAnterior == HIGH && estadoActual == LOW) {
    unsigned long ahora = millis();

    if ((ahora - ultimoPulso) > DEBOUNCE_MS) {
      ultimoPulso = ahora;
      pulsos++;

      float mmActual = pulsos * 0.2f;
      Serial.print("  [PULSO] Min:");
      Serial.print(minuto);
      Serial.print(" | Acumulado: ");
      Serial.print(pulsos);
      Serial.print(" pulsos = ");
      Serial.print(mmActual, 1);
      Serial.println(" mm");
    }
  }
  estadoAnterior = estadoActual;

  if (millis() - ultimoReporte >= INTERVALO_MS) {
    ultimoReporte = millis();

    float mm = pulsos * 0.2f;

    Serial.println("----------------------------------------");
    Serial.print("Minuto #");
    Serial.print(minuto);
    Serial.print(" | Pulsos: ");
    Serial.print(pulsos);
    Serial.print(" | Lluvia: ");
    Serial.print(mm, 1);
    Serial.println(" mm");

    // ChirpStack espera 0.1 mm por unidad.
    // 1 pulso = 0.2 mm = 2 unidades.
    uint16_t valor = (uint16_t)(pulsos * 2);
    byte payload[2];
    payload[0] = (valor >> 8) & 0xFF;
    payload[1] = valor & 0xFF;

    Serial.print("Enviando payload (0.1mm): 0x");
    if (payload[0] < 0x10) {
      Serial.print("0");
    }
    Serial.print(payload[0], HEX);
    if (payload[1] < 0x10) {
      Serial.print("0");
    }
    Serial.print(payload[1], HEX);
    Serial.print(" = ");
    Serial.print(valor);
    Serial.println(" unidades");

    // Esta libreria encola el uplink y lo procesa en lora.update().
    lora.sendUplink((char *)payload, 2, 0, 2);
    Serial.println("Uplink encolado correctamente");

    recvStatus = lora.readData(outStr);
    if (recvStatus) {
      Serial.print("Downlink recibido: ");
      Serial.println(outStr);
    }

    Serial.println("----------------------------------------");

    pulsos = 0;
    minuto = (minuto + 1) % 1440;
  }

  lora.update();
}
