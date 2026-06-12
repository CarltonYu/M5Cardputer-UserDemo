# ESP32-S3-N16R8 User Demo Port

这是从 M5Stack Cardputer ADV User Demo 拆出来的第一阶段移植工程，目标板是普通 ESP32-S3-N16R8 开发板。

当前范围：

- 320x240 ST7789 SPI LCD 点亮与全屏刷新
- EC11 旋转编码器 A/B 左右移动菜单
- Cardputer ADV User Demo 风格 launcher UI：左侧键盘状态条、顶部系统条、原工程应用图标菜单
- EC11 push 作为 Enter/确认：launcher 中打开选中 app，Chat 中发送当前输入
- OK 按钮作为退出/返回：app 内返回 launcher
- ESP-NOW Chat 第一阶段复刻：兼容 Cardputer ADV Demo 的 ESP-NOW 组件帧格式，预设短句输入、广播发送、接收消息滚动显示

## 默认接线

这些 GPIO 都可在 `idf.py menuconfig` 中修改。

| 模块 | 引脚 | 默认 GPIO | 说明 |
| --- | --- | ---: | --- |
| ST7789 | SCL/SCLK | 12 | SPI clock |
| ST7789 | SDA/MOSI | 11 | SPI MOSI |
| ST7789 | CS | 10 | 片选 |
| ST7789 | DC | 9 | 数据/命令 |
| ST7789 | RES/RST | 8 | 复位 |
| ST7789 | BL/BLK | 7 | 背光，高电平有效 |
| ST7789 | VCC | 3V3 | 按屏幕模块要求供电 |
| ST7789 | GND | GND | 共地 |
| EC11 | A | 4 | 内部上拉，低电平有效 |
| EC11 | B | 5 | 内部上拉，低电平有效 |
| EC11 | push | 6 | 内部上拉，低电平有效 |
| Button | OK | 3 | 内部上拉，低电平有效 |

EC11 和 OK 的公共端接 GND。若你的开发板这些 GPIO 已被占用，先改 `menuconfig` 再烧录。

## 操作

- Launcher：旋转 EC11 左右移动选中项，按 EC11 push 打开选中 app。
- Chat：旋转 EC11 切换预设输入内容，按 EC11 push 通过 ESP-NOW 广播发送。
- App 内：按 OK 返回 launcher。Launcher 页面按 OK 不会打开 app。

由于当前硬件只有 EC11 push 和 OK，没有完整键盘，Chat 的输入先用预设短句代替键盘输入。当前发送和接收都按 Cardputer ADV User Demo 使用的 Espressif ESP-NOW 组件帧格式处理；同时也兼容裸 ESP-NOW 文本。默认 ESP-NOW 信道为 1。

## 编译

```bash
source /Users/carlton/esp/esp-idf-v5.4.2/export.sh
idf.py -C ESP32S3N16R8 set-target esp32s3
idf.py -C ESP32S3N16R8 build
```

构建产物会在 `ESP32S3N16R8/build/` 下生成，包括 `esp32s3n16r8-user-demo.bin`、bootloader 和 partition table。`release/` 包里额外放了一个 0x0 起烧的合并固件。

## 烧录

```bash
idf.py -C ESP32S3N16R8 -p /dev/tty.usbmodem14201 flash monitor
```

如果用 merged binary 手动烧录：

```bash
python -m esptool --chip esp32s3 -p /dev/tty.usbmodem14201 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 ESP32S3N16R8/release/esp32s3n16r8-user-demo-merged.bin
```

## 常见调整

```bash
idf.py -C ESP32S3N16R8 menuconfig
```

进入 `ESP32S3N16R8 User Demo Port`：

- 屏幕方向不对：调整 `LCD_SWAP_XY`、`LCD_MIRROR_X`、`LCD_MIRROR_Y`。当前默认是 `SWAP_XY + MIRROR_X`，是在上一次左右镜像固件基础上再旋转 180 度后的配置。
- 颜色反相：切换 `LCD_INVERT_COLOR`
- 红蓝颠倒：切换 `LCD_BGR_COLOR`
- 画面有偏移：调整 `LCD_GAP_X`、`LCD_GAP_Y`
- 编码器左右相反：打开 `ENCODER_REVERSE`
- OK 或 push 未接：对应 GPIO 设为 `-1`
- ESP-NOW 信道：调整 `ESPNOW_CHANNEL`，两端必须一致

## 后续移植建议

当前仍未引入 M5Unified/Mooncake，避免被 Cardputer ADV 的键盘、音频、电源、I2C 设备绑住。下一步可以继续把 `LcdSt7789`、`RotaryInput` 和 `EspNowChat` 包成新的 HAL，再逐步复刻其他 app 的具体功能。
