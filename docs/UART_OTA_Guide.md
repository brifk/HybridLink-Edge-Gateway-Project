# UART OTA 升级系统完整指南

## 📋 系统架构

```
┌─────────────┐     MQTT      ┌─────────────┐     UART      ┌─────────────┐
│   云端服务器  │ ──────────── │  OrangePi   │ ──────────── │  ESP32-S3   │
│   (EMQX)    │   固件下发     │   网关       │   固件传输    │   设备       │
└─────────────┘              └─────────────┘              └─────────────┘
      │                            │                            │
      │ 1. 上传固件                 │ 2. 接收固件                 │ 3. 写入Flash
      │ 2. 发布到MQTT              │ 3. 解析并校验               │ 4. 重启验证
      │                            │ 4. 通过UART发送             │ 5. 回滚保护
      │                            │                            │
```

---

## 🔧 第一阶段：基础设施搭建

### 1.1 EMQX MQTT服务器

#### Docker快速部署
```bash
# 拉取并运行EMQX
docker run -d --name emqx \
    -p 1883:1883 \
    -p 8083:8083 \
    -p 8084:8084 \
    -p 8883:8883 \
    -p 18083:18083 \
    emqx/emqx:latest

# 访问管理界面: http://your-server:18083
# 默认账号: admin / public
```

#### 创建OTA专用用户
1. 登录EMQX Dashboard
2. 访问 "认证" -> "用户管理"
3. 创建用户: `ota_gateway` / `your_password`
4. 配置ACL权限：
   - 允许发布: `ota/#`
   - 允许订阅: `ota/#`

### 1.2 OrangePi网关配置

#### 硬件连接
```
OrangePi GPIO        ESP32-S3
─────────────        ─────────
TX (Pin 8)    ────── GPIO18 (RX)
RX (Pin 10)   ────── GPIO17 (TX)
GND           ────── GND
```

#### EC20 4G模块连接
```
OrangePi USB         EC20模块
────────────         ────────
USB Host      ────── USB接口

# EC20将创建以下设备:
# /dev/ttyUSB0 - DM端口
# /dev/ttyUSB1 - GPS端口
# /dev/ttyUSB2 - AT命令端口
# /dev/ttyUSB3 - PPP拨号端口
```

#### 安装网关软件
```bash
cd orangepi_gateway
sudo ./install.sh

# 编辑配置
sudo nano /opt/ota_gateway/config.env

# 启动服务
sudo systemctl start ec20-dial
sudo systemctl start ota-gateway
```

---

## 🔧 第二阶段：ESP32-S3 OTA实现

### 2.1 协议帧格式

```
┌─────┬─────┬─────┬──────┬──────┬────────┬──────────┬───────┬─────┐
│帧头  │版本  │命令  │序列号 │偏移量  │数据长度 │ Payload  │ CRC16 │帧尾 │
│2B   │1B   │1B   │2B    │4B     │2B      │ 0~1024B  │  2B   │2B   │
├─────┼─────┼─────┼──────┼──────┼────────┼──────────┼───────┼─────┤
│0xAA │0x01 │CMD  │SEQ   │OFFSET│LEN     │ DATA     │ CRC   │0x55 │
│0x55 │     │     │      │      │        │          │       │0xAA │
└─────┴─────┴─────┴──────┴──────┴────────┴──────────┴───────┴─────┘
```

### 2.2 命令类型

| 命令 | 值 | 方向 | 说明 |
|------|-----|------|------|
| OTA_START | 0x01 | 主机→ESP32 | 开始升级 |
| OTA_DATA | 0x02 | 主机→ESP32 | 数据块 |
| OTA_END | 0x03 | 主机→ESP32 | 传输完成 |
| OTA_ABORT | 0x04 | 主机→ESP32 | 中止升级 |
| OTA_QUERY_STATUS | 0x05 | 主机→ESP32 | 查询状态 |
| OTA_ROLLBACK_REQ | 0x06 | 主机→ESP32 | 请求回滚 |
| OTA_ACK | 0x80 | ESP32→主机 | 确认 |
| OTA_NACK | 0x81 | ESP32→主机 | 拒绝 |
| OTA_READY | 0x82 | ESP32→主机 | 准备就绪 |
| OTA_PROGRESS | 0x83 | ESP32→主机 | 进度报告 |
| OTA_COMPLETE | 0x84 | ESP32→主机 | 完成 |

### 2.3 在ESP32项目中使用

#### main.cpp 示例
```cpp
#include "UartOTAReceiver.hpp"

// 全局OTA接收器
static UartOTAReceiver* g_otaReceiver = nullptr;

extern "C" void app_main(void)
{
    // ===== 首启自检 (必须在app_main开始时执行) =====
    if (!UartOTAReceiver::performDiagnostic()) {
        // 自检失败，回滚到之前的固件
        UartOTAReceiver::markAppInvalidAndRollback();
        // 不会返回
    }
    
    // 自检通过，标记应用有效
    UartOTAReceiver::markAppValid();
    
    // ===== 初始化其他组件 =====
    // ...
    
    // ===== 启动UART OTA接收器 =====
    g_otaReceiver = new UartOTAReceiver(
        UART_NUM_1,  // 使用UART1
        17,          // TX引脚
        18,          // RX引脚
        921600       // 波特率
    );
    
    // 设置回调
    g_otaReceiver->setProgressCallback([](uint32_t received, uint32_t total, uint8_t percent) {
        ESP_LOGI("Main", "OTA Progress: %d%%", percent);
    });
    
    g_otaReceiver->setCompleteCallback([](bool success, OTA::ErrorCode error) {
        if (success) {
            ESP_LOGI("Main", "OTA completed!");
        } else {
            ESP_LOGE("Main", "OTA failed: 0x%02X", (int)error);
        }
    });
    
    // 初始化并启动
    if (g_otaReceiver->init() == ESP_OK) {
        g_otaReceiver->start();
        ESP_LOGI("Main", "UART OTA receiver started");
    }
    
    // ===== 主循环 =====
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### 2.4 回滚机制详解

```
┌─────────────────────────────────────────────────────────────┐
│                      OTA启动流程                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. 设备重启                                                 │
│       ↓                                                     │
│  2. 检查分区状态                                             │
│       ↓                                                     │
│  ┌─────────────────────────────────────┐                    │
│  │ ESP_OTA_IMG_PENDING_VERIFY?         │                    │
│  │ (首次从新固件启动)                   │                    │
│  └──────────────┬──────────────────────┘                    │
│          是     │         否                                │
│       ↓         │      ↓                                    │
│  3. 执行自检     │   正常启动                                │
│       ↓         │                                           │
│  ┌───────────────────────────┐                              │
│  │ 自检通过?                  │                              │
│  └─────────┬─────────────────┘                              │
│      是    │         否                                     │
│      ↓     │      ↓                                        │
│  4a. markAppValid()    4b. markAppInvalidAndRollback()     │
│      (取消回滚)             (回滚到旧固件并重启)              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 🔧 第三阶段：联调与测试

### 3.1 固件打包与上传

#### 编译固件
```bash
cd HybridLink-Edge-Gateway-Project
idf.py build

# 固件位置: build/HybridLink.bin
```

#### 上传到MQTT (Python脚本)
```python
#!/usr/bin/env python3
"""
firmware_uploader.py - 固件上传工具
"""
import json
import base64
import hashlib
import paho.mqtt.client as mqtt
from pathlib import Path

def upload_firmware(
    broker: str,
    port: int,
    device_id: str,
    firmware_path: str,
    version: str
):
    # 读取固件
    firmware = Path(firmware_path).read_bytes()
    
    # 计算MD5
    md5 = hashlib.md5(firmware).hexdigest()
    
    # 构建消息
    message = {
        "version": version,
        "project": "HybridLink",
        "size": len(firmware),
        "md5": md5,
        "data": base64.b64encode(firmware).decode('ascii')
    }
    
    # 发布到MQTT
    client = mqtt.Client()
    client.connect(broker, port)
    
    topic = f"ota/{device_id}/firmware"
    client.publish(topic, json.dumps(message), qos=1)
    
    print(f"Firmware uploaded to {topic}")
    print(f"Size: {len(firmware)} bytes")
    print(f"MD5: {md5}")
    
    client.disconnect()

if __name__ == '__main__':
    upload_firmware(
        broker="your-emqx-server.com",
        port=1883,
        device_id="esp32_device_001",
        firmware_path="build/HybridLink.bin",
        version="1.0.1"
    )
```

### 3.2 监控升级过程

#### 订阅状态主题
```bash
# 使用mosquitto_sub监控
mosquitto_sub -h your-emqx-server.com -t "ota/esp32_device_001/#" -v
```

#### 状态消息示例
```json
// 开始升级
{"device_id": "esp32_device_001", "status": "upgrading", "message": "Starting OTA: 1.0.1"}

// 进度
{"received": 102400, "total": 512000, "percent": 20}

// 完成
{"device_id": "esp32_device_001", "status": "success", "message": "OTA completed: 1.0.1"}
```

### 3.3 直接串口测试

```bash
# 在OrangePi上直接发送固件
cd /opt/ota_gateway
python3 uart_ota_sender.py \
    /path/to/firmware.bin \
    -p /dev/ttyUSB0 \
    -b 921600 \
    -v 1.0.1
```

### 3.4 查询设备状态

```bash
# 发送状态查询命令
mosquitto_pub -h your-emqx-server.com \
    -t "ota/esp32_device_001/control" \
    -m '{"command": "status"}'
```

### 3.5 请求回滚

```bash
# 发送回滚命令
mosquitto_pub -h your-emqx-server.com \
    -t "ota/esp32_device_001/control" \
    -m '{"command": "rollback"}'
```

---

## 📁 项目文件结构

```
HybridLink-Edge-Gateway-Project/
├── components/
│   └── OTAServer/
│       ├── CMakeLists.txt
│       ├── OTAServer.cpp          # 原HTTP OTA (保留)
│       ├── UartOTAReceiver.cpp    # 新UART OTA接收器
│       └── include/
│           ├── OTAServer.hpp
│           ├── OTAProtocol.hpp    # 协议定义
│           └── UartOTAReceiver.hpp
│
├── orangepi_gateway/
│   ├── ota_protocol.py            # Python协议定义
│   ├── uart_ota_sender.py         # UART发送器
│   ├── mqtt_ota_client.py         # MQTT客户端
│   ├── ec20_dial.sh               # 4G拨号脚本
│   ├── install.sh                 # 安装脚本
│   └── requirements.txt           # Python依赖
│
└── partitions.csv                 # 分区表(需支持OTA)
```

---

## ⚠️ 注意事项

### 分区表配置
确保 `partitions.csv` 包含两个OTA分区：
```csv
# Name,   Type, SubType, Offset,  Size,   Flags
nvs,      data, nvs,     0x9000,  0x6000,
otadata,  data, ota,     0xf000,  0x2000,
phy_init, data, phy,     0x11000, 0x1000,
ota_0,    app,  ota_0,   0x20000, 0x1E0000,
ota_1,    app,  ota_1,   0x200000,0x1E0000,
```

### 波特率选择
- 921600 bps: 推荐，传输速度快
- 460800 bps: 兼容性好
- 115200 bps: 最稳定，但速度慢

### 固件大小限制
- 单个OTA分区最大: 约1.9MB (取决于分区表配置)
- 建议开启固件压缩: `idf.py menuconfig` -> Component config -> ESP32 -> OTA

---

## 🔍 故障排除

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| CRC校验失败 | 串口噪声、波特率不匹配 | 降低波特率，检查接线 |
| 序列号错误 | 丢包 | 检查Ring Buffer大小 |
| 镜像校验失败 | 固件损坏 | 重新编译并上传 |
| 回滚循环 | 自检逻辑有问题 | 检查diagnostic函数 |
| 分区错误 | 分区表配置错误 | 检查partitions.csv |
