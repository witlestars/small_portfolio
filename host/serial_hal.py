"""
serial_hal.py — Serial Port HAL for Raspberry Pi ↔ C51

Handles: port open/close, frame send, frame receive with timeout.
Uses pyserial.
"""

import serial
import serial.tools.list_ports
import time
import threading
from typing import Optional, List, Callable

from idacs_protocol import (
    FrameParser, ParsedFrame, FrameType, encode_heartbeat,
    STX_H, STX_L, MAX_FRAME
)

DEFAULT_BAUD = 9600
DEFAULT_PORT = '/dev/ttyUSB0'
ACK_TIMEOUT = 0.3      # seconds
RETRY_COUNT = 2


class SerialHAL:
    """Serial communication with C51 over UART."""
    
    def __init__(self, port: str = DEFAULT_PORT, baud: int = DEFAULT_BAUD):
        self._port_name = port
        self._baud = baud
        self._ser: Optional[serial.Serial] = None
        self._parser = FrameParser()
        self._rx_buffer = bytearray()
        self._on_frame: Optional[Callable] = None
        self._running = False
    
    @staticmethod
    def list_ports() -> List[str]:
        ports = serial.tools.list_ports.comports()
        return [p.device for p in ports]
    
    @staticmethod
    def find_c51_port() -> Optional[str]:
        """Auto-find likely C51 port (CH340, CP2102, etc.)."""
        patterns = ['CH340', 'CP210', 'CH341', 'PL2303', 'FT232', 'USB Serial']
        ports = serial.tools.list_ports.comports()
        for p in ports:
            desc = p.description + ' ' + (p.manufacturer or '')
            for pat in patterns:
                if pat.lower() in desc.lower():
                    return p.device
        return None
    
    def open(self) -> bool:
        try:
            self._ser = serial.Serial(
                port=self._port_name,
                baudrate=self._baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.01,      # Non-blocking read
                write_timeout=1.0
            )
            self._ser.reset_input_buffer()
            self._ser.reset_output_buffer()
            self._running = True
            return True
        except (serial.SerialException, OSError) as e:
            print(f"[ERR] 无法打开串口 {self._port_name}: {e}")
            return False
    
    def close(self):
        self._running = False
        if self._ser and self._ser.is_open:
            self._ser.close()
    
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open
    
    @property
    def port(self) -> str:
        return self._port_name
    
    # ── Send ────────────────────────────────────────────────
    
    def send_raw(self, data: bytes):
        """Send raw bytes to C51."""
        if not self._ser or not self._ser.is_open:
            raise RuntimeError("串口未打开")
        self._ser.write(data)
        self._ser.flush()
    
    def send_frame(self, frame: bytes):
        """Send a pre-built frame."""
        self.send_raw(frame)
    
    # ── Receive ──────────────────────────────────────────────
    
    def poll(self) -> List[ParsedFrame]:
        """Poll for incoming frames (non-blocking)."""
        frames = []
        if not self._ser or not self._ser.is_open:
            return frames
        
        try:
            while self._ser.in_waiting > 0:
                byte = self._ser.read(1)
                if byte:
                    frame = self._parser.feed(byte[0])
                    if frame:
                        frames.append(frame)
        except serial.SerialException:
            pass
        
        return frames
    
    def read_frame(self, timeout: float = 2.0) -> Optional[ParsedFrame]:
        """Read one frame with timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            frames = self.poll()
            if frames:
                return frames[0]
            time.sleep(0.01)
        return None
    
    # ── High-Level Commands ──────────────────────────────────
    
    def heartbeat(self) -> bool:
        """Send heartbeat, wait for ACK."""
        frame = encode_heartbeat(0)
        self.send_frame(frame)
        response = self.read_frame(ACK_TIMEOUT * 2)
        return (response is not None and
                response.type == FrameType.ACK and
                response.crc_ok)
    
    def read_reg(self, addr: int, count: int, seq: int = 0) -> Optional[bytes]:
        """Read register(s). Returns data bytes or None on failure."""
        from idacs_protocol import encode_read_reg, decode_read_resp
        
        frame = encode_read_reg(seq, addr, count)
        self.send_frame(frame)
        
        for _ in range(RETRY_COUNT + 1):
            resp = self.read_frame(ACK_TIMEOUT)
            if resp and resp.type == FrameType.ACK and resp.crc_ok:
                _, data = decode_read_resp(resp.payload)
                return data
            time.sleep(0.05)
        return None
    
    def write_reg(self, addr: int, data: bytes, seq: int = 0) -> bool:
        """Write register(s). Returns True on ACK."""
        from idacs_protocol import encode_write_reg
        
        frame = encode_write_reg(seq, addr, data)
        self.send_frame(frame)
        
        for _ in range(RETRY_COUNT + 1):
            resp = self.read_frame(ACK_TIMEOUT)
            if resp and resp.type == FrameType.ACK and resp.crc_ok:
                return True
            time.sleep(0.05)
        return False
