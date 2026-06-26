function decodeUplink(input) {
  const bytes = input.bytes;

  if (bytes.length !== 8) {
    return {
      errors: [`Payload invalido: se esperaban 8 bytes y llegaron ${bytes.length}`]
    };
  }

  const speedRaw = (bytes[0] << 8) | bytes[1];
  const levelRaw = (bytes[2] << 8) | bytes[3];
  const directionRaw = (bytes[4] << 8) | bytes[5];
  const positionUnitRaw = (bytes[6] << 8) | bytes[7];

  return {
    data: {
      payload_hex: bytes.map((b) => b.toString(16).padStart(2, "0")).join(""),
      speed_raw: speedRaw,
      wind_speed: speedRaw / 10.0,
      speed_m_s: speedRaw / 10.0,
      level_raw: levelRaw,
      direction_raw: directionRaw,
      wind_direction: directionRaw / 100.0,
      direction_degrees: directionRaw / 100.0,
      position_unit_raw: positionUnitRaw
    }
  };
}
