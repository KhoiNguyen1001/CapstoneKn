#!/usr/bin/env python3
"""Send PC time to the STM32/DS3231 (or read it back).

pip install pyserial
python send_time.py COM5            # set time
python send_time.py /dev/ttyUSB0 -g # read time back
"""
import argparse
import datetime
import serial

SOF, CMD_SET, CMD_GET, CMD_RESP = 0xAA, 0x01, 0x02, 0x82
ACK, NAK = 0x06, 0x15


def crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([cmd, len(payload)]) + payload
    return bytes([SOF]) + body + bytes([crc8(body)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-g", "--get", action="store_true", help="read time from DS3231")
    a = ap.parse_args()

    with serial.Serial(a.port, a.baud, timeout=1) as ser:
        if a.get:
            ser.write(frame(CMD_GET))
            r = ser.read(11)
            if len(r) == 11 and r[0] == SOF and r[1] == CMD_RESP and crc8(r[1:10]) == r[10]:
                y, mo, d, wd, h, mi, s = r[3:10]
                print(f"20{y:02d}-{mo:02d}-{d:02d} (wd {wd}) {h:02d}:{mi:02d}:{s:02d}")
            else:
                print("No/invalid response:", r.hex())
            return

        now = datetime.datetime.now()
        payload = bytes([now.year - 2000, now.month, now.day,
                         now.isoweekday(),              # 1 = Monday ... 7 = Sunday
                         now.hour, now.minute, now.second])
        ser.write(frame(CMD_SET, payload))
        r = ser.read(1)
        print("ACK - time set to", now.strftime("%Y-%m-%d %H:%M:%S")
              if r == bytes([ACK]) else f"failed (response: {r.hex() or 'none'})")


if __name__ == "__main__":
    main()
