# ESP32-S3-N16R8 User Demo 引脚规划

> **Rule**：本文件是本项目唯一的引脚接入规范。任何引脚接法变更都必须先更新本文件，再同步修改 `Kconfig.projbuild`、`sdkconfig.defaults` 和对应驱动代码。
>
> **新增硬件规则**：每新增一块硬件，必须在本文件的「当前硬件清单」中登记，并在「引脚总表」中补充其所有用到的 GPIO 及信号说明；若占用了原先标记为「保留/不可用」的引脚，必须同步更新「不可用/已保留引脚」表。

## 当前硬件清单

- ESP32-S3-N16R8 开发板（16 MB Flash，8 MB PSRAM）
- 2.0 寸 320×240 ST7789 SPI LCD
- EC11 旋转编码器（A/B/push）
- OK 按钮
- TF 卡模块（5 脚：CS/MOSI/MISO/SCK/CD）
- ES8311 + NS4150B 音频模块（I2C 控制 + I2S 数字音频）
- （可选）TCA8418 键盘、MPU6050 IMU 等 I2C 设备

## 引脚总表

| 功能模块 | 信号 | GPIO | 说明 |
|---|---|---|---|
| **LCD** | BL | GPIO 7 | 背光，高电平有效 |
| | RST | GPIO 8 | 复位 |
| | DC | GPIO 9 | 数据/命令选择 |
| | CS | GPIO 10 | SPI 片选 |
| | MOSI | GPIO 11 | SPI MOSI，与 TF 卡共享 |
| | SCK | GPIO 12 | SPI 时钟，与 TF 卡共享 |
| **TF 卡 (SDSPI)** | CS | GPIO 14 | SD 卡独立片选 |
| | MOSI | GPIO 11 | 与 LCD MOSI 共享 |
| | MISO | GPIO 13 | 需接入 |
| | SCK | GPIO 12 | 与 LCD SCK 共享 |
| | CD | GPIO 15 | 卡检测，可不接 |
| **EC11 编码器** | A | GPIO 4 | 内部上拉，低电平有效 |
| | B | GPIO 5 | 内部上拉，低电平有效 |
| | push | GPIO 6 | 内部上拉，低电平有效 |
| **OK 按钮** | - | GPIO 3 | 内部上拉，低电平有效 |
| **内部 I2C** | SDA | GPIO 17 | 板载 I2C 设备（TCA8418/ES8311/MPU6050） |
| | SCL | GPIO 18 | |
| **外部 I2C (Grove)** | SDA | GPIO 15 | 外接 I2C 接口 |
| | SCL | GPIO 16 | |
| **键盘 INT** | - | GPIO 21 | TCA8418 中断，当前未使用时可不接 |
| **Audio (ES8311+NS4150B)** | I2C SDA | GPIO 17 | 与内部 I2C 共享，ES8311 控制总线 |
| | I2C SCL | GPIO 18 | 与内部 I2C 共享 |
| | I2S MCLK | GPIO 38 | I2S 主时钟 |
| | I2S BCLK / SCLK | GPIO 35 | I2S 位时钟 |
| | I2S LRCK / WS | GPIO 36 | I2S 左右声道时钟 |
| | I2S DIN | GPIO 37 | ES8311 ADC 数据输出 → ESP32 录音输入 |
| | I2S DOUT | GPIO 47 | ESP32 播放输出 → ES8311 DAC 数据输入 |

## 不可用/已保留引脚

| GPIO | 用途 | 备注 |
|---|---|---|
| GPIO 0 | Boot / Strapping | 谨慎使用 |
| GPIO 1 / 2 | UART0 TX / RX | Console 日志输出，不可占用 |
| GPIO 19 / 20 | USB D- / D+ | USB 功能保留 |
| GPIO 26 ~ 32 | SPI0/1 Flash + PSRAM | 芯片内置存储器专用，不可使用 |
| GPIO 39 ~ 42 | JTAG | 尽量保留，避免冲突 |
| GPIO 45 / 46 | Strapping | 启动配置引脚，避免使用 |

## ES8311 + NS4150B 模块使用注意事项

> 参考模块厂商文档：https://www.xinlucity.com/?s=resourcedetail/index/id/145.html

1. **NS4150B 功放必须 5V 供电**，仅给 3.3 V 时功放完全不工作，喇叭无声。
2. **ES8311 必须外部提供 MCLK**，主控不输出 MCLK 时 Codec 会初始化失败，I2C 通信也可能异常。
3. **I2C 信号线尽量短**，建议 SDA/SCL 不超过 5 cm，并远离天线等干扰源。
4. **回环测试定位问题**：
   - 接耳机有声音、接喇叭无声 → Codec 正常，问题在 NS4150B 功放电路（供电/使能/喇叭）。
   - 喇叭有杂音但无音乐 → 功放工作，问题在 Codec 配置或 I2S 数据传输。

## 变更记录

| 日期 | 变更内容 | 相关提交 |
|---|---|---|
| 2026-06-14 | 初始规划：LCD、EC11、OK、双 I2C、TF 卡 | - |
| 2026-06-15 | 增加 ES8311 + NS4150B 音频模块 I2S/I2C 引脚及使用注意事项 | - |
