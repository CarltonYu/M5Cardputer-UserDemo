# ESP32S3N16R8 User Demo Port — Agent Notes

本目录是 `M5Cardputer-UserDemo` 项目的 ESP32-S3-N16R8 移植版本，与根目录的 `AGENTS.md` 共同生效。本文件中的规则优先于根目录的通用规则。

## 引脚文档规则（强制）

**任何硬件引脚变更，必须先更新 `PINOUT.md`，再同步修改代码。**

具体包括：

1. **新增硬件**：必须在 `PINOUT.md` 的「当前硬件清单」中登记该硬件，并在「引脚总表」中补充其所有信号与 GPIO 对应关系。
2. **引脚复用/变更**：若某功能改用新的 GPIO，必须同步更新：
   - `PINOUT.md` 引脚总表
   - `main/Kconfig.projbuild` 中的对应 `config` 默认值
   - `sdkconfig.defaults`（如存在该键值）
   - 驱动代码中硬编码的引脚宏/变量
3. **保留引脚调整**：若新硬件占用了原先标记为「不可用/已保留」的引脚，必须同步更新「不可用/已保留引脚」表，并说明原因。
4. **禁止行为**：不允许在代码或配置中直接使用 GPIO 数字而不在 `PINOUT.md` 中登记。

## 当前关键约束

- **GPIO 8 / 9**：LCD RST / DC 专用，禁止用于 I2C 或其他功能。
- **内部 I2C**：SDA = GPIO 17，SCL = GPIO 18，连接 ES8311、TCA8418、MPU6050 等板载 I2C 设备。
- **SPI2 共享**：LCD 与 TF 卡共享 SPI2，MOSI = GPIO 11，SCK = GPIO 12，MISO = GPIO 13。
- **ES8311 + NS4150B 音频模块**：I2C 接内部 I2C，I2S 使用 GPIO 35/36/37/38/47。
