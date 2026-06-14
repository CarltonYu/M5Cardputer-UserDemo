# ESP32-S3-N16R8 User Demo 引脚规划

> **Rule**：本文件是本项目唯一的引脚接入规范。任何引脚接法变更都必须先更新本文件，再同步修改 `Kconfig.projbuild`、`sdkconfig.defaults` 和对应驱动代码。

## 当前硬件清单

- ESP32-S3-N16R8 开发板（16 MB Flash，8 MB PSRAM）
- 2.0 寸 320×240 ST7789 SPI LCD
- EC11 旋转编码器（A/B/push）
- OK 按钮
- TF 卡模块（5 脚：CS/MOSI/MISO/SCK/CD）
- （可选）TCA8418 键盘、ES8311 音频、MPU6050 IMU 等 I2C 设备

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

## 不可用/已保留引脚

| GPIO | 用途 | 备注 |
|---|---|---|
| GPIO 0 | Boot / Strapping | 谨慎使用 |
| GPIO 1 / 2 | UART0 TX / RX | Console 日志输出，不可占用 |
| GPIO 19 / 20 | USB D- / D+ | USB 功能保留 |
| GPIO 26 ~ 32 | SPI0/1 Flash + PSRAM | 芯片内置存储器专用，不可使用 |
| GPIO 39 ~ 42 | JTAG | 尽量保留，避免冲突 |

## 变更记录

| 日期 | 变更内容 | 相关提交 |
|---|---|---|
| 2026-06-14 | 初始规划：LCD、EC11、OK、双 I2C、TF 卡 | - |
