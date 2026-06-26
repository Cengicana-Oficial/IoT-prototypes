# Manual De Ejecucion

Este subproyecto sirve para leer el anemometro por `RS485 to TTL` usando un `ESP32` y mostrar los datos en el monitor serial.

## 1. Archivo principal

- Sketch: [Lectura_RS485_SerialMonitor.ino](C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\BastWan_Anemometro_LoRa\Lectura_RS485_SerialMonitor\Lectura_RS485_SerialMonitor.ino)

## 2. Conexion ESP32 a RS485 to TTL

- `GPIO16` del ESP32 -> `RO` del modulo RS485
- `GPIO17` del ESP32 -> `DI` del modulo RS485
- `GND` del ESP32 -> `GND` del modulo RS485

## 3. Conexion del anemometro al RS485

- `Amarillo` -> `A`
- `Verde` -> `B`
- `Rojo` -> alimentacion del sensor
- `Negro` -> tierra de la fuente del sensor

## 4. Configuracion Modbus

El sketch esta configurado en:

- `9600`
- `8N1`
- `ID = 1`
- lectura de registros `0..3`

## 5. Que muestra el monitor serial

- `Registro 0`: velocidad raw y velocidad en `m/s`
- `Registro 1`: nivel de viento
- `Registro 2`: direccion raw y direccion en grados
- `Registro 3`: posicion en unidades

## 6. Compilar en Arduino IDE

1. Abre `Lectura_RS485_SerialMonitor.ino`
2. Selecciona `ESP32 Dev Module`
3. Selecciona el puerto COM de tu ESP32
4. Presiona `Verify`
5. Presiona `Upload`

## 7. Compilar con arduino-cli

```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32 --libraries "C:\Users\enman\Documents\Arduino\libraries" "C:\Users\enman\Documents\Personal\INNOVAHUB\Arduino\BastWan_Anemometro_LoRa\Lectura_RS485_SerialMonitor"
```

## 8. Abrir monitor serial

- Velocidad: `115200 baudios`

Debes ver algo como:

```text
ESP32 - Lector RS485 anemometro
Puerto RS485 inicializado
Leyendo registros 0..3 cada 1 segundo
----------------------------------------
Registro 0 - Velocidad raw: 50
Velocidad procesada: 5.0 m/s
Registro 1 - Nivel viento: 4
Registro 2 - Direccion raw: 3340
Direccion procesada: 33.40 grados
Registro 3 - Posicion unidades: 7
```

## 9. Si no responde

- revisa alimentacion del sensor
- revisa que `A` y `B` no esten invertidos
- confirma que el sensor este en `ID 1`
- prueba invertir `A` y `B` si no hay respuesta
- confirma que el convertidor RS485 to TTL comparte `GND` con el ESP32
