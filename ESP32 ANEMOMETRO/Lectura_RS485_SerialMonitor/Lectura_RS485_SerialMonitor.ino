#include <Arduino.h>
#include <HardwareSerial.h>
#include <ModbusMaster.h>

// ============================================================
// ESP32 + RS485 to TTL + Anemometro Modbus RTU
// Solo lectura local por monitor serial
//
// Conexion RS485 to TTL -> ESP32
//   RO -> GPIO16 (RX2)
//   DI -> GPIO17 (TX2)
//   GND -> GND
//
// Conexion sensor:
//   Amarillo -> A
//   Verde    -> B
//   Rojo     -> alimentacion del sensor
//   Negro    -> GND de la fuente del sensor
// ============================================================

static const int RX_PIN = 16;
static const int TX_PIN = 17;

static const uint32_t MODBUS_BAUD = 9600;
static const uint8_t MODBUS_ID = 1;
static const uint16_t START_REGISTER = 0x0000;
static const uint8_t REGISTER_COUNT = 4;

static const unsigned long READ_INTERVAL_MS = 1000UL;
static const uint8_t MAX_RETRIES = 3;

static const float WIND_SPEED_SCALE = 10.0f;
static const float WIND_DIRECTION_SCALE = 100.0f;

HardwareSerial rs485(2);
ModbusMaster node;
unsigned long lastReadAt = 0;

struct WindFrame {
  uint16_t speedRaw;
  uint16_t levelRaw;
  uint16_t directionRaw;
  uint16_t positionUnitRaw;
};

enum RegisterMode : uint8_t {
  READ_HOLDING = 0,
  READ_INPUT = 1,
};

const char *registerModeName(RegisterMode mode) {
  switch (mode) {
    case READ_HOLDING:
      return "Holding Registers (funcion 0x03)";
    case READ_INPUT:
      return "Input Registers (funcion 0x04)";
    default:
      return "Desconocido";
  }
}

const char *modbusStatusName(uint8_t status) {
  switch (status) {
    case ModbusMaster::ku8MBSuccess:
      return "Exito";
    case ModbusMaster::ku8MBIllegalFunction:
      return "Funcion ilegal";
    case ModbusMaster::ku8MBIllegalDataAddress:
      return "Direccion ilegal";
    case ModbusMaster::ku8MBIllegalDataValue:
      return "Dato ilegal";
    case ModbusMaster::ku8MBSlaveDeviceFailure:
      return "Falla interna del esclavo";
    case ModbusMaster::ku8MBInvalidSlaveID:
      return "Slave ID invalido";
    case ModbusMaster::ku8MBInvalidFunction:
      return "Funcion invalida en respuesta";
    case ModbusMaster::ku8MBResponseTimedOut:
      return "Timeout de respuesta";
    case ModbusMaster::ku8MBInvalidCRC:
      return "CRC invalido";
    default:
      return "Codigo no identificado";
  }
}

uint8_t executeRead(RegisterMode mode) {
  if (mode == READ_HOLDING) {
    return node.readHoldingRegisters(START_REGISTER, REGISTER_COUNT);
  }

  return node.readInputRegisters(START_REGISTER, REGISTER_COUNT);
}

bool readWindRegisters(RegisterMode mode, WindFrame &frame) {
  for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++) {
    const uint8_t result = executeRead(mode);
    if (result == node.ku8MBSuccess) {
      frame.speedRaw = node.getResponseBuffer(0);
      frame.levelRaw = node.getResponseBuffer(1);
      frame.directionRaw = node.getResponseBuffer(2);
      frame.positionUnitRaw = node.getResponseBuffer(3);
      return true;
    }

    Serial.print("Error Modbus [");
    Serial.print(registerModeName(mode));
    Serial.print("] intento ");
    Serial.print(attempt + 1);
    Serial.print(": 0x");
    Serial.print(result, HEX);
    Serial.print(" - ");
    Serial.println(modbusStatusName(result));
    delay(200);
  }

  return false;
}

void printFrame(const WindFrame &frame) {
  float windSpeed = frame.speedRaw / WIND_SPEED_SCALE;
  float windDirection = frame.directionRaw / WIND_DIRECTION_SCALE;

  Serial.println("----------------------------------------");
  Serial.print("Registro 0 - Velocidad raw: ");
  Serial.println(frame.speedRaw);
  Serial.print("Velocidad procesada: ");
  Serial.print(windSpeed, 1);
  Serial.println(" m/s");

  Serial.print("Registro 1 - Nivel viento: ");
  Serial.println(frame.levelRaw);

  Serial.print("Registro 2 - Direccion raw: ");
  Serial.println(frame.directionRaw);
  Serial.print("Direccion procesada: ");
  Serial.print(windDirection, 2);
  Serial.println(" grados");

  Serial.print("Registro 3 - Posicion unidades: ");
  Serial.println(frame.positionUnitRaw);
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESP32 - Lector RS485 anemometro");
  Serial.println(" Modbus RTU 9600-8-N-1");
  Serial.println(" GPIO16 RX2 / GPIO17 TX2");
  Serial.println("========================================");

  rs485.begin(MODBUS_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
  node.begin(MODBUS_ID, rs485);

  Serial.println("Puerto RS485 inicializado");
  Serial.println("Probando lectura de registros 0..3 cada 1 segundo");
  Serial.println("1) Holding Registers (0x03)");
  Serial.println("2) Input Registers   (0x04)");
  Serial.println("Si ambos responden 0xE2, el problema es fisico o de direccionamiento");
}

void loop() {
  if (millis() - lastReadAt < READ_INTERVAL_MS) {
    return;
  }

  lastReadAt = millis();

  WindFrame frame;
  if (readWindRegisters(READ_HOLDING, frame)) {
    Serial.println("Lectura valida usando Holding Registers");
    printFrame(frame);
    return;
  }

  if (readWindRegisters(READ_INPUT, frame)) {
    Serial.println("Lectura valida usando Input Registers");
    printFrame(frame);
  } else {
    Serial.println("No se pudo leer el sensor en ninguna tabla Modbus");
    Serial.println("Revisa: A/B invertidos, GND comun, alimentacion, ID 1 y tipo de transceptor");
  }
}
