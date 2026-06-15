#!/usr/bin/env python3
"""
ESP32 Audio Dump Receiver
=========================
从 ESP32 串口接收 Base64 编码的 WAV 音频数据，解码保存并播放。
用于排查麦克风录音 vs 喇叭播放的失真问题。

用法:
    python3 tools/esp_audio_recv.py [串口路径]

默认串口: /dev/cu.usbserial-A5069RR4
输出文件: esp_recorded.wav (当前目录)

使用步骤:
    1. 在 ESP32 上进入 Record 页面，对着麦克风说话
    2. 按编码器按钮停止录音
    3. 按 OK 按钮触发 dump（屏幕显示 DUMPING...）
    4. 等本脚本自动接收、解码、保存并播放
    5. 用 Mac 喇叭听：如果录音本身就有失真 → 麦克风问题
       如果录音干净但 ESP32 喇叭播出来失真 → 喇叭/功放问题
"""

import sys
import os
import base64
import time
import subprocess
import struct
import termios
import select

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-A5069RR4"
BAUD = 115200
OUTPUT = "esp_recorded.wav"
TIMEOUT_SEC = 30  # 等待 dump 数据的超时时间

# macOS baud rate constants
BAUD_MAP = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
}


def open_serial(port, baud):
    """打开串口，配置原始模式"""
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)

    attrs = termios.tcgetattr(fd)
    # 设置波特率
    baud_flag = BAUD_MAP.get(baud, termios.B115200)
    attrs[4] = baud_flag  # ispeed
    attrs[5] = baud_flag  # ospeed
    # 8N1, 无流控
    attrs[0] = 0  # iflag: no special input processing
    attrs[1] = 0  # oflag: no special output processing
    attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD  # cflag
    attrs[3] = 0  # lflag: raw mode, no echo
    # VMIN=0, VTIME=10 (1 second timeout)
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 10  # 1 second

    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def readline_from_fd(fd, timeout=1.0):
    """从 fd 读取一行，带超时"""
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            try:
                ch = os.read(fd, 1)
            except OSError:
                continue
            if ch == b"\n":
                return buf.decode("utf-8", errors="replace")
            elif ch == b"\r":
                continue
            elif ch:
                buf += ch
    return None


def main():
    print(f"[*] 打开串口: {PORT} @ {BAUD}")
    print(f"[*] 等待 ESP32 发送音频数据...")
    print(f"[*] (在 ESP32 Record 页面按 OK 按钮触发 dump)")

    try:
        fd = open_serial(PORT, BAUD)
    except OSError as e:
        print(f"[!] 无法打开串口: {e}")
        print(f"[!] 请检查:")
        print(f"    - ESP32 是否已连接")
        print(f"    - 串口路径是否正确 (ls /dev/cu.usb*)")
        print(f"    - 是否有其他程序占用串口 (idf.py monitor?)")
        sys.exit(1)

    collecting = False
    b64_lines = []
    start_time = time.time()

    try:
        while True:
            if time.time() - start_time > TIMEOUT_SEC:
                print(f"[!] 超时 ({TIMEOUT_SEC}s)，未收到 dump 数据")
                break

            line = readline_from_fd(fd, timeout=1.0)
            if line is None:
                continue

            line = line.rstrip("\r\n")

            # ESP_LOGI 输出格式: "I (xxxx) tag: message"
            # 提取冒号后面的实际内容
            content = line
            if ") " in line and ": " in line:
                content = line.split(": ", 1)[-1]

            if "=== AUDIO DUMP BEGIN ===" in content:
                collecting = True
                b64_lines = []
                print("[*] 开始接收音频数据...")
                continue

            if "=== AUDIO DUMP END ===" in content:
                if collecting:
                    print(f"[*] 接收完成，共 {len(b64_lines)} 行 Base64 数据")
                    break
                continue

            if collecting:
                stripped = content.strip()
                if not stripped:
                    continue
                # 跳过元数据行 (SAMPLES=xxx RATE=xxx ...)
                if stripped.startswith("SAMPLES="):
                    print(f"[*] 元数据: {stripped}")
                    continue
                # Base64 行应该只包含 A-Za-z0-9+/=
                valid_chars = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")
                if all(c in valid_chars for c in stripped):
                    b64_lines.append(stripped)

    except KeyboardInterrupt:
        print("\n[!] 用户中断")
    finally:
        os.close(fd)

    if not b64_lines:
        print("[!] 未收到有效音频数据")
        sys.exit(1)

    # 合并并解码 Base64
    b64_data = "".join(b64_lines)
    try:
        wav_bytes = base64.b64decode(b64_data)
    except Exception as e:
        print(f"[!] Base64 解码失败: {e}")
        with open("esp_dump_raw.txt", "w") as f:
            f.write("\n".join(b64_lines))
        print("[*] 原始数据已保存到 esp_dump_raw.txt")
        sys.exit(1)

    # 验证 WAV 头
    if wav_bytes[:4] != b"RIFF":
        print(f"[!] 警告: 数据不以 RIFF 开头 (前4字节: {wav_bytes[:4].hex()})")
        print(f"[*] 仍然保存，可能是原始 PCM")

    # 保存文件
    with open(OUTPUT, "wb") as f:
        f.write(wav_bytes)

    file_size = len(wav_bytes)
    print(f"[*] 已保存: {OUTPUT} ({file_size:,} bytes)")

    # 显示 WAV 信息
    if wav_bytes[:4] == b"RIFF" and len(wav_bytes) >= 44:
        sr = struct.unpack_from("<I", wav_bytes, 24)[0]
        ch = struct.unpack_from("<H", wav_bytes, 22)[0]
        bits = struct.unpack_from("<H", wav_bytes, 34)[0]
        data_bytes = struct.unpack_from("<I", wav_bytes, 40)[0]
        duration = data_bytes / (sr * ch * (bits // 8))
        print(f"[*] WAV 信息: {sr}Hz, {ch}ch, {bits}bit, {duration:.2f}s")

    # 播放
    print(f"[*] 正在播放...")
    try:
        subprocess.run(["afplay", OUTPUT], check=True)
        print("[*] 播放完成")
        print()
        print("=== 判断结果 ===")
        print("如果听到的声音有失真/杂音 → 麦克风录音端有问题")
        print("如果听到的声音清晰干净   → 录音没问题，问题在 ESP32 喇叭/功放端")
    except FileNotFoundError:
        print("[!] afplay 不可用，请手动播放:")
        print(f"    open {OUTPUT}")
    except subprocess.CalledProcessError as e:
        print(f"[!] 播放失败: {e}")
        print(f"[*] 请手动播放: open {OUTPUT}")


if __name__ == "__main__":
    main()
