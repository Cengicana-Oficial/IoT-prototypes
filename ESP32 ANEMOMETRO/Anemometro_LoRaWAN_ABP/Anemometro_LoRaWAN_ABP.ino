#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <string.h>

// ============================================================
// ESP32 + XL1262-P01 + Anemometro RS485 Modbus RTU
// LoRaWAN ABP con frame construido manualmente
//
// Payload uplink de 8 bytes (big-endian):
//   0-1: velocidad raw
//   2-3: nivel viento
//   4-5: direccion raw
//   6-7: posicion unidades
//
// Decoder ChirpStack:
//   velocidad_m_s = speed_raw / 10.0
//   direccion_grados = direction_raw / 100.0
// ============================================================

// ===== Ciclo de envio =====
static const uint64_t SEND_INTERVAL_US = 900000000ULL;  // 15 minutos
static const unsigned long SERIAL_BOOT_DELAY_MS = 2500UL;
static const unsigned long FAILURE_HOLD_MS = 5000UL;

// ===== RS485 =====
static const int RX_PIN = 16;
static const int TX_PIN = 17;
static const uint32_t MODBUS_BAUD = 9600;
static const uint8_t MODBUS_ID = 1;
static const uint16_t START_REGISTER = 0x0000;
static const uint8_t REGISTER_COUNT = 4;
static const uint8_t MAX_READ_RETRIES = 3;
static const uint16_t MODBUS_RESPONSE_TIMEOUT_MS = 500;
static const uint16_t MODBUS_INTERFRAME_DELAY_MS = 20;

// ===== LoRa SX1262 =====
SX1262 radio = new Module(5, 26, 14, 25);
static const float LORA_FREQUENCY_MHZ = 904.1f;
static const uint8_t LORA_SPREADING_FACTOR = 8;
static const float LORA_BANDWIDTH_KHZ = 125.0f;
static const uint8_t LORA_CODING_RATE = 5;
static const uint16_t LORA_PREAMBLE_LENGTH = 8;
static const int8_t LORA_OUTPUT_POWER_DBM = 14;
static const uint8_t LORA_SYNC_WORD = 0x34;

// ===== LoRaWAN ABP =====
static const uint8_t LORAWAN_FPORT = 2;
static const uint32_t DEV_ADDR = 0x01499D16;

static const uint8_t NWKSKEY[16] = {
  0x1F, 0x2E, 0x5D, 0xCC, 0x9E, 0x60, 0xD6, 0x37,
  0x03, 0xB7, 0x02, 0x67, 0xF6, 0x29, 0xBA, 0xAB
};

static const uint8_t APPSKEY[16] = {
  0x60, 0xDD, 0x20, 0x66, 0x99, 0x22, 0x06, 0x04,
  0x86, 0x4F, 0x02, 0xE1, 0x87, 0x9A, 0xEE, 0x98
};

HardwareSerial rs485(2);
Preferences prefs;
uint32_t fCntUp = 0;

struct WindFrame {
  uint16_t speedRaw;
  uint16_t levelRaw;
  uint16_t directionRaw;
  uint16_t positionUnitRaw;
};

enum RegisterMode : uint8_t {
  READ_HOLDING = 0x03,
  READ_INPUT = 0x04,
};

// ============================================================
// AES-128 para LoRaWAN ABP manual
// Base alineada con el patron que ya usas en Pluvi_5
// ============================================================
static const uint8_t sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static uint8_t xtime(uint8_t b) {
  return (b << 1) ^ ((b & 0x80) ? 0x1B : 0);
}

static void aes128_encrypt(const uint8_t *key, const uint8_t *in, uint8_t *out) {
  uint8_t s[16];
  uint8_t rk[16];
  memcpy(s, in, 16);
  memcpy(rk, key, 16);

  for (int i = 0; i < 16; i++) {
    s[i] ^= rk[i];
  }

  uint8_t rcon = 1;
  for (int round = 0; round < 10; round++) {
    uint8_t t[16];
    t[0] = sbox[s[0]];   t[1] = sbox[s[5]];   t[2] = sbox[s[10]];  t[3] = sbox[s[15]];
    t[4] = sbox[s[4]];   t[5] = sbox[s[9]];   t[6] = sbox[s[14]];  t[7] = sbox[s[3]];
    t[8] = sbox[s[8]];   t[9] = sbox[s[13]];  t[10] = sbox[s[2]];  t[11] = sbox[s[7]];
    t[12] = sbox[s[12]]; t[13] = sbox[s[1]];  t[14] = sbox[s[6]];  t[15] = sbox[s[11]];

    if (round < 9) {
      for (int c = 0; c < 4; c++) {
        uint8_t a0 = t[c * 4];
        uint8_t a1 = t[c * 4 + 1];
        uint8_t a2 = t[c * 4 + 2];
        uint8_t a3 = t[c * 4 + 3];
        t[c * 4]     = xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3;
        t[c * 4 + 1] = a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3;
        t[c * 4 + 2] = a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3;
        t[c * 4 + 3] = xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3);
      }
    }

    uint8_t newrk[16];
    newrk[0] = rk[0] ^ sbox[rk[13]] ^ rcon;
    newrk[1] = rk[1] ^ sbox[rk[14]];
    newrk[2] = rk[2] ^ sbox[rk[15]];
    newrk[3] = rk[3] ^ sbox[rk[12]];
    for (int i = 4; i < 16; i++) {
      newrk[i] = rk[i] ^ newrk[i - 4];
    }
    memcpy(rk, newrk, 16);
    rcon = xtime(rcon);

    for (int i = 0; i < 16; i++) {
      s[i] = t[i] ^ rk[i];
    }
  }

  memcpy(out, s, 16);
}

void lorawan_encrypt(const uint8_t *key, uint32_t devAddr, uint32_t fCnt,
                     uint8_t dir, const uint8_t *in, uint8_t *out, uint8_t len) {
  uint8_t block[16];
  uint8_t enc[16];
  uint8_t ctr = 1;

  for (int i = 0; i < len; i += 16) {
    block[0] = 0x01;
    block[1] = 0x00;
    block[2] = 0x00;
    block[3] = 0x00;
    block[4] = 0x00;
    block[5] = dir;
    block[6] = devAddr & 0xFF;
    block[7] = (devAddr >> 8) & 0xFF;
    block[8] = (devAddr >> 16) & 0xFF;
    block[9] = (devAddr >> 24) & 0xFF;
    block[10] = fCnt & 0xFF;
    block[11] = (fCnt >> 8) & 0xFF;
    block[12] = (fCnt >> 16) & 0xFF;
    block[13] = (fCnt >> 24) & 0xFF;
    block[14] = 0x00;
    block[15] = ctr++;

    aes128_encrypt(key, block, enc);
    for (int j = 0; j < 16 && (i + j) < len; j++) {
      out[i + j] = in[i + j] ^ enc[j];
    }
  }
}

void lorawan_mic(const uint8_t *key, uint32_t devAddr, uint32_t fCnt,
                 uint8_t dir, const uint8_t *msg, uint8_t msgLen, uint8_t *mic) {
  uint8_t b0[16] = {0};
  b0[0] = 0x49;
  b0[5] = dir;
  b0[6] = devAddr & 0xFF;
  b0[7] = (devAddr >> 8) & 0xFF;
  b0[8] = (devAddr >> 16) & 0xFF;
  b0[9] = (devAddr >> 24) & 0xFF;
  b0[10] = fCnt & 0xFF;
  b0[11] = (fCnt >> 8) & 0xFF;
  b0[12] = (fCnt >> 16) & 0xFF;
  b0[13] = (fCnt >> 24) & 0xFF;
  b0[15] = msgLen;

  uint8_t K1[16] = {0};
  uint8_t K2[16] = {0};
  uint8_t X[16] = {0};
  uint8_t Y[16];

  aes128_encrypt(key, X, Y);
  memcpy(K1, Y, 16);

  uint8_t msb = K1[0] & 0x80;
  for (int i = 0; i < 15; i++) {
    K1[i] = (K1[i] << 1) | (K1[i + 1] >> 7);
  }
  K1[15] = (K1[15] << 1) ^ (msb ? 0x87 : 0x00);

  memcpy(K2, K1, 16);
  msb = K2[0] & 0x80;
  for (int i = 0; i < 15; i++) {
    K2[i] = (K2[i] << 1) | (K2[i + 1] >> 7);
  }
  K2[15] = (K2[15] << 1) ^ (msb ? 0x87 : 0x00);

  memset(X, 0, 16);
  for (int i = 0; i < 16; i++) {
    X[i] ^= b0[i];
  }
  aes128_encrypt(key, X, Y);
  memcpy(X, Y, 16);

  int blocks = (msgLen + 15) / 16;
  for (int b = 0; b < blocks; b++) {
    uint8_t block[16] = {0};
    int rem = msgLen - b * 16;

    if (rem >= 16) {
      memcpy(block, msg + b * 16, 16);
      if (b == blocks - 1) {
        for (int i = 0; i < 16; i++) {
          block[i] ^= K1[i];
        }
      }
    } else {
      memcpy(block, msg + b * 16, rem);
      block[rem] = 0x80;
      for (int i = 0; i < 16; i++) {
        block[i] ^= K2[i];
      }
    }

    for (int i = 0; i < 16; i++) {
      X[i] ^= block[i];
    }
    aes128_encrypt(key, X, Y);
    memcpy(X, Y, 16);
  }

  memcpy(mic, Y, 4);
}

uint8_t lorawan_build_frame(uint8_t *buf, const uint8_t *payload, uint8_t payLen,
                            uint8_t fPort, uint32_t devAddr, uint32_t fCnt,
                            const uint8_t *nwkSKey, const uint8_t *appSKey) {
  uint8_t pos = 0;
  buf[pos++] = 0x80;
  buf[pos++] = devAddr & 0xFF;
  buf[pos++] = (devAddr >> 8) & 0xFF;
  buf[pos++] = (devAddr >> 16) & 0xFF;
  buf[pos++] = (devAddr >> 24) & 0xFF;
  buf[pos++] = 0x80;
  buf[pos++] = fCnt & 0xFF;
  buf[pos++] = (fCnt >> 8) & 0xFF;
  buf[pos++] = fPort;

  uint8_t encPayload[16] = {0};
  lorawan_encrypt(appSKey, devAddr, fCnt, 0, payload, encPayload, payLen);
  memcpy(buf + pos, encPayload, payLen);
  pos += payLen;

  uint8_t mic[4];
  lorawan_mic(nwkSKey, devAddr, fCnt, 0, buf, pos, mic);
  memcpy(buf + pos, mic, 4);
  pos += 4;

  return pos;
}

// ============================================================
// Modbus RTU manual
// ============================================================
uint16_t modbusCrc(const uint8_t *buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t j = 0; j < 8; j++) {
      crc = (crc & 0x0001) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
  }
  return crc;
}

const char *registerModeName(RegisterMode mode) {
  return mode == READ_HOLDING ? "Holding Registers (0x03)" : "Input Registers (0x04)";
}

bool radioConfigStep(const char *label, int state) {
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("OK radio ");
    Serial.println(label);
    return true;
  }

  Serial.print("ERROR radio ");
  Serial.print(label);
  Serial.print(": ");
  Serial.println(state);
  return false;
}

void printHexBuffer(const char *label, const uint8_t *buffer, uint8_t length) {
  Serial.print(label);
  for (uint8_t i = 0; i < length; i++) {
    if (buffer[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(buffer[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}

bool readRegisters(RegisterMode mode, uint16_t *values, uint8_t valueCount) {
  uint8_t request[8];
  request[0] = MODBUS_ID;
  request[1] = static_cast<uint8_t>(mode);
  request[2] = highByte(START_REGISTER);
  request[3] = lowByte(START_REGISTER);
  request[4] = 0x00;
  request[5] = valueCount;

  const uint16_t crc = modbusCrc(request, 6);
  request[6] = lowByte(crc);
  request[7] = highByte(crc);

  while (rs485.available()) {
    rs485.read();
  }

  Serial.print("Consultando ");
  Serial.println(registerModeName(mode));
  printHexBuffer("Modbus TX: ", request, sizeof(request));

  rs485.write(request, sizeof(request));
  delay(10);
  rs485.flush();

  const uint8_t expectedByteCount = valueCount * 2;
  const uint8_t expectedLength = 5 + expectedByteCount;
  uint8_t response[32];
  uint8_t count = 0;
  const unsigned long startedAt = millis();

  while ((millis() - startedAt) < MODBUS_RESPONSE_TIMEOUT_MS) {
    while (rs485.available() && count < sizeof(response)) {
      response[count++] = rs485.read();
    }

    if (count >= expectedLength) {
      delay(MODBUS_INTERFRAME_DELAY_MS);
      while (rs485.available() && count < sizeof(response)) {
        response[count++] = rs485.read();
      }
      break;
    }
  }

  if (count == 0) {
    Serial.print("Sin respuesta usando ");
    Serial.println(registerModeName(mode));
    return false;
  }

  printHexBuffer("Modbus RX: ", response, count);

  if (count < expectedLength) {
    Serial.print("Respuesta incompleta usando ");
    Serial.print(registerModeName(mode));
    Serial.print(": bytes=");
    Serial.println(count);
    return false;
  }

  if (response[0] != MODBUS_ID) {
    Serial.print("Slave ID invalido: ");
    Serial.println(response[0], HEX);
    return false;
  }

  if (response[1] != static_cast<uint8_t>(mode)) {
    if (response[1] == (static_cast<uint8_t>(mode) | 0x80)) {
      Serial.print("Excepcion Modbus en ");
      Serial.print(registerModeName(mode));
      Serial.print(": 0x");
      Serial.println(response[2], HEX);
    } else {
      Serial.print("Funcion inesperada: 0x");
      Serial.println(response[1], HEX);
    }
    return false;
  }

  if (response[2] != expectedByteCount) {
    Serial.print("ByteCount inesperado: ");
    Serial.println(response[2]);
    return false;
  }

  const uint16_t responseCrc = modbusCrc(response, expectedLength - 2);
  if (lowByte(responseCrc) != response[expectedLength - 2] ||
      highByte(responseCrc) != response[expectedLength - 1]) {
    Serial.println("CRC Modbus invalido");
    return false;
  }

  for (uint8_t i = 0; i < valueCount; i++) {
    values[i] = word(response[3 + i * 2], response[4 + i * 2]);
  }

  return true;
}

bool readWindFrame(WindFrame &frame) {
  uint16_t values[REGISTER_COUNT];

  for (uint8_t attempt = 0; attempt < MAX_READ_RETRIES; attempt++) {
    Serial.print("Intento Modbus ");
    Serial.print(attempt + 1);
    Serial.print("/");
    Serial.println(MAX_READ_RETRIES);

    if (readRegisters(READ_HOLDING, values, REGISTER_COUNT)) {
      Serial.println("Lectura valida en Holding Registers");
      frame.speedRaw = values[0];
      frame.levelRaw = values[1];
      frame.directionRaw = values[2];
      frame.positionUnitRaw = values[3];
      return true;
    }

    Serial.println("Holding Registers sin lectura valida");

    if (readRegisters(READ_INPUT, values, REGISTER_COUNT)) {
      Serial.println("Lectura valida en Input Registers");
      frame.speedRaw = values[0];
      frame.levelRaw = values[1];
      frame.directionRaw = values[2];
      frame.positionUnitRaw = values[3];
      return true;
    }

    Serial.println("Input Registers sin lectura valida");
    delay(200);
  }

  return false;
}

// ============================================================
// Utilidades
// ============================================================
void printFrame(const WindFrame &frame) {
  Serial.println("----------------------------------------");
  Serial.print("Velocidad raw: ");
  Serial.println(frame.speedRaw);
  Serial.print("Velocidad: ");
  Serial.print(frame.speedRaw / 10.0f, 1);
  Serial.println(" m/s");

  Serial.print("Nivel viento: ");
  Serial.println(frame.levelRaw);

  Serial.print("Direccion raw: ");
  Serial.println(frame.directionRaw);
  Serial.print("Direccion: ");
  Serial.print(frame.directionRaw / 100.0f, 2);
  Serial.println(" grados");

  Serial.print("Posicion unidades: ");
  Serial.println(frame.positionUnitRaw);
}

void buildPayload(const WindFrame &frame, uint8_t *payload) {
  payload[0] = highByte(frame.speedRaw);
  payload[1] = lowByte(frame.speedRaw);
  payload[2] = highByte(frame.levelRaw);
  payload[3] = lowByte(frame.levelRaw);
  payload[4] = highByte(frame.directionRaw);
  payload[5] = lowByte(frame.directionRaw);
  payload[6] = highByte(frame.positionUnitRaw);
  payload[7] = lowByte(frame.positionUnitRaw);
}

void goToSleep() {
  Serial.println("Entrando en deep sleep...");
  Serial.flush();
  esp_deep_sleep(SEND_INTERVAL_US);
}

void holdBeforeSleep(const char *reason) {
  Serial.println(reason);
  Serial.print("Esperando ");
  Serial.print(FAILURE_HOLD_MS);
  Serial.println(" ms antes de dormir para diagnostico...");
  Serial.flush();
  delay(FAILURE_HOLD_MS);
}

// ============================================================
// Setup principal
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(SERIAL_BOOT_DELAY_MS);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 ANEMOMETRO LoRaWAN ABP");
  Serial.println(" RS485 9600-8-N-1 / SX1262 904.1 MHz");
  Serial.println("========================================");

  prefs.begin("anemo", false);
  fCntUp = prefs.getUInt("fCnt", 0);
  Serial.print("FCnt restaurado: ");
  Serial.println(fCntUp);

  rs485.begin(MODBUS_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(100);
  Serial.println("RS485 inicializado");

  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR radio.begin(): ");
    Serial.println(state);
    prefs.end();
    holdBeforeSleep("Fallo inicializando la radio");
    goToSleep();
  }

  if (!radioConfigStep("setFrequency", radio.setFrequency(LORA_FREQUENCY_MHZ)) ||
      !radioConfigStep("setSpreadingFactor", radio.setSpreadingFactor(LORA_SPREADING_FACTOR)) ||
      !radioConfigStep("setBandwidth", radio.setBandwidth(LORA_BANDWIDTH_KHZ)) ||
      !radioConfigStep("setCodingRate", radio.setCodingRate(LORA_CODING_RATE)) ||
      !radioConfigStep("setSyncWord", radio.setSyncWord(LORA_SYNC_WORD)) ||
      !radioConfigStep("setPreambleLength", radio.setPreambleLength(LORA_PREAMBLE_LENGTH)) ||
      !radioConfigStep("setOutputPower", radio.setOutputPower(LORA_OUTPUT_POWER_DBM))) {
    prefs.end();
    holdBeforeSleep("Fallo configurando la radio");
    goToSleep();
  }

  Serial.println("Radio SX1262 inicializada");

  WindFrame frame;
  if (!readWindFrame(frame)) {
    prefs.end();
    holdBeforeSleep("No se pudo leer el anemometro");
    goToSleep();
  }

  printFrame(frame);

  uint8_t payload[8];
  buildPayload(frame, payload);

  uint8_t uplink[32];
  const uint8_t uplinkLen = lorawan_build_frame(
    uplink,
    payload,
    sizeof(payload),
    LORAWAN_FPORT,
    DEV_ADDR,
    fCntUp,
    NWKSKEY,
    APPSKEY
  );

  Serial.print("Enviando uplink LoRaWAN ABP, FCnt=");
  Serial.println(fCntUp);

  const int txState = radio.transmit(uplink, uplinkLen);
  if (txState == RADIOLIB_ERR_NONE) {
    Serial.println("Uplink enviado correctamente");
    fCntUp++;
    prefs.putUInt("fCnt", fCntUp);
  } else {
    Serial.print("Error transmitiendo: ");
    Serial.println(txState);
    holdBeforeSleep("Fallo en la transmision LoRaWAN");
  }

  prefs.end();
  goToSleep();
}

void loop() {
}
