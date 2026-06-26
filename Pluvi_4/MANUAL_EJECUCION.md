# Manual De Ejecucion — Pluvi_4

Este proyecto lee un **pluviometro RS485 Modbus RTU** con un `ESP32`, calcula el **diferencial de lluvia** respecto a la lectura anterior, arma un payload binario de `2 bytes`, construye manualmente el frame `LoRaWAN ABP` (AES-128 propio) y lo transmite con un modulo `SX1262` (RadioLib). Tras cada ciclo entra en `deep sleep` durante **15 minutos**.

> **Pluvi_4** es la primera version del firmware del pluviometro ESP32 + SX1262 + RS485. La version `Pluvi_5` mejora la rutina de lectura Modbus; ver [Diferencias con Pluvi_5](#13-diferencias-con-pluvi_5) al final.

## 1. Archivos entregados

- Sketch principal: [Pluvi_4.ino](./Pluvi_4.ino)
- Este manual: [MANUAL_EJECUCION.md](./MANUAL_EJECUCION.md)

## 2. Conexion de pines

### 2.1 Tabla general

| Componente | Senal / Pin modulo | GPIO ESP32 | Notas |
|------------|--------------------|------------|-------|
| Convertidor RS485 | `RO` (Receiver Output) | `GPIO16` | RX del ESP32 |
| Convertidor RS485 | `DI` (Driver Input) | `GPIO17` | TX del ESP32 |
| Convertidor RS485 | `GND` | `GND` | Comun con ESP32 y sensor |
| SX1262 (LoRa) | `NSS` (CS) | `GPIO5` | Chip Select SPI |
| SX1262 (LoRa) | `DIO1` | `GPIO26` | Interrupcion |
| SX1262 (LoRa) | `NRST` | `GPIO14` | Reset |
| SX1262 (LoRa) | `BUSY` | `GPIO25` | Busy (RadioLib) |
| SX1262 (LoRa) | `SCK` | `GPIO18` | SPI clock (HW) |
| SX1262 (LoRa) | `MOSI` | `GPIO23` | SPI MOSI (HW) |
| SX1262 (LoRa) | `MISO` | `GPIO19` | SPI MISO (HW) |
| SX1262 (LoRa) | `VCC` | `3V3` | Alimentacion |
| SX1262 (LoRa) | `GND` | `GND` | Comun |

> No conectar `DE/RE`. El proyecto asume un convertidor RS485 **auto-direction** (cambia TX→RX automaticamente). Confirma `GND` comun entre ESP32, convertidor y sensor.

### 2.2 Mapeo en el codigo

```cpp
// Radio SX1262: Module(NSS, DIO1, NRST, BUSY)
SX1262 radio = new Module(5, 26, 14, 25);

// RS485 Modbus
#define RX_PIN 16
#define TX_PIN 17
#define BAUD_MODBUS 9600
HardwareSerial rs485(2);   // UART2
```

## 3. Funcionamiento (logica de ejecucion)

El ESP32 **no usa `loop()`**: hace todo el trabajo dentro de `setup()` y luego entra en `deep sleep`. Al despertar (cada 15 min), `setup()` vuelve a correr desde el inicio. El estado persistente (contador `FCnt` y `mm10_anterior`) se guarda en **NVS** (`Preferences`).

Flujo por ciclo:

1. **Arranca** `Serial` (115200) y lee el NVS:
   - `fCnt`: frame counter LoRaWAN.
   - `mm10ant`: ultima lectura acumulada (en unidades de 0.1 mm), `0xFFFF` si es primer arranque.
2. **Inicia RS485** a `9600 8N1` sobre los pines 16/17.
3. **Inicia la radio** SX1262; si falla, duerme.
4. Configura los parametros de radio (ver seccion 6).
5. **Lee el pluviometro** por Modbus (ver seccion 5). Si hay error, duerme sin transmitir.
6. Si es el **primer arranque** (`mm10_anterior == 0xFFFF`), guarda el valor como baseline y duerme (no transmite).
7. **Calcula el diferencial**: `diferencial = mm10_actual - mm10_anterior` (si `actual >= anterior`; si no, 0). Es la lluvia caida en los ultimos 15 minutos.
8. Empaqueta `diferencial` en **2 bytes big-endian** y construye el frame LoRaWAN ABP.
9. **Transmite** el frame con `radio.transmit()`.
10. **Incrementa `fCnt`**, actualiza `mm10_anterior` en NVS y entra en `deep sleep` 15 minutos.

## 4. Deep sleep

```cpp
#define INTERVALO_US 900000000ULL  // 15 minutos en microsegundos
esp_deep_sleep(INTERVALO_US);
```

`loop()` queda vacio porque el programa nunca llega a ejecutarlo (siempre duerme al final de `setup()`).

## 5. Configuracion Modbus

- Baudrate: `9600`, `8N1`
- Slave ID = `1`
- Funcion: `0x03` (Holding Registers)
- Registro inicial: `0`, cantidad: `1`
- Request: `{0x01, 0x03, 0x00, 0x00, 0x00, 0x01, CRC_lo, CRC_hi}`
- Espera la respuesta con timeout de `1000 ms`, leyendo **7 bytes**.
- Valida CRC (Modbus) y extrae `raw = (resp[3] << 8) | resp[4]`.
- Conversion: `mm = raw / 10.0` (el sensor entrega decimas de mm).

## 6. Parametros de radio

Configurados en el sketch:

- Frecuencia: `904.9 MHz`
- Spreading Factor: `10`
- Bandwidth: `125 kHz`
- Coding Rate: `4/5` (`setCodingRate(5)`)
- SyncWord: `0x34`
- Preamble: `8`
- Potencia: `14 dBm`
- `FPort = 1`

Si tu gateway/perfil usan otro canal, ajusta `radio.setFrequency(...)`.

## 7. Formato del payload uplink

Payload de **2 bytes**, big-endian, con el **diferencial de lluvia** en unidades de `0.1 mm`:

```cpp
uint8_t payload[2];
payload[0] = (diferencial >> 8) & 0xFF;  // MSB
payload[1] =  diferencial       & 0xFF;  // LSB
```

Decoder esperado en ChirpStack:

- `diferencial_raw = payload[0] * 256 + payload[1]`
- `lluvia_mm = diferencial_raw / 10.0`

Ejemplos:

| Diferencial (mm) | unidades (0.1mm) | Payload (hex) |
|------------------|------------------|---------------|
| 0.0 mm | 0 | `00 00` |
| 0.5 mm | 5 | `00 05` |
| 2.3 mm | 23 | `00 17` |
| 25.6 mm | 256 | `01 00` |

## 8. Credenciales LoRaWAN ABP

```cpp
static const uint32_t DEV_ADDR = 0x260CD358;

static const uint8_t NWKSKEY[16] = {
  0xe7, 0xa9, 0x47, 0x2e, 0x80, 0x20, 0xb2, 0x62,
  0x0c, 0x10, 0x12, 0x9e, 0x4a, 0x90, 0x07, 0x67
};

static const uint8_t APPSKEY[16] = {
  0x57, 0xaa, 0xb7, 0x34, 0x04, 0x54, 0x93, 0xf7,
  0x05, 0x03, 0x1d, 0x91, 0xa3, 0x26, 0x89, 0x5f
};
```

> En ABP solo se usan `DevAddr`, `NwkSKey` y `AppSKey`. Confirma que coincidan con el device registrado en ChirpStack.

## 9. Construccion del frame LoRaWAN

El sketch implementa AES-128 a mano (`aes128_encrypt`, `lorawan_encrypt`, `lorawan_mic`, `lorawan_build_frame`) en lugar de usar la pila LMIC. El frame sigue la estructura ABP estandar:

```
MHDR(0x80) | DevAddr(4) | FCtrl(0x80) | FCnt(2) | FPort | payload enc | MIC(4)
```

- `payload` se cifra con `AppSKey` (modo contador, LoRaWAN).
- `MIC` se calcula con `NwkSKey` (CMAC).

## 10. Compilar en Arduino IDE

1. Abre [Pluvi_4.ino](./Pluvi_4.ino).
2. Selecciona `ESP32 Dev Module`.
3. Verifica que las librerias `RadioLib` y `Preferences` esten instaladas.
4. Presiona `Verify` y luego `Upload`.

## 11. Compilar con arduino-cli

```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" `
  compile --fqbn esp32:esp32:esp32 `
  --libraries "C:\Users\enman\Documents\Arduino\libraries" `
  "C:\Users\enman\Documents\Personal\INNOVAHUB\GitCengiCaña\IoT-prototypes\Pluvi_4"
```

## 12. Que debes ver por serial

Salida tipica de un ciclo correcto (115200 baud):

```text
Despertando - FCnt: 0 | mm_ant: 12.5 mm
Radio OK
Acumulado: 13.0 mm | Anterior: 12.5 mm | Diferencial: 0.5 mm
Enviando (FCnt=0, diff=0.5 mm)... OK ✅
Durmiendo 1 minuto...
```

En el **primer arranque** (baseline):

```text
Despertando - FCnt: 0 | mm_ant: 0.0 mm
Radio OK
Primer arranque - Baseline: 13.0 mm
```

## 13. Diferencias con Pluvi_5

`Pluvi_4` y `Pluvi_5` comparten arquitectura, pines y credenciales casi identicas (la unica diferencia en claves es el `DevAddr`). Las diferencias principales estan en la rutina de lectura Modbus `leer_pluviometro()`:

| Aspecto | Pluvi_4 | Pluvi_5 |
|---------|---------|---------|
| `DEV_ADDR` | `0x260CD358` | `0x260CD359` |
| Patron de envio TX | `write()` → `flush()` directo | `write()` → `delay(10)` → `flush()` (espera auto-direction) |
| Lectura RX | Lee exactamente **7 bytes** con timeout de 1000 ms | Lee hasta **64 bytes** con bucle `millis()` (500 ms) + `delay(20)` extra |
| Debug por serial | Solo mensajes de estado | Imprime frames `Modbus TX` y `Modbus RX` en hex |
| Mensaje final | `"Durmiendo 1 minuto..."` (texto, igual 15 min real) | `"Durmiendo 15 minutos..."` |

`Pluvi_5` es mas robusto frente a convertidores RS485 auto-direction lentos y facilita el diagnostico por su mayor trazado serial.

## 14. Solucion de problemas

- **`Modbus: timeout`** — sin respuesta del sensor. Revisa cableado `A/B`, `GND` comun, alimentacion del sensor y baudrate.
- **`Modbus: CRC incorrecto`** — llega ruido o frame incompleto. Verifica longitudes de cable y terminacion RS485.
- **`ERROR radio: <n>`** — fallo al iniciar el SX1262. Revisa pines SPI/NSS/DIO1/NRST/BUSY y alimentacion `3V3`.
- **No llegan datos a ChirpStack**:
  - Confirma `DevAddr`, `NwkSKey` y `AppSKey` del device correcto.
  - Verifica que el gateway escuche `904.9 MHz`.
  - Revisa que el `FPort` coincide con el esperado por el decoder.
- **El diferencial siempre es 0** — el sensor reporta un valor acumulado no creciente, o la lectura previa no se guardo bien en NVS. Borra el namespace `pluv` o flashea de nuevo.
- **Reinicia el baseline** — para forzarlo, borra la entrada `mm10ant` en NVS (o cambia temporalmente el nombre del namespace `prefs.begin("pluv", ...)`).