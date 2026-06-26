# Manual De Ejecucion — PluviometroBastWan (PV-Davies01)

Este proyecto es un **pluviometro inteligente** basado en la placa **BastWAN v2.0** (microcontrolador SAMD21G18 + modulo LoRa **RFM95/SX1276**) y el sensor de balancin **Davis 7345.440**. El BastWAN cuenta los **pulsos del reed switch** (cada pulso = 0.2 mm de lluvia) durante un intervalo, los empaqueta en un payload de **2 bytes** y los transmite por **LoRaWAN ABP** hacia **ChirpStack** (CENGICANA).

> A diferencia de `Pluvi_4`/`Pluvi_5` (ESP32 + SX1262 + RS485), aqui **no hay lectura Modbus**: el sensor es un contacto seco (reed switch) que se cuenta por interrupciones de flanco en el pin 9. Tampoco hay `deep sleep` con NVS: el BastWAN permanece activo, cuenta pulsos en `loop()` y transmite en forma **no bloqueante** con `millis()`.

## 1. Archivos entregados

- Sketch principal: [PluviometroBastWan.ino](./PluviometroBastWan.ino)
- Documentacion HTML interactiva: [index.html](./index.html)
- Este manual: [MANUAL_EJECUCION.md](./MANUAL_EJECUCION.md)

## 2. Conexion de pines

### 2.1 Tabla general

| Componente | Senal / Pin sensor | Pin BastWAN v2.0 | Notas |
|------------|--------------------|-----------------|-------|
| Davis 7345.440 | `GND` (hilo 1) | `GND` | Tierra comun |
| Davis 7345.440 | `SIG` (hilo 2) | `D9` | Entrada digital, `INPUT_PULLUP` |
| BastWAN (RFM95) | `CS` | `SS` | Definido por el board variant (BastWAN SDK) |
| BastWAN (RFM95) | `RST` | `RFM_RST` | Definido por el board variant |
| BastWAN (RFM95) | `DIO0` | `RFM_DIO0` | Definido por el board variant |
| BastWAN (RFM95) | `DIO1` | `RFM_DIO1` | Definido por el board variant |
| BastWAN (RFM95) | `DIO2` | `RFM_DIO2` | Definido por el board variant |
| BastWAN (RFM95) | `DIO5` | `RFM_DIO5` | Definido por el board variant |
| BastWAN | `USB-C` | — | Programacion y Monitor Serial |

> **Conexion fisica del sensor**: el cable del Davis 7345.440 tiene dos hilos. Uno va a `GND` y el otro a `D9`. El pin se configura como `INPUT_PULLUP`, asi que el reed switch (abierto) mantiene el pin en `HIGH` (pull-up interno a 3.3V); cuando el iman del balancin cierra el contacto, el pin baja a `LOW` (a GND).

> Los pines del modulo RFM95 (`CS`, `RST`, `DIO0..DIO5`) **no se configuran numericamente** en el sketch: se usan las macros `SS`, `RFM_RST`, `RFM_DIO0`, etc., que vienen predefinidas en el **board variant del BastWAN** (Electronic Cats). Solo necesitas usar la placa tal cual.

### 2.2 Mapeo en el codigo

```cpp
// Pin del pluviometro (contacto seco / reed switch)
#define RAIN_PIN 9
#define DEBOUNCE_MS 200
#define INTERVALO_MS 900000UL   // 15 minutos

// Pines del RFM95: tomados del board variant del BastWAN
const sRFM_pins RFM_pins = {
  .CS   = SS,
  .RST  = RFM_RST,
  .DIO0 = RFM_DIO0,
  .DIO1 = RFM_DIO1,
  .DIO2 = RFM_DIO2,
  .DIO5 = RFM_DIO5,
};
```

## 3. Funcionamiento (logica de ejecucion)

A diferencia de los pluviometros ESP32, este dispositivo **se mantiene despierto** y trabaja en `loop()`, sin deep sleep. El flujo:

### 3.1 `setup()` (una sola vez al arrancar)

1. **Serial** a `115200` con `delay(3000)` para que alcance a abrirse el Monitor Serial.
2. Configura `RAIN_PIN` (D9) como `INPUT_PULLUP` y lee el estado inicial (`estadoAnterior`).
3. **`lora.init()`**: verifica que el RFM95 responda por SPI. Si falla, entra en `while(1){}` (se detiene).
4. Configura LoRaWAN: `CLASS_A`, `SF8BW125`, canal `MULTI`, y carga `nwkSKey`, `appSKey`, `devAddr`.
5. Imprime el banner `PV-Davies01 - CENGICANA [v2.1]`.
6. Inicializa `ultimoReporte = millis()`.

### 3.2 `loop()` (se repite miles de veces por segundo)

**Parte A — Deteccion de pulsos (detector de flanco de bajada + debounce):**

1. `digitalRead(RAIN_PIN)`.
2. Detecta flanco de bajada: `estadoAnterior == HIGH && estadoActual == LOW`.
3. Aplica **debounce** de 200 ms: solo cuenta el pulso si pasaron mas de 200 ms desde `ultimoPulso`.
4. Incrementa `pulsos` e imprime el acumulado del minuto.

**Parte B — Transmision periodica (no bloqueante):**

1. Cada `INTERVALO_MS` (15 minutos) se dispara el bloque con `if (millis() - ultimoReporte >= INTERVALO_MS)`.
2. Calcula `mm = pulsos * 0.2` y el `valor` para ChirpStack = `pulsos * 2` (unidades de 0.1 mm).
3. Empaqueta `valor` en **2 bytes big-endian**.
4. Imprime el payload en hexadecimal.
5. `lora.sendUplink((char*)payload, 2, 0, 2)` → **encola** el uplink (no transmite aun).
6. Intenta leer downlink con `lora.readData(outStr)`.
7. Resetea `pulsos = 0` e incrementa `minuto = (minuto + 1) % 1440`.

**Parte C — Mantenimiento LoRa:**

- `lora.update()` se ejecuta en **cada** iteracion del `loop()`. Es la maquina de estados de la libreria: procesa la cola de uplinks, abre las ventanas RX1/RX2 de CLASS A, gestiona retransmisiones y downlinks.

## 4. Medida y conversion de lluvia

- 1 basculazo del balancin (Davis 7345.440) = **0.2 mm** de precipitacion = **1 pulso**.
- ChirpStack espera unidades de **0.1 mm** por valor del payload, entonces cada pulso vale **2 unidades**.

```cpp
uint16_t valor = (uint16_t)(pulsos * 2);   // pulsos -> unidades de 0.1 mm
byte payload[2];
payload[0] = (valor >> 8) & 0xFF;          // MSB (big-endian)
payload[1] =  valor       & 0xFF;          // LSB
```

| Pulsos | Lluvia real | `valor` (payload) | Bytes (hex) |
|--------|-------------|-------------------|-------------|
| 0 | 0.0 mm | 0 | `00 00` |
| 1 | 0.2 mm | 2 | `00 02` |
| 5 | 1.0 mm | 10 | `00 0A` |
| 25 | 5.0 mm | 50 | `00 32` |
| 128 | 25.6 mm | 256 | `01 00` |

Decoder esperado en ChirpStack:

- `valor = payload[0] * 256 + payload[1]`
- `lluvia_mm = valor * 0.1`  (o directamente `pulsos * 0.2`)

## 5. Configuracion LoRaWAN

```cpp
const char *devAddr = "260ccc34";
const char *nwkSKey = "05ee44efb4045dbcd942fdfaa8c2546c";
const char *appSKey = "8b7921986ff1b498ae383c0cd2f6c44f";
```

Parametros de la sesion:

- Activacion: **ABP** (claves pre-programadas, sin `join`).
- Clase: `CLASS_A` (transmite y abre RX1/RX2; lo demas del tiempo duerre/ejecuta).
- Data Rate: `SF8BW125`.
- Canal: `MULTI` (frequency hopping, obligatorio por regulacion).
- `sendUplink(payload, 2, port=0, confirmed=2)` → uplink confirmado (espera ACK; si no llega, la libreria retransmite).

> En ABP solo se usan `devAddr`, `nwkSKey` y `appSKey`. Confirma que coincidan con el device registrado en ChirpStack.

## 6. Timing y debounce

- **`millis()` en lugar de `delay()`**: nunca se congela el micro. El `loop()` sigue leyendo pulsos mientras espera el proximo reporte (programacion **no bloqueante**).
- **Debounce de 200 ms**: el reed switch rebota al cerrarse; 200 ms cubre el rebote mecanico del balancin sin perder basculazos reales (que ocurren a >500 ms de separacion incluso en tormenta).

## 7. Preparar el entorno (BastWAN)

1. Instala el soporte de placas **BastWAN / Electronic Cats** en el Arduino IDE (Gestor de placas → buscar `Electronic Cats` / `BastWAN`).
2. Selecciona la placa **BastWAN** (o SAMD21 compatible con el variant de Electronic Cats) y el puerto COM correcto.
3. La libreria **`lorawan.h`** viene con el SDK del BastWAN; asegurate de tenerla instalada.

## 8. Compilar en Arduino IDE

1. Abre [PluviometroBastWan.ino](./PluviometroBastWan.ino).
2. Selecciona la placa **BastWAN**.
3. Presiona `Verify` y luego `Upload`.

## 9. Configurar decoder en ChirpStack

En el dispositivo de ChirpStack:

1. Abre el device.
2. Ve a `Codec`.
3. Pega un decoder que reconstruya `valor = (payload[0] << 8) | payload[1]` y exponga `lluvia_mm = valor * 0.1`.
4. Guarda.

## 10. Que debes ver por serial

Arranque:

```text
========================================
  PV-Davies01 - CENGICANA [v2.1]
  Davis 7345.440 + BastWAN ABP
  0.2 mm/pulso = 2 unidades (0.1 mm)
========================================
```

Al detectar un pulso:

```text
  [PULSO] Min:1 | Acumulado: 1 pulsos = 0.2 mm
```

Al transmitir (cada 15 minutos):

```text
----------------------------------------
Minuto #1 | Pulsos: 5 | Lluvia: 1.0 mm
Enviando payload (0.1mm): 0x000A = 10 unidades
Uplink encolado correctamente
----------------------------------------
```

## 11. Solucion de problemas

- **`ERROR: LoRa no detectado`** al arrancar — el RFM95 no responde por SPI. Revisa la placa/varaint BastWAN, alimentacion y que la libreria `lorawan.h` corresponda a Electronic Cats. El dispositivo queda detenido en `while(1)`.
- **Cuenta pulsos fantasma / de mas** — rebotes no filtrados o ruido en el cable del sensor. Aumenta `DEBOUNCE_MS` o acorta/mejora el cable del Davis (apantallado, lejos de fuentes de ruido).
- **No cuenta pulsos** — verifica `GND` comun y que el reed switch del Davis funcione; comprueba que `RAIN_PIN` sea el pin 9 del variant BastWAN.
- **No llegan datos a ChirpStack**:
  - Confirma `devAddr`, `nwkSKey` y `appSKey` del device correcto.
  - Verifica que `CLASS_A` / `SF8BW125` coincidan con el perfil del device.
  - Asegura que `lora.update()` se siga llamando en cada iteracion (no introduzcas `delay()` largos que la bloqueen).
  - Revisa alcance respecto al gateway y antena del BastWAN.
- **Pulsos se pierden durante la transmision** — si agregas `delay()` largos, se detiene la lectura del pin y `lora.update()`. Manten la logica no bloqueante con `millis()`.
- **`lluvia_mm` siempre duplicado/mitad en el dashboard** — revisa el decoder de ChirpStack: debe interpretar el payload como big-endian y aplicar `valor * 0.1` (no `* 0.2`).