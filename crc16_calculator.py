def calculate_crc16(data: bytes) -> int:
    """
    CRC-16/CCITT-FALSE
    - init   = 0xFFFF
    - poly   = 0x1021
    - no reflection, no final XOR
    Matches the C function exactly.
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_csv_line(timestamp: int,
                   timestamp_ms: int,
                   bus_voltage_V: int,
                   current_A: int,
                   cell_voltage_V: float,
                   cell_id: int,
                   with_crc: bool = True) -> str:
    """
    Replicates the snprintf format:
        "%lu.%03u,%.5f,%.5f,%.5f,%u\r\n"
    then optionally appends ",%04X\r\n" with the CRC.
    """
    # First part: the payload that gets CRC'd
    payload = "{}.{:03d},{:.5f},{:.5f},{:.5f},{}".format(
        timestamp,
        timestamp_ms & 0xFF,          # cast to unsigned char in C
        bus_voltage_V,
        current_A,
        cell_voltage_V,
        cell_id,
    )

    if not with_crc:
        return payload

    crc = calculate_crc16(payload.encode("ascii"))
    # ",%04X\r\n" -> uppercase hex, zero-padded to 4 digits
    return payload + ",{:04X}\r\n".format(crc)


if __name__ == "__main__":
    # Your example values
    line = build_csv_line(
        timestamp       = 4294880951,
        timestamp_ms    = 107,          # the ".026" part
        bus_voltage_V   = 0,
        current_A       = 0.00092,
        cell_voltage_V  = 0.00694,
        cell_id         = 1,
    )

    # Show exactly what bytes were CRC'd (the payload, before the CRC field)
    payload = build_csv_line(4294880951, 107, 0, 0.00092, 0.00694, 2, with_crc=False)
    print("Payload repr :", repr(payload))
    print("Payload bytes:", payload.encode("ascii").hex(" "))
    print("CRC          : 0x{:04X}".format(calculate_crc16(payload.encode("ascii"))))
    print()
    print("Full line    :", repr(line))