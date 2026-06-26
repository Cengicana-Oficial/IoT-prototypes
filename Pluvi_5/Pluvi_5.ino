#include <RadioLib.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include <string.h>

// ===== DEEP SLEEP =====
#define INTERVALO_US 900000000ULL // 15 minutos

// ===== RADIO SX1262 =====
SX1262 radio = new Module(5, 26, 14, 25);

// ===== RS485 MODBUS =====
// ✅ Mismos pines del código que funcionó
#define RX_PIN 16
#define TX_PIN 17
#define BAUD_MODBUS 9600
HardwareSerial rs485(2);


static const uint32_t DEV_ADDR = 0x260CD359;

static const uint8_t NWKSKEY[16] = {
  0xe7, 0xa9, 0x47, 0x2e, 0x80, 0x20, 0xb2, 0x62,
  0x0c, 0x10, 0x12, 0x9e, 0x4a, 0x90, 0x07, 0x67
};

static const uint8_t APPSKEY[16] = {
  0x57, 0xaa, 0xb7, 0x34, 0x04, 0x54, 0x93, 0xf7,
  0x05, 0x03, 0x1d, 0x91, 0xa3, 0x26, 0x89, 0x5f
};

// ===== NVS =====
Preferences prefs;

// ===== ESTADO =====
uint32_t fCntUp = 0;
uint16_t mm10_anterior = 0xFFFF;

// ============================================================
// AES-128
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

static uint8_t xtime(uint8_t b) { return (b << 1) ^ ((b & 0x80) ? 0x1b : 0); }

static void aes128_encrypt(const uint8_t *key, const uint8_t *in, uint8_t *out) {
  uint8_t s[16], rk[16];
  memcpy(s, in, 16);
  memcpy(rk, key, 16);
  for (int i = 0; i < 16; i++) s[i] ^= rk[i];
  uint8_t rcon = 1;
  for (int round = 0; round < 10; round++) {
    uint8_t t[16];
    t[0]=sbox[s[0]];  t[1]=sbox[s[5]];  t[2]=sbox[s[10]]; t[3]=sbox[s[15]];
    t[4]=sbox[s[4]];  t[5]=sbox[s[9]];  t[6]=sbox[s[14]]; t[7]=sbox[s[3]];
    t[8]=sbox[s[8]];  t[9]=sbox[s[13]]; t[10]=sbox[s[2]]; t[11]=sbox[s[7]];
    t[12]=sbox[s[12]];t[13]=sbox[s[1]]; t[14]=sbox[s[6]]; t[15]=sbox[s[11]];
    if (round < 9) {
      for (int c = 0; c < 4; c++) {
        uint8_t a0=t[c*4],a1=t[c*4+1],a2=t[c*4+2],a3=t[c*4+3];
        t[c*4]  =xtime(a0)^xtime(a1)^a1^a2^a3;
        t[c*4+1]=a0^xtime(a1)^xtime(a2)^a2^a3;
        t[c*4+2]=a0^a1^xtime(a2)^xtime(a3)^a3;
        t[c*4+3]=xtime(a0)^a0^a1^a2^xtime(a3);
      }
    }
    uint8_t newrk[16];
    newrk[0]=rk[0]^sbox[rk[13]]^rcon;
    newrk[1]=rk[1]^sbox[rk[14]];
    newrk[2]=rk[2]^sbox[rk[15]];
    newrk[3]=rk[3]^sbox[rk[12]];
    for (int i=4;i<16;i++) newrk[i]=rk[i]^newrk[i-4];
    memcpy(rk,newrk,16);
    rcon=xtime(rcon);
    for (int i=0;i<16;i++) s[i]=t[i]^rk[i];
  }
  memcpy(out,s,16);
}

void lorawan_encrypt(const uint8_t *key, uint32_t devAddr, uint32_t fCnt,
                     uint8_t dir, const uint8_t *in, uint8_t *out, uint8_t len) {
  uint8_t block[16], enc[16];
  uint8_t ctr = 1;
  for (int i = 0; i < len; i += 16) {
    block[0]=0x01; block[1]=0x00; block[2]=0x00; block[3]=0x00; block[4]=0x00;
    block[5]=dir;
    block[6]=devAddr&0xFF; block[7]=(devAddr>>8)&0xFF;
    block[8]=(devAddr>>16)&0xFF; block[9]=(devAddr>>24)&0xFF;
    block[10]=fCnt&0xFF; block[11]=(fCnt>>8)&0xFF;
    block[12]=(fCnt>>16)&0xFF; block[13]=(fCnt>>24)&0xFF;
    block[14]=0x00; block[15]=ctr++;
    aes128_encrypt(key, block, enc);
    for (int j=0; j<16 && (i+j)<len; j++) out[i+j]=in[i+j]^enc[j];
  }
}

void lorawan_mic(const uint8_t *key, uint32_t devAddr, uint32_t fCnt,
                 uint8_t dir, const uint8_t *msg, uint8_t msgLen, uint8_t *mic) {
  uint8_t b0[16]={0};
  b0[0]=0x49; b0[5]=dir;
  b0[6]=devAddr&0xFF; b0[7]=(devAddr>>8)&0xFF;
  b0[8]=(devAddr>>16)&0xFF; b0[9]=(devAddr>>24)&0xFF;
  b0[10]=fCnt&0xFF; b0[11]=(fCnt>>8)&0xFF;
  b0[12]=(fCnt>>16)&0xFF; b0[13]=(fCnt>>24)&0xFF;
  b0[15]=msgLen;
  uint8_t K1[16]={0},K2[16]={0},X[16]={0},Y[16];
  aes128_encrypt(key,X,Y); memcpy(K1,Y,16);
  uint8_t msb=K1[0]&0x80;
  for(int i=0;i<15;i++) K1[i]=(K1[i]<<1)|(K1[i+1]>>7);
  K1[15]=(K1[15]<<1)^(msb?0x87:0x00);
  memcpy(K2,K1,16); msb=K2[0]&0x80;
  for(int i=0;i<15;i++) K2[i]=(K2[i]<<1)|(K2[i+1]>>7);
  K2[15]=(K2[15]<<1)^(msb?0x87:0x00);
  memset(X,0,16);
  for(int i=0;i<16;i++) X[i]^=b0[i];
  aes128_encrypt(key,X,Y); memcpy(X,Y,16);
  int blocks=(msgLen+15)/16;
  for(int b=0;b<blocks;b++) {
    uint8_t block[16]={0};
    int rem=msgLen-b*16;
    if(rem>=16) {
      memcpy(block,msg+b*16,16);
      if(b==blocks-1) for(int i=0;i<16;i++) block[i]^=K1[i];
    } else {
      memcpy(block,msg+b*16,rem);
      block[rem]=0x80;
      for(int i=0;i<16;i++) block[i]^=K2[i];
    }
    for(int i=0;i<16;i++) X[i]^=block[i];
    aes128_encrypt(key,X,Y); memcpy(X,Y,16);
  }
  memcpy(mic,Y,4);
}

uint8_t lorawan_build_frame(uint8_t *buf, const uint8_t *payload, uint8_t payLen,
                             uint8_t fPort, uint32_t devAddr, uint32_t fCnt,
                             const uint8_t *nwkSKey, const uint8_t *appSKey) {
  uint8_t pos=0;
  buf[pos++]=0x80;
  buf[pos++]=devAddr&0xFF; buf[pos++]=(devAddr>>8)&0xFF;
  buf[pos++]=(devAddr>>16)&0xFF; buf[pos++]=(devAddr>>24)&0xFF;
  buf[pos++]=0x80;
  buf[pos++]=fCnt&0xFF; buf[pos++]=(fCnt>>8)&0xFF;
  buf[pos++]=fPort;
  uint8_t encPayload[payLen];
  lorawan_encrypt(appSKey, devAddr, fCnt, 0, payload, encPayload, payLen);
  memcpy(buf+pos, encPayload, payLen); pos+=payLen;
  uint8_t mic[4];
  lorawan_mic(nwkSKey, devAddr, fCnt, 0, buf, pos, mic);
  memcpy(buf+pos, mic, 4); pos+=4;
  return pos;
}

// ============================================================
// MODBUS RS485
// ✅ Adaptado al patrón del código que funcionó
// ============================================================
uint16_t modbus_crc(uint8_t *buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++)
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

float leer_pluviometro() {
  // Construir frame con CRC calculado
  uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
  uint16_t crc = modbus_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  // Debug TX
  Serial.print("Modbus TX: ");
  for (int i = 0; i < 8; i++) {
    if (request[i] < 16) Serial.print("0");
    Serial.print(request[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Limpiar buffer RX antes de enviar
  while (rs485.available()) rs485.read();

  // ✅ Patrón correcto: write → delay(10) → flush()
  rs485.write(request, 8);
  delay(10);    // tiempo para que el módulo auto cambie TX → RX
  rs485.flush();

  // ✅ Leer con millis() (igual al código que funcionó)
  uint8_t buffer[64];
  int count = 0;
  unsigned long t = millis();

  while (millis() - t < 500) {
    if (rs485.available()) {
      while (rs485.available() && count < 64)
        buffer[count++] = rs485.read();
      delay(20); // esperar resto del frame
      while (rs485.available() && count < 64)
        buffer[count++] = rs485.read();
      break;
    }
  }

  // Debug RX
  Serial.print("Modbus RX: ");
  if (count == 0) {
    Serial.println("SIN RESPUESTA");
    return -1.0;
  }
  for (int i = 0; i < count; i++) {
    if (buffer[i] < 16) Serial.print("0");
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Validar longitud mínima
  if (count < 7) {
    Serial.println("Modbus: respuesta incompleta");
    return -1.0;
  }

  // Validar CRC
  uint16_t crc_resp = modbus_crc(buffer, 5);
  if ((crc_resp & 0xFF) != buffer[5] || ((crc_resp >> 8) & 0xFF) != buffer[6]) {
    Serial.println("Modbus: CRC incorrecto");
    return -1.0;
  }

  uint16_t raw = (buffer[3] << 8) | buffer[4];
  float mm = raw / 10.0;
  Serial.printf("Modbus OK: raw=%d → %.1f mm\n", raw, mm);
  return mm;
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Leer NVS
  prefs.begin("pluv", false);
  fCntUp        = prefs.getUInt("fCnt", 0);
  mm10_anterior = prefs.getUInt("mm10ant", 0xFFFF);
  Serial.printf("Despertando - FCnt: %lu | mm_ant: %.1f mm\n",
                fCntUp, mm10_anterior == 0xFFFF ? 0.0 : mm10_anterior / 10.0);

  // Iniciar RS485
  rs485.begin(BAUD_MODBUS, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(100);

  // Iniciar Radio
  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("ERROR radio: %d\n", state);
    prefs.end();
    esp_deep_sleep(INTERVALO_US);
  }
  radio.setFrequency(904.9);
  radio.setSpreadingFactor(10);
  radio.setBandwidth(125.0);
  radio.setCodingRate(5);
  radio.setSyncWord(0x34);
  radio.setPreambleLength(8);
  radio.setOutputPower(14);
  Serial.println("Radio OK");

  // Leer pluviómetro
  float mm = leer_pluviometro();
  if (mm < 0) {
    Serial.println("Error leyendo sensor, durmiendo...");
    prefs.end();
    esp_deep_sleep(INTERVALO_US);
  }

  uint16_t mm10_actual = (uint16_t)(mm * 10);

  // Primera vez
  if (mm10_anterior == 0xFFFF) {
    mm10_anterior = mm10_actual;
    prefs.putUInt("mm10ant", mm10_anterior);
    prefs.end();
    Serial.printf("Primer arranque - Baseline: %.1f mm\n", mm);
    esp_deep_sleep(INTERVALO_US);
  }

  // Calcular diferencial
  uint16_t diferencial = 0;
  if (mm10_actual >= mm10_anterior)
    diferencial = mm10_actual - mm10_anterior;

  float mm_diff = diferencial / 10.0;
  Serial.printf("Acumulado: %.1f mm | Anterior: %.1f mm | Diferencial: %.1f mm\n",
                mm, mm10_anterior / 10.0, mm_diff);

  // Payload: 2 bytes big-endian
  uint8_t payload[2];
  payload[0] = (diferencial >> 8) & 0xFF;
  payload[1] = diferencial & 0xFF;

  // Construir y enviar frame LoRaWAN
  uint8_t frame[32];
  uint8_t frameLen = lorawan_build_frame(
    frame, payload, sizeof(payload),
    1, DEV_ADDR, fCntUp, NWKSKEY, APPSKEY
  );

  Serial.printf("Enviando LoRa (FCnt=%lu, diff=%.1f mm)... ", fCntUp, mm_diff);
  int txState = radio.transmit(frame, frameLen);
  if (txState == RADIOLIB_ERR_NONE) {
    Serial.println("OK ✅");
  } else {
    Serial.printf("Error TX: %d\n", txState);
  }

  // Guardar NVS y dormir
  fCntUp++;
  prefs.putUInt("fCnt", fCntUp);
  prefs.putUInt("mm10ant", mm10_actual);
  prefs.end();

  Serial.println("Durmiendo 15 minutos...\n");
  esp_deep_sleep(INTERVALO_US);
}

void loop() {}