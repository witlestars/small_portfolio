"""
register_model.py — Register Map Mirror (C51-side register definitions)

Provides typed access to the 42-byte register space.
1:1 correspondence with C51 reg_map[] in idacs_lite.h.
"""

from dataclasses import dataclass
from typing import Optional, Tuple

REG_MAP_SIZE = 0x2A  # 42 bytes

# ── Register Addresses ─────────────────────────────────────────

class Reg:
    DEVICE_ID      = 0x00  # RO 2B
    FW_VERSION     = 0x02  # RO 2B
    SYS_STATUS     = 0x04  # RO 1B
    ERROR_CODE     = 0x05  # RO 1B
    TEMP_THRESH    = 0x06  # RW 2B  x10
    HUMI_THRESH    = 0x08  # RW 2B  x10
    LIGHT_THRESH   = 0x0A  # RW 2B  lux
    MOTOR_TARGET   = 0x0C  # RW 2B  angle x10
    MOTOR_SPEED    = 0x0E  # RW 1B  1~10
    BUZZER_CTRL    = 0x0F  # RW 1B  0=off 1=slow 2=fast
    PID_KP         = 0x10  # RW 2B  x100
    PID_KI         = 0x12  # RW 2B  x100
    PID_KD         = 0x14  # RW 2B  x100
    SAMPLE_PERIOD  = 0x16  # RW 1B  seconds
    ALERT_MASK     = 0x17  # RW 1B
    UPTIME         = 0x18  # RO 2B  seconds
    SENSOR_BASE    = 0x1A  # RO 16B  8ch x2B

# Sensor channel indices within SENSOR_BASE
SENSOR_CH = {
    'temp':     0,
    'humi':     1,
    'press':    2,
    'light':    3,
    'analog':   4,
    'distance': 5,
    'motor':    6,
    'reserved': 7,
}

# Status flags
SYS_STAT_ALARM      = 0x01
SYS_STAT_MOTOR_RUN  = 0x02
SYS_STAT_BOOT_OK    = 0x80

# Alert mask flags
ALERT_TEMP     = 0x01
ALERT_HUMI     = 0x02
ALERT_LIGHT    = 0x04
ALERT_DIST     = 0x08
ALERT_PRESSURE = 0x10


@dataclass
class SensorData:
    """Decoded sensor readings from DATA_REPORT."""
    temperature: float = 0.0    # Celsius
    humidity: float = 0.0       # %
    pressure: float = 0.0       # hPa
    light: int = 0              # lux
    analog_mv: int = 0          # mV
    distance: int = 0           # cm (0=no reading)
    motor_angle: float = 0.0    # degrees
    raw: bytes = b''
    
    @classmethod
    def from_payload(cls, payload: bytes) -> 'SensorData':
        if len(payload) < 16:
            return cls(raw=payload)
        
        def u16(i):
            return (payload[i*2] << 8) | payload[i*2+1]
        
        return cls(
            temperature = u16(0) / 10.0,
            humidity    = u16(1) / 10.0,
            pressure    = u16(2) / 100.0,
            light       = u16(3),
            analog_mv   = u16(4),
            distance    = u16(5),
            motor_angle = u16(6) / 10.0,
            raw         = payload,
        )
    
    def as_dict(self) -> dict:
        return {
            'temp': self.temperature,
            'humi': self.humidity,
            'press': self.pressure,
            'light': self.light,
            'analog_mv': self.analog_mv,
            'distance': self.distance,
            'motor_angle': self.motor_angle,
        }


class RegisterCache:
    """Local cache of C51 register space.
    Updates from read-reg responses and DATA_REPORT frames."""
    
    def __init__(self):
        self._map = bytearray(REG_MAP_SIZE)
    
    def update(self, addr: int, data: bytes):
        """Write data into cache at addr."""
        end = min(addr + len(data), REG_MAP_SIZE)
        self._map[addr:end] = data
    
    def read_u8(self, addr: int) -> int:
        return self._map[addr] if addr < REG_MAP_SIZE else 0
    
    def read_u16(self, addr: int) -> int:
        if addr + 1 >= REG_MAP_SIZE:
            return 0
        return (self._map[addr] << 8) | self._map[addr + 1]
    
    def clear(self):
        self._map = bytearray(REG_MAP_SIZE)
    
    @property
    def device_id(self) -> int:
        return self.read_u16(Reg.DEVICE_ID)
    
    @property
    def fw_version(self) -> Tuple[int, int]:
        raw = self.read_u16(Reg.FW_VERSION)
        return (raw >> 8, raw & 0xFF)
    
    @property
    def sys_status(self) -> int:
        return self.read_u8(Reg.SYS_STATUS)
    
    @property
    def is_booted(self) -> bool:
        return bool(self.read_u8(Reg.SYS_STATUS) & SYS_STAT_BOOT_OK)
    
    @property
    def is_alarmed(self) -> bool:
        return bool(self.read_u8(Reg.SYS_STATUS) & SYS_STAT_ALARM)
    
    @property
    def uptime(self) -> int:
        return self.read_u16(Reg.UPTIME)
    
    @property
    def light_target(self) -> int:
        return self.read_u16(Reg.LIGHT_THRESH)
    
    @property
    def pid_kp(self) -> float:
        return self.read_u16(Reg.PID_KP) / 100.0
    
    @property
    def pid_ki(self) -> float:
        return self.read_u16(Reg.PID_KI) / 100.0
    
    @property
    def pid_kd(self) -> float:
        return self.read_u16(Reg.PID_KD) / 100.0
