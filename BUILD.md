# zhengchen-eye 编译烧录指南

## 项目信息

- **项目路径**: `/mnt/d/work/langmem/eye/zhengchen-eye`
- **芯片**: ESP32-S3 (QFN56, 8MB PSRAM)
- **ESP-IDF**: v5.4
- **LCD**: GC9A01, 240x240 分辨率
- **Flash**: 16MB, DIO 模式, 80MHz

## 关键配置 (编译前必检查)

LCD 驱动配置必须正确，否则屏幕不显示：

```bash
# 检查配置
grep -E "USE_LCD_240X240|USE_LCD_160X160" sdkconfig
```

**正确配置**:
```
CONFIG_USE_LCD_240X240_GIF1=y
# CONFIG_USE_LCD_160X160_GIF1 is not set
```

**错误配置** (屏幕不显示):
```
# CONFIG_USE_LCD_240X240_GIF1 is not set
CONFIG_USE_LCD_160X160_GIF1=y
```

修复命令：
```bash
sed -i 's/# CONFIG_USE_LCD_240X240_GIF1 is not set/CONFIG_USE_LCD_240X240_GIF1=y/' sdkconfig
sed -i 's/CONFIG_USE_LCD_160X160_GIF1=y/# CONFIG_USE_LCD_160X160_GIF1 is not set/' sdkconfig
```

## 编译方式

### Docker 编译 (推荐)

在 WSL 中执行，无需配置 Windows ESP-IDF 环境：

```bash
# 完整清理编译 (约 10-15 分钟)
rm -rf /mnt/d/work/langmem/eye/zhengchen-eye/build && \
docker run --rm -v /mnt/d/work/langmem/eye/zhengchen-eye:/project -w /project espressif/idf:v5.4 idf.py build
```

监控编译进度：
```bash
docker logs --tail 30 $(docker ps -q --filter ancestor=espressif/idf:v5.4)
```

### Windows 编译 (备选)

需要安装 ESP-IDF v5.4 并配置环境变量，在 **ESP-IDF 命令行** 中执行：

```cmd
cd D:\work\langmem\eye\zhengchen-eye
idf.py fullclean
idf.py build
```

## 烧录流程

### 1. 合并固件

```bash
cd /mnt/d/work/langmem/eye/zhengchen-eye/build

esptool.py --chip esp32s3 merge_bin \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size 16MB \
  -o merged_firmware.bin \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x10000 srmodels/srmodels.bin \
  0x410000 xiaozhi.bin
```

### 2. 烧录

```bash
sudo chmod 666 /dev/ttyACM0

esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 /mnt/d/work/langmem/eye/zhengchen-eye/build/merged_firmware.bin
```

## 一键命令

### 完整流程 (编译 + 合并 + 烧录)

```bash
rm -rf /mnt/d/work/langmem/eye/zhengchen-eye/build && \
docker run --rm -v /mnt/d/work/langmem/eye/zhengchen-eye:/project -w /project espressif/idf:v5.4 idf.py build && \
cd /mnt/d/work/langmem/eye/zhengchen-eye/build && \
esptool.py --chip esp32s3 merge_bin --flash_mode dio --flash_freq 80m --flash_size 16MB \
  -o merged_firmware.bin \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x10000 srmodels/srmodels.bin \
  0x410000 xiaozhi.bin && \
sudo chmod 666 /dev/ttyACM0 && \
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 merged_firmware.bin
```

### 仅烧录 (已编译)

```bash
cd /mnt/d/work/langmem/eye/zhengchen-eye/build && \
esptool.py --chip esp32s3 merge_bin --flash_mode dio --flash_freq 80m --flash_size 16MB \
  -o merged_firmware.bin \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0xd000 ota_data_initial.bin \
  0x10000 srmodels/srmodels.bin \
  0x410000 xiaozhi.bin && \
sudo chmod 666 /dev/ttyACM0 && \
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 merged_firmware.bin
```

## 验证烧录

```bash
python3 -c "
import serial, time
ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
ser.setDTR(False); ser.setRTS(True); time.sleep(0.1)
ser.setRTS(False); time.sleep(0.5)
start = time.time()
while time.time() - start < 20:
    line = ser.readline()
    if line: print(line.decode('utf-8', errors='ignore'), end='')
ser.close()
"
```

**验证要点**:
1. `Compile time:` - 确认是最新编译时间
2. `gc9a01: LCD panel create success` - 正确的 LCD 驱动
3. `GIF initialized, size=240x240` - 显示初始化成功

## 分区表

| 偏移地址 | 文件 | 说明 |
|----------|------|------|
| 0x00000000 | bootloader.bin | 引导加载程序 |
| 0x00008000 | partition-table.bin | 分区表 |
| 0x0000d000 | ota_data_initial.bin | OTA 数据 |
| 0x00010000 | srmodels.bin | 语音识别模型 |
| 0x00410000 | xiaozhi.bin | 主应用程序 (8MB) |

## 常见问题

### 屏幕不显示
1. 检查 sdkconfig 的 LCD 配置
2. 确认使用 `CONFIG_USE_LCD_240X240_GIF1=y`
3. 确认日志显示 `gc9a01` 而不是 `gc9d01n`

### 编译时间未更新
Docker 编译时需要先删除 build 目录：
```bash
rm -rf /mnt/d/work/langmem/eye/zhengchen-eye/build
```

### USB 设备未识别
在 Windows PowerShell (管理员) 中执行：
```powershell
usbipd list
usbipd bind --busid 3-1
usbipd attach --wsl --busid 3-1
```

### 烧录权限问题
```bash
sudo chmod 666 /dev/ttyACM0
```

---

**最后更新**: 2025-12-19
**验证状态**: 成功 (GC9A01 LCD, 240x240 GIF 动画正常显示)
