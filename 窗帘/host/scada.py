#!/usr/bin/env python3
"""
scada.py — SHADE 监控终端 (Pi-side SCADA Dashboard)

Usage:
    python3 scada.py                    # Auto-detect port
    python3 scada.py /dev/ttyUSB0       # Specify port
    python3 scada.py COM3               # Windows

Commands (type in terminal):
    reg <addr> [count]         Read register(s)
    set <addr> <val> [val...]  Write register byte(s)
    set16 <addr> <val>         Write 16-bit register value
    thresh <lux>               Set light threshold
    pid <kp> <ki> <kd>         Set PID gains (x100)
    speed <1-10>               Set motor speed
    buzzer <0|1|2>             Set buzzer mode
    quit                       Exit
"""

import sys
import os
import time
import argparse
from typing import Optional

# Make script importable from parent dir
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from serial_hal import SerialHAL
from idacs_protocol import (
    FrameType, ErrorCode, ERROR_MESSAGES, ALERT_NAMES,
    decode_data_report, decode_alert, decode_read_resp,
    encode_read_reg, encode_write_reg, encode_heartbeat,
    ParsedFrame, FrameParser
)
from register_model import (
    Reg, SensorData, RegisterCache, SYS_STAT_ALARM,
    ALERT_TEMP, ALERT_HUMI, ALERT_LIGHT, ALERT_DIST
)

# Try to import Rich for pretty rendering, fallback to plain text
try:
    from rich.live import Live
    from rich.table import Table
    from rich.panel import Panel
    from rich.layout import Layout
    from rich.console import Console
    from rich.text import Text
    from rich import box
    HAS_RICH = True
except ImportError:
    HAS_RICH = False
    print("[WARN] 未安装 rich 库。pip install rich 以获得更好显示效果。")
    print("       当前使用纯文本模式。\n")


# ═══════════════════════════════════════════════════════════════
# PLAIN-TEXT FALLBACK
# ═══════════════════════════════════════════════════════════════

class PlainUI:
    def __init__(self):
        self._last_lines = 0
    
    def render(self, data: SensorData, cache: RegisterCache, frames: list, connected: bool):
        # Clear previous output
        print("\033[H\033[J", end='')
        
        status = "[● 已连接]" if connected else "[○ 未连接]"
        print(f"╔════════════════════ SHADE 监控终端 v1.0 ═══════════════════╗")
        print(f"║  {status:38s} 波特率:9600 ║")
        print(f"╠═══════════════╤═════════════════════════════════════════════╣")
        
        # Sensor data
        print(f"║ 温度  {data.temperature:6.1f} C  │  湿度  {data.humidity:5.1f} %                ║")
        print(f"║ 气压  {data.pressure:6.1f} hPa │  光照  {data.light:5d} lux              ║")
        print(f"║ 模拟  {data.analog_mv:5d} mV   │  距离  {data.distance:5d} cm              ║")
        print(f"║ 电机  {data.motor_angle:5.1f} deg │  {'⚠ 报警中' if cache.is_alarmed else '  正常'}                     ║")
        print(f"╠═══════════════════╧═════════════════════════════════════════╣")
        
        # PID info
        target = cache.light_target
        print(f"║ PID目标: {target} lux   KP:{cache.pid_kp:.2f} KI:{cache.pid_ki:.2f} KD:{cache.pid_kd:.2f}     ║")
        
        # Protocol frames
        print(f"╠═════════════════════════════════════════════════════════════╣")
        print(f"║ 协议帧流 (最近5条):                                        ║")
        for f in frames[-5:]:
            line = _format_frame_line(f)
            print(f"║ {line:<57s} ║")
        
        print(f"╚═════════════════════════════════════════════════════════════╝")
        print(f"\n命令> ", end='', flush=True)


def _format_frame_line(f: ParsedFrame) -> str:
    tname = FrameType(f.type).name if f.type in FrameType.__members__.values() else f"0x{f.type:02X}"
    phex = ' '.join(f'{b:02X}' for b in f.payload[:8])
    return f"{tname:12s} seq={f.seq:3d} [{phex}...]"


# ═══════════════════════════════════════════════════════════════
# Rich UI
# ═══════════════════════════════════════════════════════════════

if HAS_RICH:
    class RichUI:
        def __init__(self):
            self.console = Console()
        
        def make_layout(self, data: SensorData, cache: RegisterCache,
                        frames: list, connected: bool) -> Layout:
            layout = Layout()
            layout.split_column(
                Layout(name="header", size=3),
                Layout(name="main"),
                Layout(name="footer", size=5),
            )
            layout["main"].split_row(
                Layout(name="sensors", ratio=2),
                Layout(name="pid", ratio=2),
            )
            
            # Header
            status = "[bold green]● 已连接[/]" if connected else "[bold red]○ 未连接[/]"
            header = Panel(f"{status}  波特率: 9600 bps", title="SHADE 监控终端 v1.0",
                          border_style="blue")
            layout["header"].update(header)
            
            # Sensor table
            sensor_table = Table(box=box.SIMPLE, show_header=True)
            sensor_table.add_column("通道", style="cyan")
            sensor_table.add_column("数值", style="white")
            sensor_table.add_column("单位")
            
            if data.temperature > -273:
                sensor_table.add_row("🌡 温度", f"{data.temperature:.1f}", "°C")
                sensor_table.add_row("💧 湿度", f"{data.humidity:.1f}", "%")
                sensor_table.add_row("🌍 气压", f"{data.pressure:.1f}", "hPa")
            sensor_table.add_row("☀ 光照", f"{data.light}", "lux")
            sensor_table.add_row("🔌 模拟", f"{data.analog_mv}", "mV")
            sensor_table.add_row("📏 距离", f"{data.distance}", "cm")
            sensor_table.add_row("⚙ 电机", f"{data.motor_angle:.1f}", "°")
            
            alarm = "[bold red]⚠ 报警中[/]" if cache.is_alarmed else "[green]✓ 正常[/]"
            sensor_table.add_row("🚨 状态", alarm, "")
            
            sensors_panel = Panel(sensor_table, title="📊 实时数据", border_style="green")
            layout["sensors"].update(sensors_panel)
            
            # PID panel
            pid_table = Table(box=box.SIMPLE)
            pid_table.add_column("参数", style="cyan")
            pid_table.add_column("值", style="white")
            pid_table.add_row("目标照度", f"{cache.light_target} lux")
            pid_table.add_row("Kp", f"{cache.pid_kp:.2f}")
            pid_table.add_row("Ki", f"{cache.pid_ki:.2f}")
            pid_table.add_row("Kd", f"{cache.pid_kd:.2f}")
            pid_table.add_row("电机速度", f"{cache.read_u8(Reg.MOTOR_SPEED)}")
            pid_table.add_row("运行时间", f"{cache.uptime}s")
            
            pid_panel = Panel(pid_table, title="🎯 PID 参数", border_style="yellow")
            layout["pid"].update(pid_panel)
            
            # Footer: protocol frames
            frame_lines = []
            for f in frames[-6:]:
                tname = FrameType(f.type).name if f.type in FrameType.__members__.values() else f"0x{f.type:02X}"
                phex = ' '.join(f'{b:02X}' for b in f.payload[:8])
                marker = "[green]✓[/]" if f.crc_ok else "[red]✗[/]"
                frame_lines.append(f"{marker} {tname:12s} seq={f.seq:<3d} [{phex}]")
            
            if not frame_lines:
                frame_lines = ["[dim](等待数据...)[/]"]
            
            frame_text = "\n".join(frame_lines)
            footer = Panel(frame_text, title="📡 协议帧流", border_style="magenta")
            layout["footer"].update(footer)
            
            return layout


# ═══════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description='SHADE SCADA Terminal')
    parser.add_argument('port', nargs='?', help='串口设备路径')
    args = parser.parse_args()
    
    # Detect port
    port = args.port
    if not port:
        port = SerialHAL.find_c51_port()
        if not port:
            print("[ERR] 未找到C51设备。请指定串口路径。")
            print("      可用端口:", SerialHAL.list_ports())
            return 1
        print(f"[INFO] 自动检测到: {port}")
    
    hal = SerialHAL(port)
    
    if not hal.open():
        return 1
    
    print(f"[INFO] 串口已打开: {port}")
    print("[INIT] 发送 HEARTBEAT...")
    
    cache = RegisterCache()
    latest_sensor = SensorData()
    frame_history = []
    
    # Connect
    if not hal.heartbeat():
        print("[WARN] HEARTBEAT 无响应，继续尝试...")
    else:
        print("[OK] 设备响应正常")
        
        # Read device ID
        dev_data = hal.read_reg(Reg.DEVICE_ID, 2)
        if dev_data:
            dev_id = (dev_data[0] << 8) | dev_data[1]
            cache.update(Reg.DEVICE_ID, dev_data)
            print(f"[OK] 设备ID: 0x{dev_id:04X}")
        
        # Read firmware version
        fw_data = hal.read_reg(Reg.FW_VERSION, 2)
        if fw_data:
            cache.update(Reg.FW_VERSION, fw_data)
            print(f"[OK] 固件版本: v{fw_data[0]}.{fw_data[1]}")
        
        # Read register snapshot
        for (reg, size, _) in [
            (Reg.LIGHT_THRESH, 2, "光照阈值"),
            (Reg.PID_KP, 6, "PID参数"),
            (Reg.MOTOR_SPEED, 1, "电机速度"),
            (Reg.ALERT_MASK, 1, "报警掩码"),
        ]:
            data = hal.read_reg(reg, size)
            if data:
                cache.update(reg, data)
    
    print("\n[OK] 系统就绪。输入命令或按 Ctrl+C 退出。")
    print("=" * 60)
    
    # UI
    if HAS_RICH:
        ui = RichUI()
        console = Console()
    
    try:
        import threading
        
        running = [True]
        frames_lock = threading.Lock()
        data_lock = threading.Lock()
        latest_alert = [None]
        
        def reader_thread():
            """Background: poll frames, update cache."""
            last_report = time.time()
            while running[0]:
                frames = hal.poll()
                if frames:
                    with frames_lock:
                        for f in frames:
                            frame_history.append(f)
                            if len(frame_history) > 50:
                                frame_history.pop(0)
                            
                            if f.crc_ok:
                                if f.type == FrameType.DATA_REPORT:
                                    with data_lock:
                                        s = SensorData.from_payload(f.payload)
                                        latest_sensor = s
                                elif f.type == FrameType.EVENT_ALERT:
                                    atype, val = decode_alert(f.payload)
                                    latest_alert[0] = (atype, val)
                                    name = ALERT_NAMES.get(atype, f"0x{atype:02X}")
                                    print(f"\n[ALERT] {name}: {val}")
                                    print("命令> ", end='', flush=True)
                
                # Auto-refresh register cache every 3s
                if time.time() - last_report > 3.0:
                    data = hal.read_reg(Reg.TEMP_THRESH, 4)  # Quick snapshot
                    if data:
                        cache.update(Reg.TEMP_THRESH, data)
                    data = hal.read_reg(Reg.LIGHT_THRESH, 2)
                    if data:
                        cache.update(Reg.LIGHT_THRESH, data)
                    data = hal.read_reg(Reg.MOTOR_SPEED, 3)
                    if data:
                        cache.update(Reg.MOTOR_SPEED, data)
                    last_report = time.time()
                
                time.sleep(0.02)
        
        reader = threading.Thread(target=reader_thread, daemon=True)
        reader.start()
        
        # Main command loop
        while running[0]:
            if HAS_RICH:
                with data_lock, frames_lock:
                    layout = ui.make_layout(
                        latest_sensor, cache,
                        list(frame_history), hal.is_open()
                    )
                console.print(layout)
            
            try:
                cmd = input("命令> ").strip()
            except (EOFError, KeyboardInterrupt):
                break
            
            if not cmd:
                continue
            
            parts = cmd.split()
            op = parts[0].lower()
            
            try:
                if op in ('q', 'quit', 'exit'):
                    break
                
                elif op == 'reg':
                    addr = int(parts[1], 0)
                    cnt = int(parts[2]) if len(parts) > 2 else 1
                    data = hal.read_reg(addr, cnt)
                    if data:
                        cache.update(addr, data)
                        hex_str = ' '.join(f'{b:02X}' for b in data)
                        print(f"  [{addr:#04X}]: {hex_str}")
                        # Decode known registers
                        if addr == Reg.LIGHT_THRESH and cnt >= 2:
                            val = (data[0] << 8) | data[1]
                            print(f"  → 光照阈值: {val} lux")
                        elif addr == Reg.TEMP_THRESH and cnt >= 2:
                            val = (data[0] << 8) | data[1]
                            print(f"  → 温度阈值: {val/10:.1f} °C")
                        elif addr >= Reg.PID_KP and addr <= Reg.PID_KD and cnt >= 2:
                            val = (data[0] << 8) | data[1]
                            names = {Reg.PID_KP: 'Kp', Reg.PID_KI: 'Ki', Reg.PID_KD: 'Kd'}
                            name = names.get(addr, 'Gain')
                            print(f"  → {name}: {val/100:.2f}")
                    else:
                        print("  [ERR] 无响应")
                
                elif op == 'set':
                    addr = int(parts[1], 0)
                    vals = bytes(int(v, 0) & 0xFF for v in parts[2:])
                    if hal.write_reg(addr, vals):
                        cache.update(addr, vals)
                        print(f"  [OK] 写入 {addr:#04X} ← {' '.join(f'{b:02X}' for b in vals)}")
                    else:
                        print("  [ERR] 写入失败 (无ACK)")
                
                elif op == 'set16':
                    addr = int(parts[1], 0)
                    val = int(parts[2], 0)
                    data = bytes([(val >> 8) & 0xFF, val & 0xFF])
                    if hal.write_reg(addr, data):
                        cache.update(addr, data)
                        print(f"  [OK] 写入 {addr:#04X} ← {val} (0x{val:04X})")
                    else:
                        print("  [ERR] 写入失败")
                
                elif op == 'thresh':
                    lux = int(parts[1])
                    data = bytes([(lux >> 8) & 0xFF, lux & 0xFF])
                    if hal.write_reg(Reg.LIGHT_THRESH, data):
                        cache.update(Reg.LIGHT_THRESH, data)
                        print(f"  [OK] 光照阈值设为 {lux} lux")
                    else:
                        print("  [ERR] 设置失败")
                
                elif op == 'pid':
                    kp = int(float(parts[1]) * 100)
                    ki = int(float(parts[2]) * 100)
                    kd = int(float(parts[3]) * 100)
                    data = bytes([
                        (kp >> 8) & 0xFF, kp & 0xFF,
                        (ki >> 8) & 0xFF, ki & 0xFF,
                        (kd >> 8) & 0xFF, kd & 0xFF,
                    ])
                    if hal.write_reg(Reg.PID_KP, data):
                        cache.update(Reg.PID_KP, data)
                        print(f"  [OK] PID设为 Kp={kp/100:.2f} Ki={ki/100:.2f} Kd={kd/100:.2f}")
                    else:
                        print("  [ERR] 设置失败")
                
                elif op == 'speed':
                    s = int(parts[1])
                    s = max(1, min(10, s))
                    if hal.write_reg(Reg.MOTOR_SPEED, bytes([s])):
                        cache.update(Reg.MOTOR_SPEED, bytes([s]))
                        print(f"  [OK] 电机速度设为 {s}")
                    else:
                        print("  [ERR] 设置失败")
                
                elif op == 'buzzer':
                    mode = int(parts[1])
                    if hal.write_reg(Reg.BUZZER_CTRL, bytes([mode])):
                        cache.update(Reg.BUZZER_CTRL, bytes([mode]))
                        print(f"  [OK] 蜂鸣器: {'关闭' if mode==0 else '慢速' if mode==1 else '快速'}")
                    else:
                        print("  [ERR] 设置失败")
                
                elif op == 'status':
                    status = cache.sys_status
                    print(f"  系统状态: 0x{status:02X}")
                    print(f"    启动完成: {'是' if status & 0x80 else '否'}")
                    print(f"    报警状态: {'是' if status & SYS_STAT_ALARM else '否'}")
                    print(f"    电机运行: {'是' if status & 0x02 else '否'}")
                    print(f"  上次错误: {ERROR_MESSAGES.get(cache.read_u8(Reg.ERROR_CODE), 'N/A')}")
                
                elif op == 'data':
                    with data_lock:
                        s = latest_sensor
                    print(f"  温度: {s.temperature:.1f}°C  湿度: {s.humidity:.1f}%")
                    print(f"  气压: {s.pressure:.1f}hPa  光照: {s.light}lux")
                    print(f"  模拟: {s.analog_mv}mV  距离: {s.distance}cm")
                    print(f"  电机角度: {s.motor_angle:.1f}°")
                
                else:
                    print(f"  未知命令: {op}")
                    print("  可用: reg set set16 thresh pid speed buzzer status data quit")
            
            except (IndexError, ValueError) as e:
                print(f"  [ERR] 参数错误: {e}")
    
    finally:
        running[0] = False
        hal.close()
        print("\n再见。")

if __name__ == '__main__':
    main()
