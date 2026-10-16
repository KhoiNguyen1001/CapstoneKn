"""STM32 UART logger (tkinter + pyserial).

Expected line from the STM32, newline terminated:
    <rtc_timestamp>,<bus_volt_v>,<current_a>,<cell_id>,<cell_volt_v>
    e.g. 12345.678,12.34500,1.25000,3,3.70000

Install dependency:  pip install pyserial
"""
import csv
import os
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from datetime import datetime

import serial
import serial.tools.list_ports

CSV_HEADER = ["TIMESTAMP", "BUS_VOLT_V", "CURRENT_A", "CELL_ID", "CELL_VOLT_V"]
BAUD_RATES = ["9600", "19200", "38400", "57600", "115200", "230400"]
MAX_TABLE_ROWS = 500  # keep the live view light; the CSV keeps everything


def parse_line(line):
    """Return (timestamp, bus_v, current_a, cell_id, cell_v) or None if malformed."""
    fields = line.split(",")
    if len(fields) != 5:
        return None
    timestamp = fields[0].strip()
    if not timestamp:
        return None
    try:
        bus_v = float(fields[1])
        current = float(fields[2])
        cell_id = int(fields[3])
        cell_v = float(fields[4])
    except ValueError:
        return None
    return timestamp, bus_v, current, cell_id, cell_v


class SerialReader(threading.Thread):
    """Reads bytes in the background, splits on newline, pushes lines to a queue."""

    def __init__(self, port, out_queue):
        super().__init__(daemon=True)
        self.port = port
        self.out_queue = out_queue
        self._stop_event = threading.Event()

    def stop(self):
        self._stop_event.set()

    def run(self):
        buffer = bytearray()
        while not self._stop_event.is_set():
            try:
                data = self.port.read(self.port.in_waiting or 1)  # blocks up to timeout
            except (serial.SerialException, OSError) as exc:
                self.out_queue.put(("error", str(exc)))
                return
            if not data:
                continue
            # print("RAW:", data) 
            buffer.extend(data)
            while True:
                idx = buffer.find(b"\n")
                if idx == -1:
                    break
                raw = bytes(buffer[:idx])
                del buffer[: idx + 1]
                line = raw.decode("utf-8", errors="replace").strip()  # also strips \r
                if line:
                    self.out_queue.put(("line", line))


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("STM32 UART Logger")
        self.geometry("820x560")

        self.serial_port = None
        self.reader = None
        self.rx_queue = queue.Queue()

        self.csv_file = None
        self.csv_writer = None
        self.logging = False
        self.sample_count = 0

        self._build_ui()
        self.refresh_ports()
        self.after(50, self._poll_queue)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------- UI ----------

    def _build_ui(self):
        conn = ttk.LabelFrame(self, text="Connection")
        conn.pack(fill="x", padx=10, pady=(10, 5))

        ttk.Label(conn, text="Port:").pack(side="left", padx=(8, 2), pady=8)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(conn, textvariable=self.port_var, state="readonly", width=28)
        self.port_combo.pack(side="left", padx=2)
        self.refresh_btn = ttk.Button(conn, text="Refresh", command=self.refresh_ports)
        self.refresh_btn.pack(side="left", padx=6)

        ttk.Label(conn, text="Baud:").pack(side="left", padx=(12, 2))
        self.baud_var = tk.StringVar(value="115200")
        self.baud_combo = ttk.Combobox(conn, textvariable=self.baud_var, values=BAUD_RATES,
                                       state="readonly", width=10)
        self.baud_combo.pack(side="left", padx=2)

        self.connect_btn = ttk.Button(conn, text="Connect", command=self.toggle_connection)
        self.connect_btn.pack(side="right", padx=8)

        log = ttk.LabelFrame(self, text="CSV Logging")
        log.pack(fill="x", padx=10, pady=5)

        self.csv_path_var = tk.StringVar()
        ttk.Entry(log, textvariable=self.csv_path_var, state="readonly").pack(
            side="left", fill="x", expand=True, padx=(8, 4), pady=8)
        self.browse_btn = ttk.Button(log, text="Browse...", command=self.browse_csv)
        self.browse_btn.pack(side="left", padx=4)
        self.log_btn = ttk.Button(log, text="Start Logging", command=self.toggle_logging,
                                  state="disabled")
        self.log_btn.pack(side="left", padx=4)
        self.count_var = tk.StringVar(value="Samples: 0")
        ttk.Label(log, textvariable=self.count_var, width=14).pack(side="left", padx=8)

        table_frame = ttk.Frame(self)
        table_frame.pack(fill="both", expand=True, padx=10, pady=5)
        columns = ("ts", "bus", "cur", "id", "cell")
        self.table = ttk.Treeview(table_frame, columns=columns, show="headings",
                                  selectmode="none")
        for col, text, width in (("ts", "Timestamp", 160), ("bus", "Bus Volt (V)", 120),
                                 ("cur", "Current (A)", 120), ("id", "Cell ID", 80),
                                 ("cell", "Cell Volt (V)", 120)):
            self.table.heading(col, text=text)
            self.table.column(col, width=width, anchor="center")
        scroll = ttk.Scrollbar(table_frame, orient="vertical", command=self.table.yview)
        self.table.configure(yscrollcommand=scroll.set)
        self.table.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")

        self.status_var = tk.StringVar(value="Disconnected")
        ttk.Label(self, textvariable=self.status_var, relief="sunken", anchor="w").pack(
            fill="x", side="bottom")

    def set_status(self, text):
        self.status_var.set(text)

    # ---------- Ports / connection ----------

    def refresh_ports(self):
        previous = self.port_var.get()
        ports = list(serial.tools.list_ports.comports())
        self._port_names = [p.device for p in ports]
        self.port_combo["values"] = [
            f"{p.device} ({p.description})" if p.description and p.description != "n/a" else p.device
            for p in ports
        ]
        if not ports:
            self.port_var.set("")
            self.set_status("No serial ports found")
            return
        # Keep the same port selected after a refresh if it's still there.
        for i, dev in enumerate(self._port_names):
            if previous.startswith(dev):
                self.port_combo.current(i)
                return
        self.port_combo.current(0)

    def toggle_connection(self):
        if self.serial_port is not None:
            self.disconnect()
            return

        idx = self.port_combo.current()
        if idx < 0:
            messagebox.showwarning("No port", "No serial port selected.")
            return
        device = self._port_names[idx]
        baud = int(self.baud_var.get())

        try:
            self.serial_port = serial.Serial(device, baud, bytesize=8, parity="N",
                                             stopbits=1, timeout=0.1)
        except (serial.SerialException, OSError) as exc:
            self.serial_port = None
            messagebox.showerror("Connection failed", f"Could not open {device}: {exc}")
            return

        self.reader = SerialReader(self.serial_port, self.rx_queue)
        self.reader.start()
        self._set_connected_ui(True)
        self.set_status(f"Connected to {device} @ {baud} baud")

    def disconnect(self, message="Disconnected"):
        if self.reader is not None:
            self.reader.stop()
            self.reader.join(timeout=1)
            self.reader = None
        if self.serial_port is not None:
            try:
                self.serial_port.close()
            except (serial.SerialException, OSError):
                pass
            self.serial_port = None
        self._set_connected_ui(False)
        self.set_status(message)

    def _set_connected_ui(self, connected):
        self.connect_btn.config(text="Disconnect" if connected else "Connect")
        state = "disabled" if connected else "readonly"
        self.port_combo.config(state=state)
        self.baud_combo.config(state=state)
        self.refresh_btn.config(state="disabled" if connected else "normal")
        self.log_btn.config(state="normal" if connected and self.csv_path_var.get() else "disabled")
        if not connected and self.logging:
            self.stop_logging()

    # ---------- CSV logging ----------

    def browse_csv(self):
        path = filedialog.asksaveasfilename(
            title="Choose CSV log file", defaultextension=".csv",
            initialfile=datetime.now().strftime("%Y%m%d_%H%M%S") + ".csv",
            filetypes=[("CSV files", "*.csv")])
        if not path:
            return
        self.csv_path_var.set(path)
        if self.serial_port is not None:
            self.log_btn.config(state="normal")

    def toggle_logging(self):
        if self.logging:
            self.stop_logging()
            return
        path = self.csv_path_var.get()
        if not path:
            messagebox.showwarning("No file", "Choose a CSV file first.")
            return
        # Header only for a new/empty file, so re-logging to an existing file doesn't duplicate it.
        needs_header = not os.path.exists(path) or os.path.getsize(path) == 0
        try:
            self.csv_file = open(path, "a", newline="", encoding="utf-8")
        except OSError as exc:
            messagebox.showerror("Cannot open file", f"Could not open {path}: {exc}")
            return
        self.csv_writer = csv.writer(self.csv_file, lineterminator="\n")
        if needs_header:
            self.csv_writer.writerow(CSV_HEADER)
            self.csv_file.flush()
        self.logging = True
        self.log_btn.config(text="Stop Logging")
        self.browse_btn.config(state="disabled")
        self.set_status(f"Logging to {path}")

    def stop_logging(self):
        if self.csv_file is not None:
            self.csv_file.flush()
            self.csv_file.close()
        self.csv_file = None
        self.csv_writer = None
        self.logging = False
        self.log_btn.config(text="Start Logging")
        self.browse_btn.config(state="normal")
        self.set_status("Logging stopped")

    # ---------- Incoming data ----------

    def _poll_queue(self):
        try:
            while True:
                kind, payload = self.rx_queue.get_nowait()
                if kind == "line":
                    self._handle_line(payload)
                elif kind == "error":
                    self.disconnect(f"Serial port lost: {payload}")
        except queue.Empty:
            pass
        self.after(50, self._poll_queue)

    def _handle_line(self, line):
        parsed = parse_line(line)
        if parsed is None:
            self.set_status(f'Ignored malformed line: "{line}"')
            return
        timestamp, bus_v, current, cell_id, cell_v = parsed
        row = (timestamp, f"{bus_v:.3f}", f"{current:.3f}", cell_id, f"{cell_v:.3f}")

        self.table.insert("", "end", values=row)
        children = self.table.get_children()
        if len(children) > MAX_TABLE_ROWS:
            self.table.delete(*children[: len(children) - MAX_TABLE_ROWS])
        self.table.yview_moveto(1.0)

        if self.logging and self.csv_writer is not None:
            self.csv_writer.writerow(row)
            self.csv_file.flush()  # favor durability over throughput

        self.sample_count += 1
        self.count_var.set(f"Samples: {self.sample_count}")

    def _on_close(self):
        self.disconnect()
        if self.logging:
            self.stop_logging()
        self.destroy()


if __name__ == "__main__":
    App().mainloop()
