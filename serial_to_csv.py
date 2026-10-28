"""Receive STM32 UART data and log it to a CSV file (no GUI).

Expected line from the MCU, newline terminated:
    <rtc_timestamp>,<bus_volt_v>,<current_a>,<cell_id>,<cell_volt_v>
    e.g. 12345.678,12.34500,1.25000,3,3.7000123

Each received line (ending in \n) is written to the CSV exactly as sent:
no field splitting, no checking, no rounding.

Usage:
    pip install pyserial
    python serial_to_csv.py                       # pick the port from a list
    python serial_to_csv.py -p COM5 -b 115200 -o log.csv
    python serial_to_csv.py --list                # just list available ports

Press Ctrl+C to stop.
"""
import argparse
import os
import sys
from datetime import datetime

import serial
import serial.tools.list_ports

CSV_HEADER = ["TIMESTAMP", "BUS_VOLT_V", "CURRENT_A", "CELL_ID", "CELL_VOLT_V"]


def choose_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        sys.exit("No serial ports found.")
    print("Available ports:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device}  {p.description}")
    while True:
        choice = input("Select port number: ").strip()
        if choice.isdigit() and int(choice) < len(ports):
            return ports[int(choice)].device
        print("Invalid choice.")


def main():
    ap = argparse.ArgumentParser(description="Log STM32 UART data to CSV")
    ap.add_argument("-p", "--port", help="COM port, e.g. COM5 or /dev/ttyUSB0")
    ap.add_argument("-b", "--baud", type=int, default=115200, help="baud rate (default 115200)")
    ap.add_argument("-o", "--out", help="output CSV file (default: <timestamp>.csv)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    if args.list:
        for p in serial.tools.list_ports.comports():
            print(f"{p.device}  {p.description}")
        return

    port = args.port or choose_port()
    out_path = args.out or datetime.now().strftime("%Y%m%d_%H%M%S") + ".csv"

    try:
        ser = serial.Serial(port, args.baud, bytesize=8, parity="N", stopbits=1, timeout=1)
    except (serial.SerialException, OSError) as exc:
        sys.exit(f"Could not open {port}: {exc}")

    # Header only for a new/empty file, so appending to an existing log doesn't duplicate it.
    needs_header = not os.path.exists(out_path) or os.path.getsize(out_path) == 0
    count = 0
    print(f"Connected to {port} @ {args.baud} baud, logging to {out_path}. Ctrl+C to stop.")

    with open(out_path, "a", newline="", encoding="utf-8") as f:
        if needs_header:
            f.write(",".join(CSV_HEADER) + "\n")
            f.flush()
        try:
            while True:
                raw = ser.readline()          # waits for '\n' or the 1 s timeout
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()   # also strips '\r'
                if not line:
                    continue
                f.write(line + "\n")
                f.flush()                      # don't lose data if the program is killed
                count += 1
                print(f"{count:6d}  {line}")
        except KeyboardInterrupt:
            print(f"\nStopped. {count} rows saved to {out_path}")
        except (serial.SerialException, OSError) as exc:
            print(f"\nSerial port lost: {exc}. {count} rows saved to {out_path}")
        finally:
            ser.close()


if __name__ == "__main__":
    main()
