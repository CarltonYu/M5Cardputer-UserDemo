# ESP32S3N16R8 项目现状与音频处理记录

> 文档路径：`docs/PROJECT_STATUS.md`  
> 适用范围：`M5Cardputer-UserDemo/ESP32S3N16R8` 移植版本  
> 最后更新：2026-06-14

---

## 1. 硬件与目标平台

| 项目 | 配置 |
|---|---|
| 开发板 | ESP32-S3-N16R8（16 MB Flash，8 MB PSRAM） |
| 框架 | ESP-IDF v5.4.2 |
| LCD | 2.0 寸 320×240 ST7789 SPI |
| 输入 | EC11 旋转编码器 + OK 按钮 |
| 音频 | ES8311 Codec + NS4150B 功放 |
| IMU | MPU6050（I2C，地址 0x68） |
| 存储 | TF 卡（SDSPI，与 LCD 共享 SPI2） |
| 可选 I2C 设备 | TCA8418 键盘控制器 |

详细引脚规划见 `ESP32S3N16R8/PINOUT.md`。音频相关关键引脚：

| 信号 | GPIO |
|---|---|
| I2C SDA | 17 |
| I2C SCL | 18 |
| I2S MCLK | 38 |
| I2S BCLK | 35 |
| I2S WS / LRCK | 36 |
| I2S DIN（录音） | 37 |
| I2S DOUT（播放） | 47 |

---

## 2. 已实现功能

- [x] 基于 ESP-IDF 5.4.2 的最小系统启动
- [x] ST7789 LCD 驱动与 Launcher 图标菜单
- [x] EC11 旋转编码器 + OK 按钮输入
- [x] ESP-NOW 点对点聊天（`Chat` 应用）
- [x] Wi-Fi 扫描（`Scan` 应用）
- [x] SD 卡探测页面（当前识别失败，见第 5 节）
- [x] 自定义 ES8311 音频 Codec 驱动
- [x] I2S 音频流封装：录音/播放半双工切换
- [x] 录音应用（`Record`）：循环缓冲区录音 + 回放
- [x] 串口音频 dump：OK 按钮触发 Base64 WAV 输出，Mac 端接收播放排查音质
- [x] Launcher 旋转提示音 `playTone()`
- [x] IMU 页面（`IMU`）：MPU6050 加速度/陀螺仪/温度实时显示 + 倾斜球指示器

---

## 3. 音频子系统架构

### 3.1 目录结构

```
ESP32S3N16R8/main/hal/audio/
├── es8311.h / es8311.cpp      # ES8311 I2C 控制、上电/模式切换序列
├── i2s_audio.h / i2s_audio.cpp # ESP-IDF I2S STD 通道管理、单声道/立体声转换
```

### 3.2 关键配置

```text
Sample rate:    16 kHz
Slot format:    Philips I2S, stereo, 32-bit slot, 16-bit data
MCLK multiple:  256 × LRCK = 4.096 MHz
I2S port:       CONFIG_AUDIO_I2S_PORT = 0
ES8311 I2C addr:0x18 (7-bit)
```

### 3.3 ES8311 驱动要点

- 寄存器序列从 `espressif/esp_codec_dev` 的 `device/es8311/es8311.c` 移植。
- `enableSpeaker()` / `enableMicrophone()` / `disable()` 分别对应 DAC、ADC、断电三种模式。
- **冷启动保护**：首次上电时等待 500 ms（麦克风首次 200 ms），并最多重试 5 次，避免 ES8311 上电未完成时 I2C NACK。
- **成功标志位**：`speaker_ok_` / `mic_ok_` 首次成功后使用更短延时。
- **空闲静音**：初始化完成后写入 `0x32 = 0x00` 把 DAC 静音，避免无 I2S 数据时功放产生底噪。
- **录音模式**：关闭 DAC 电源，对齐 M5Unified `_microphone_enabled_cb_cardputer_adv` 配置：
  - 寄存器 0x01 = 0xBA（MCLK from BCLK）、0x02 = 0x18（MULT_PRE=3）
  - 不写 0x16（mic boost = 0 dB，ES8311 默认值，避免 ADC 削波）
  - PGA 增益 0x14 = 0x10（最低）、ADC 音量 0x17 = 0xBF（±0 dB）
  - 不启用 ALC

### 3.4 I2S 音频流设计

- **通道持久化**：`I2sAudio::init()` 一次性创建 TX、RX 两个通道，整个运行期间不删除。
- **方向切换**：
  - 录音时同时使能 TX、RX（MCLK/BCLK 由 TX 侧提供，保证 RX 有连续时钟）。
  - 播放时关闭 RX，仅保留 TX。
- **`stopPlayback()` 不关闭 TX 通道**：利用 `auto_clear=true` 让驱动自动发零，避免反复 enable/disable 带来的 pop/click。
- **单声道 API**：
  - 录音：从 32-bit 立体声槽位取左声道。
  - 播放：把单声道样本复制到左右两个槽位。

### 3.5 Launcher 提示音 `Hal::playTone()`

- 生成正弦波样本后调用 `i2s_audio_.startPlayback()`。
- 若 DAC 不在 speaker 模式，会自动 `enableSpeaker()`。
- 写入 `0x32 = 0xBF` 恢复 0 dB 音量（防止录音回放把音量改到 `0xD7` 后提示音过响）。
- 播放结束后调用 `stopPlayback()`，TX 通道继续保持运行。

### 3.6 Record 应用流程

1. 进入 `Record` 页面：
   - `enableMicrophone()` → `startRecording()`（TX/RX 同时使能）。
   - 16 kHz × 1 s = 16,000 样本的循环缓冲区，每块 200 样本，共 80 块。
2. 按下编码器：
   - `stopRecording()` → `enableSpeaker()` → `startPlayback()`。
   - 写入 `0x32 = 0xD7`（约 +12 dB），补偿录音增益偏低。
3. 播放完或再次按下编码器：
   - 切回录音模式，恢复 `0x32 = 0xBF`。
4. 退出页面：
   - 停止播放/录音，恢复 DAC 音量到 `0xBF`。

---

## 4. 关键问题与修复过程

| 现象 | 根因 | 处理 |
|---|---|---|
| 录音时 RX 读不到数据 | 全双工模式下 MCLK 绑定到 TX，录音时 TX 未运行导致 MCLK 停振 | 改为半双工：录音时同时启用 TX 提供 MCLK，RX 取左声道数据 |
| 冷启动 ES8311 I2C NACK | Codec 振荡器和 5 V 功放上电未完成 | `enableSpeaker()` 首次 500 ms 延时 + 5 次重试，成功后走快速路径 |
| 空闲时喇叭有底噪/嗡嗡声 | DAC 上电后无数据输入，功放放大噪声 | 初始化后 `0x32 = 0x00` 静音 DAC，播放前再恢复 |
| Launcher 提示音 / 录音回放断续、失真 | 每次 `playTone`/`startPlayback` 都重新创建/删除 I2S TX 通道，并频繁 mute/unmute DAC | I2S 通道持久化；`stopPlayback()` 只停数据不删通道；`playTone` 不再 mute DAC |
| 录音嘈杂、像失真 | ALC 把环境噪声一起放大，且麦克风增益过高 | 关闭 ALC，PGA 固定为 `0x14 = 0x10`；回放时通过 `0x32 = 0xD7` 提升音量 |
| 自动进入 Record 页面影响测试 | 临时调试代码 | 已移除，默认进入 Launcher |

---

## 5. 已知问题

### 5.1 SD 卡识别失败

- **现象**：`SDCard` 页面显示 `Not Found`，日志出现 `send_op_cond` timeout（`0x107`）。
- **可能原因**：
  - SPI2 与 LCD 共享，初始化顺序或片选冲突。
  - TF 卡模块供电不足或接线问题。
  - `SD_PIN_CD`（GPIO 15）未接入或卡检测逻辑需调整。
  - SPI 默认速率 100 kHz，可能需要更低上电时序。
- **状态**：待排查，当前为软件探测页面，不影响其他功能。

### 5.2 TCA8418 键盘 / MPU6050 无响应

- **现象**：I2C 扫描未检测到键盘/IMU。
- **可能原因**：板上未贴装对应器件，或 I2C 地址与当前配置不符。
- **状态**：代码中已做非致命处理，HAL 初始化继续运行。

### 5.3 录音与提示音音质

- **状态**：已完成持久化通道、去 ALC、固定增益等修改，**待实际硬件重测**。
- **预期**：反复旋转 launcher 的提示音应更干净；录音回放应避免“卡顿、爆破”。
- **注意**：开机第一声提示音仍可能有轻微 pop，因为 TX 通道首次从静音状态启动。

---

## 6. 相关文件清单

| 文件 | 说明 | 当前状态 |
|---|---|---|
| `ESP32S3N16R8/main/hal/audio/es8311.h` | ES8311 类定义 | 修改 |
| `ESP32S3N16R8/main/hal/audio/es8311.cpp` | 上电/模式序列 | 修改 |
| `ESP32S3N16R8/main/hal/audio/i2s_audio.h` | I2S 流接口（新增） | 新增 |
| `ESP32S3N16R8/main/hal/audio/i2s_audio.cpp` | I2S 通道持久化实现（新增） | 新增 |
| `ESP32S3N16R8/main/hal/hal.h` | 集成 `I2sAudio` | 修改 |
| `ESP32S3N16R8/main/hal/hal.cpp` | 初始化顺序、`playTone()` | 修改 |
| `ESP32S3N16R8/main/ui.h` | Record 页面状态定义 | 修改 |
| `ESP32S3N16R8/main/ui.cpp` | Record 页面流程、提示音调用 | 修改 |
| `ESP32S3N16R8/main/CMakeLists.txt` | 增加 `esp_driver_i2s`、新增 `i2s_audio.cpp` | 修改 |
| `ESP32S3N16R8/main/Kconfig.projbuild` | 音频引脚与采样率配置 | 修改 |
| `ESP32S3N16R8/PINOUT.md` | 引脚规划 | 修改 |
| `ESP32S3N16R8/AGENTS.md` | 当前目录 Agent 规则 | 新增 |

---

## 7. 构建与测试命令

```bash
# 1. 进入工程目录
cd M5Cardputer-UserDemo/ESP32S3N16R8

# 2. 构建
idf.py build

# 3. 烧录（注意先关闭 monitor 释放串口）
idf.py -p /dev/cu.usbserial-A5069RR4 flash

# 4. 串口监控（macOS 下若 monitor 异常，可用 script 包装）
script -q /tmp/esp_script.log idf.py -p /dev/cu.usbserial-A5069RR4 monitor
```

---

## 8. 测试 checklist

- [ ] 开机后 I2C 扫描能看到 `0x18`（ES8311）。
- [ ] Launcher 旋转时提示音响亮、无断续。
- [ ] 快速连续旋转时提示音不丢失、不爆音。
- [ ] 进入 Record 页面后波形有变化（录音中）。
- [ ] 按下编码器后能从喇叭回放录音。
- [ ] 反复切换录音/回放无异常。
- [ ] 退出 Record 页面后 Launcher 提示音音量正常。
- [ ] （可选）SD 卡页面能识别到卡片。

---

## 9. 后续 TODO

1. **音频质量收尾**：根据重测结果决定是否进一步调整 DAC 音量（`0x32`）、PGA 增益（`0x14`）或 I2S DMA 缓冲大小。
2. **SD 卡修复**：检查 SPI 共享逻辑、供电、卡检测引脚，必要时降低初始化速率。
3. **键盘/IMU**：若硬件已贴装，核对 I2C 地址、中断引脚和上电时序。
4. **代码整理**：将音频配置（增益、音量、缓冲区大小）抽取到 `Kconfig` 或头文件常量，方便调试。
