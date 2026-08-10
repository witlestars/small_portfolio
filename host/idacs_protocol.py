"""
idacs_protocol.py — IDACS Lite Protocol Python Implementation

Frame Format:
    [0xAA][0x55][Len][Type][Seq][Payload...(0~28B)][CRC16]

CRC-16/CCITT: poly=0x1021, init=0xFFFF
CRC covers: Type + Seq + Payload
"""

import struct
from enum import IntEnum
from typing import Optional, Tuple, List

# ── Constants ──────────────────────────────────────────────────

STX_H = 0xAA
STX_L = 0x55
HEADER_SIZE = 5
CRC_SIZE = 2
MIN_FRAME = 7
MAX_FRAME = 35
MAX_PAYLOAD = 28

# ── Frame Types ────────────────────────────────────────────────

class FrameType(IntEnum):
    DATA_REPORT  = 0x01  # C51 → Pi: periodic sensor data
    ACK          = 0x02  # C51 → Pi: acknowledge
    NACK         = 0x03  # C51 → Pi: negative ack + error code
    EVENT_ALERT  = 0x04  # C51 → Pi: async alert
    WRITE_REG    = 0x10  # Pi → C51: write register(s)
    READ_REG     = 0x11  # Pi → C51: read register(s)
    HEARTBEAT    = 0x14  # Pi → C51: link check

# ── Error Codes ────────────────────────────────────────────────

class ErrorCode(IntEnum):
    NONE         = 0x00
    CRC          = 0x01
    REG_ADDR     = 0x02
    REG_RANGE    = 0x03
    PARAM        = 0x04
    BUSY         = 0x05
    UNKNOWN_CMD  = 0x06
    FRAME_LEN    = 0x07

ERROR_MESSAGES = {
    0x00: "OK",
    0x01: "CRC校验失败",
    0x02: "寄存器地址越界",
    0x03: "寄存器范围越界",
    0x04: "参数越界",
    0x05: "设备忙",
    0x06: "未知命令",
    0x07: "帧长度异常",
}

# ── Alert Types ────────────────────────────────────────────────

ALERT_NAMES = {
    0x01: "温度超限",
    0x02: "湿度超限",
    0x03: "光照超限",
    0x04: "距离过近",
    0x05: "气压骤降",
    0x06: "碰撞规避",
}

# ── CRC-16/CCITT ───────────────────────────────────────────────

CRC16_TABLE = [
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
    0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF,
    0x1231,0x0210,0x3273,0x2252,0x52B5,0x4294,0x72F7,0x62D6,
    0x9339,0x8318,0xB37B,0xA35A,0xD3BD,0xC39C,0xF3FF,0xE3DE,
    0x2462,0x3443,0x0420,0x1401,0x64E6,0x74C7,0x44A4,0x5485,
    0xA56A,0xB54B,0x8528,0x9509,0xE5EE,0xF5CF,0xC5AC,0xD58D,
    0x3653,0x2672,0x1611,0x0630,0x76D7,0x66F6,0x5695,0x46B4,
    0xB75B,0xA77A,0x9719,0x8738,0xF7DF,0xE7FE,0xD79D,0xC7BC,
    0x48C4,0x58E5,0x6886,0x78A7,0x0840,0x1861,0x2802,0x3823,
    0xC9CC,0xD9ED,0xE98E,0xF9AF,0x8948,0x9969,0xA90A,0xB92B,
    0x5AF5,0x4AD4,0x7AB7,0x6A96,0x1A71,0x0A50,0x3A33,0x2A12,
    0xDBFD,0xCBDC,0xFBBF,0xEB9E,0x9B79,0x8B58,0xBB3B,0xAB1A,
    0x6CA6,0x7C87,0x4CE4,0x5CC5,0x2C22,0x3C03,0x0C60,0x1C41,
    0xEDAE,0xFD8F,0xCDEC,0xDDCD,0xAD2A,0xBD0B,0x8D68,0x9D49,
    0x7E97,0x6EB6,0x5ED5,0x4EF4,0x3E13,0x2E32,0x1E51,0x0E70,
    0xFF9F,0xEFBE,0xDFDD,0xCFFC,0xBF1B,0xAF3A,0x9F59,0x8F78,
    0x9188,0x81A9,0xB1CA,0xA1EB,0xD10C,0xC12D,0xF14E,0xE16F,
    0x1080,0x00A1,0x30C2,0x20E3,0x5004,0x4025,0x7046,0x6067,
    0x83B9,0x9398,0xA3FB,0xB3DA,0xC33D,0xD31C,0xE37F,0xF35E,
    0x02B1,0x1290,0x22F3,0x32D2,0x4235,0x5214,0x6277,0x7256,
    0xB5EA,0xA5CB,0x95A8,0x8589,0xF56E,0xE54F,0xD52C,0xC50D,
    0x34E2,0x24C3,0x14A0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xA7DB,0xB7FA,0x8799,0x97B8,0xE75F,0xF77E,0xC71D,0xD73C,
    0x26D3,0x36F2,0x0691,0x16B0,0x6657,0x7676,0x4615,0x5634,
    0xD94C,0xC96D,0xF90E,0xE92F,0x99C8,0x89E9,0xB98A,0xA9AB,
    0x5844,0x4865,0x7806,0x6827,0x18C0,0x08E1,0x3882,0x28A3,
    0xCB7D,0xDB5C,0xEB3F,0xFB1E,0x8BF9,0x9BD8,0xABBB,0xBB9A,
    0x4A75,0x5A54,0x6A37,0x7A16,0x0AF1,0x1AD0,0x2AB3,0x3A92,
    0xFD2E,0xED0F,0xDD6C,0xCD4D,0xBDAA,0xAD8B,0x9DE8,0x8DC9,
    0x7C26,0x6C07,0x5C64,0x4C45,0x3CA2,0x2C83,0x1CE0,0x0CC1,
    0xEF1F,0xFF3E,0xCF5D,0xDF7C,0xAF9B,0xBFBA,0x8FD9,0x9FF8,
    0x6E17,0x7E36,0x4E55,0x5E74,0x2E93,0x3EB2,0x0ED1,0x1EF0,
]

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc = ((crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ b) & 0xFF]) & 0xFFFF
    return crc

# ── Encode ─────────────────────────────────────────────────────

def encode(type_: int, seq: int, payload: bytes = b'') -> bytes:
    """Build a complete IDACS frame (bytes)."""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"Payload too long: {len(payload)} > {MAX_PAYLOAD}")
    
    total_len = HEADER_SIZE + len(payload) + CRC_SIZE
    # Buffer first (without total_len at position), then prepend
    body = struct.pack('>BB', type_, seq) + payload
    crc = crc16(body)
    
    frame = bytes([STX_H, STX_L, total_len]) + body + struct.pack('>H', crc)
    return frame

def encode_read_reg(seq: int, addr: int, count: int) -> bytes:
    return encode(FrameType.READ_REG, seq, bytes([addr, count]))

def encode_write_reg(seq: int, addr: int, data: bytes) -> bytes:
    return encode(FrameType.WRITE_REG, seq, bytes([addr, len(data)]) + data)

def encode_heartbeat(seq: int) -> bytes:
    return encode(FrameType.HEARTBEAT, seq, b'')

# ── Decode ─────────────────────────────────────────────────────

class ParsedFrame:
    __slots__ = ('type', 'seq', 'payload', 'crc_ok', 'error')
    
    def __init__(self, type_: int, seq: int, payload: bytes, crc_ok: bool, error: int = 0):
        self.type = type_
        self.seq = seq
        self.payload = payload
        self.crc_ok = crc_ok
        self.error = error
    
    def __repr__(self):
        tname = FrameType(self.type).name if self.type in FrameType.__members__.values() else f"0x{self.type:02X}"
        phex = ' '.join(f'{b:02X}' for b in self.payload[:16])
        return (f"Frame(type={tname}, seq={self.seq}, "
                f"payload=[{phex}], crc={'OK' if self.crc_ok else 'FAIL'})")


class FrameParser:
    """Stateful byte-at-a-time frame parser (mirrors C51 parser)."""
    
    def __init__(self):
        self._state = 'IDLE'
        self._buf = bytearray()
        self._frame_len = 0
        self._type = 0
        self._seq = 0
        self._payload = bytearray()
        self._payload_len = 0
        self._crc = 0
    
    def feed(self, byte: int) -> Optional[ParsedFrame]:
        b = byte & 0xFF
        
        if self._state == 'IDLE':
            if b == STX_H:
                self._state = 'STX1'
        
        elif self._state == 'STX1':
            if b == STX_L:
                self._state = 'LEN'
            elif b != STX_H:
                self._state = 'IDLE'
        
        elif self._state == 'LEN':
            if b < MIN_FRAME or b > MAX_FRAME:
                self._state = 'IDLE'
                return None
            self._frame_len = b
            self._state = 'TYPE'
        
        elif self._state == 'TYPE':
            self._type = b
            self._crc = self._crc_byte(0xFFFF, b)
            self._state = 'SEQ'
        
        elif self._state == 'SEQ':
            self._seq = b
            self._crc = self._crc_byte(self._crc, b)
            self._payload_len = self._frame_len - HEADER_SIZE - CRC_SIZE
            self._payload = bytearray()
            if self._payload_len == 0:
                self._state = 'CRC1'
            else:
                self._state = 'PAYLOAD'
        
        elif self._state == 'PAYLOAD':
            self._payload.append(b)
            self._crc = self._crc_byte(self._crc, b)
            if len(self._payload) >= self._payload_len:
                self._state = 'CRC1'
        
        elif self._state == 'CRC1':
            self._buf_crc_hi = b
            self._state = 'CRC2'
        
        elif self._state == 'CRC2':
            frame_crc = (self._buf_crc_hi << 8) | b
            self._state = 'IDLE'
            crc_ok = (self._crc == frame_crc)
            return ParsedFrame(self._type, self._seq, bytes(self._payload),
                              crc_ok, 0 if crc_ok else ErrorCode.CRC)
        
        return None
    
    @staticmethod
    def _crc_byte(crc: int, byte: int) -> int:
        return ((crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ byte) & 0xFF]) & 0xFFFF
    
    def reset(self):
        self._state = 'IDLE'

# ── Payload Decoders ───────────────────────────────────────────

def decode_data_report(payload: bytes) -> dict:
    """Parse 8ch x 2B sensor data from DATA_REPORT payload."""
    if len(payload) < 16:
        return {}
    channels = {}
    names = ['temp', 'humi', 'press', 'light', 'analog', 'distance', 'motor', 'reserved']
    for i, name in enumerate(names):
        val = (payload[i*2] << 8) | payload[i*2+1]
        channels[name] = val
    return channels

def decode_alert(payload: bytes) -> Tuple[int, int]:
    """Parse alert type and value from EVENT_ALERT payload."""
    if len(payload) < 3:
        return (0, 0)
    alert_type = payload[0]
    value = (payload[1] << 8) | payload[2]
    return (alert_type, value)

def decode_read_resp(payload: bytes) -> Tuple[int, bytes]:
    """Parse read-register response: (addr, data_bytes)."""
    if len(payload) < 2:
        return (0, b'')
    addr = payload[0]
    count = payload[1]
    data = payload[2:2+count] if len(payload) >= 2 + count else payload[2:]
    return (addr, data)
