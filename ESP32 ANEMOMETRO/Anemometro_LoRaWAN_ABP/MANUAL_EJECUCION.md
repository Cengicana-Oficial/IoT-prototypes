# Manual De Ejecucion

Este proyecto lee el anemometro RS485 Modbus RTU con un `ESP32`, arma un payload binario de `8 bytes`, construye manualmente el frame `LoRaWAN ABP` y lo transmite con un `XL1262-P01 / SX1262`.

## 1. Archivos entregados

- Sketch principal: [Anemometro_LoRaWAN_ABP.ino](<C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\ESP32 ANEMOMETRO\Anemometro_LoRaWAN_ABP\Anemometro_LoRaWAN_ABP.ino>)
- Decoder ChirpStack: [chirpstack_decoder.js](<C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\ESP32 ANEMOMETRO\Anemometro_LoRaWAN_ABP\chirpstack_decoder.js>)
- Este manual: [MANUAL_EJECUCION.md](<C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\ESP32 ANEMOMETRO\Anemometro_LoRaWAN_ABP\MANUAL_EJECUCION.md>)

## 2. Conexion RS485 al ESP32

- `RO` del convertidor RS485 -> `GPIO16` del ESP32
- `DI` del convertidor RS485 -> `GPIO17` del ESP32
- `GND` del convertidor RS485 -> `GND` del ESP32

No agregar `DE/RE`. Este proyecto asume el mismo convertidor auto-direction que ya estas usando.

## 3. Conexion del modulo LoRa XL1262-P01

- `NSS` -> `GPIO5`
- `DIO1` -> `GPIO26`
- `NRST` -> `GPIO14`
- `BUSY` -> `GPIO25`
- `SCK` -> `GPIO18`
- `MOSI` -> `GPIO23`
- `MISO` -> `GPIO19`
- `VCC` -> `3V3`
- `GND` -> `GND`

## 4. Configuracion Modbus

- `9600`
- `8N1`
- `Slave ID = 1`
- Registros `0..3`
- El sketch prueba `Holding Registers (0x03)` e `Input Registers (0x04)`

## 5. Formato del payload uplink

El payload es de `8 bytes`, en `big-endian`:

- Byte `0-1`: velocidad raw
- Byte `2-3`: nivel viento
- Byte `4-5`: direccion raw
- Byte `6-7`: posicion unidades

Escalas esperadas en ChirpStack:

- `speed_m_s = speed_raw / 10.0`
- `direction_degrees = direction_raw / 100.0`

Ejemplo:

- `speed_raw = 73` -> `7.3 m/s`
- `direction_raw = 3345` -> `33.45 grados`

## 6. Credenciales LoRaWAN ABP

Actualmente el sketch trae:

- `DevAddr = 01499D16`
- `NwkSKey = 1F2E5DCC9E60D63703B70267F629BAAB`
- `AppSKey = 60DD206699220604864F02E1879AEE98`

Tambien tienes en ChirpStack el `DevEUI = B27460BA109E83C2`, pero en este sketch ABP solo se usan `DevAddr`, `NwkSKey` y `AppSKey`.

## 7. Parametros de radio

El sketch quedo configurado asi:

- Frecuencia: `904.1 MHz`
- `SF8`
- `BW 125 kHz`
- `CR 4/5`
- `SyncWord 0x34`
- `Preamble 8`
- Potencia: `14 dBm`
- `FPort = 2`

Si tu gateway o perfil real usan otro canal/frecuencia, cambia `LORA_FREQUENCY_MHZ` en el sketch.

## 8. Configurar decoder en ChirpStack

En el dispositivo de ChirpStack:

1. Abre el device.
2. Ve a `Codec`.
3. Pega el contenido de [chirpstack_decoder.js](<C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\ESP32 ANEMOMETRO\Anemometro_LoRaWAN_ABP\chirpstack_decoder.js>).
4. Guarda.

El decoder publicara:

- `speed_raw`
- `speed_m_s`
- `level_raw`
- `direction_raw`
- `direction_degrees`
- `position_unit_raw`

## 9. Compilar en Arduino IDE

1. Abre [Anemometro_LoRaWAN_ABP.ino](<C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\ESP32 ANEMOMETRO\Anemometro_LoRaWAN_ABP\Anemometro_LoRaWAN_ABP.ino>)
2. Selecciona `ESP32 Dev Module`
3. Verifica que las librerias `RadioLib` esten disponibles
4. Presiona `Verify`
5. Presiona `Upload`

## 10. Compilar con arduino-cli

```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32 --libraries "C:\Users\enman\Documents\Arduino\libraries" "C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\ESP32 ANEMOMETRO\Anemometro_LoRaWAN_ABP"
```

Si estas usando las librerias dentro de `C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\libraries`, compila asi:

```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32 --libraries "C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\libraries" "C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\ESP32 ANEMOMETRO\Anemometro_LoRaWAN_ABP"
```

## 11. Que debes ver por serial

Si la lectura y el armado del payload estan bien:

```text
ESP32 ANEMOMETRO LoRaWAN ABP
FCnt restaurado: 0
RS485 inicializado
Radio SX1262 inicializada
Velocidad raw: 73
Velocidad: 7.3 m/s
Nivel viento: 4
Direccion raw: 3345
Direccion: 33.45 grados
Posicion unidades: 7
Enviando uplink LoRaWAN ABP, FCnt=0
Uplink enviado correctamente
Entrando en deep sleep...
```

## 12. Si no llegan datos a ChirpStack

- Confirma que la `APPSKEY` real ya fue cargada
- Verifica que `DevAddr` y `NwkSKey` correspondan al device correcto
- Revisa que el gateway este escuchando `904.1 MHz`
- Si no hay respuesta Modbus, prueba invertir `A` y `B`
- Confirma `GND` comun entre ESP32, convertidor y fuente del sensor
- Si tu sensor solo responde en una tabla Modbus, el sketch ya prueba `0x03` y `0x04`
