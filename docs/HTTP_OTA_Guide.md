# HTTP OTA 升级指南

## 概述

本项目实现了基于 HTTP 的 OTA（Over-The-Air）固件升级方案。

### OTA 链路

```
┌─────────┐    MQTT     ┌───────────┐    4G     ┌─────────┐
│  云端   │ ──通知──→  │  OrangePi │ ──下载──→ │  云端   │
│ (EMQX)  │            │   网关    │           │ 固件库  │
└─────────┘            └───────────┘           └─────────┘
                             │
                             │ HTTP (WiFi)
                             ▼
                       ┌───────────┐
                       │  ESP32-S3 │
                       │ (设备端)   │
                       └───────────┘
```

**完整流程：**
1. 云端通过 MQTT 发送固件更新通知给 OrangePi
2. OrangePi 通过 4G 网络从云端下载固件
3. OrangePi 启动 HTTP 服务器，存储固件
4. OrangePi 通过 MQTT 通知 ESP32 进行升级
5. ESP32 通过 WiFi 使用 `esp_https_ota` API 从 OrangePi 下载固件

## 系统架构

### ESP32 端

使用 ESP-IDF 的 `esp_https_ota` API 实现固件下载和升级。

**核心组件：**
- `OTAServer` 类：封装了 OTA 下载逻辑
- 支持进度回调
- 支持自动/手动重启
- 双分区（OTA_0/OTA_1）轮换升级

**使用示例：**
```cpp
#include "OTAServer.hpp"

// 创建 OTA 服务器实例
OTAServer ota;

// 设置固件下载 URL
ota.setURL("http://192.168.4.1:8000/firmware.bin");

// 可选：设置状态回调
ota.setStatusCallback([](OTAStatus status, int progress, const char* msg) {
    ESP_LOGI("OTA", "Status: %d, Progress: %d%%, Msg: %s", 
             (int)status, progress, msg);
});

// 启动 OTA 任务
ota.start();
```

### OrangePi 端

运行 HTTP 服务器和 MQTT 客户端。

**核心组件：**
- `ota_http_server.py`：HTTP 固件服务器
- `mqtt_ota_client.py`：MQTT 客户端，处理云端通知

## 安装部署

### OrangePi 安装

```bash
# 1. 将 orangepi_gateway 目录复制到 OrangePi
scp -r orangepi_gateway/ orangepi@<IP>:/home/orangepi/

# 2. 运行安装脚本
cd /home/orangepi/orangepi_gateway
sudo ./install.sh

# 3. 编辑配置文件
sudo nano /opt/ota_gateway/config.env

# 4. 启动服务
sudo systemctl start ec20-dial      # 启动 4G 网络
sudo systemctl start ota-gateway    # 启动 OTA 网关
```

### 配置文件说明

`/opt/ota_gateway/config.env`:

```bash
# MQTT 配置（连接云端 EMQX）
MQTT_HOST=your-emqx-server.com
MQTT_PORT=1883
MQTT_USER=username
MQTT_PASS=password

# 设备 ID
DEVICE_ID=esp32_device_001

# 网关 IP（ESP32 WiFi 连接地址）
GATEWAY_IP=192.168.4.1

# HTTP 服务器端口
HTTP_PORT=8000

# 固件存储目录
FIRMWARE_DIR=/opt/ota_firmware
```

## MQTT 主题

| 主题 | 方向 | 说明 |
|------|------|------|
| `ota/{device_id}/firmware/notify` | 云端 → 网关 | 固件更新通知 |
| `ota/{device_id}/control` | 云端 → 网关 | 控制命令 |
| `ota/{device_id}/status` | 网关 → 云端 | 状态上报 |
| `ota/{device_id}/progress` | 网关 → 云端 | 进度上报 |
| `esp32/{device_id}/ota/upgrade` | 网关 → ESP32 | 通知 ESP32 升级 |
| `esp32/{device_id}/ota/status` | ESP32 → 网关 | ESP32 状态上报 |

### 固件更新通知格式

云端发送到 `ota/{device_id}/firmware/notify`:

```json
{
    "version": "1.2.0",
    "download_url": "https://cloud.example.com/firmware/v1.2.0/firmware.bin",
    "md5": "abc123def456...",
    "size": 1234567,
    "release_notes": "Bug fixes and improvements"
}
```

### ESP32 升级通知格式

网关发送到 `esp32/{device_id}/ota/upgrade`:

```json
{
    "action": "upgrade",
    "url": "http://192.168.4.1:8000/firmware_v1.2.0.bin",
    "version": "1.2.0",
    "md5": "abc123def456...",
    "timestamp": 1699999999
}
```

## HTTP API

OrangePi HTTP 服务器端点：

| 端点 | 方法 | 说明 |
|------|------|------|
| `/{filename}.bin` | GET | 下载固件文件 |
| `/info` | GET | 获取固件列表（JSON） |
| `/health` | GET | 健康检查 |

### 固件信息响应

GET `/info`:

```json
{
    "status": "ok",
    "firmware_count": 2,
    "firmware_list": [
        {
            "file_name": "firmware_v1.0.0.bin",
            "file_size": 962125,
            "md5": "abc123...",
            "version": "1.0.0"
        },
        {
            "file_name": "firmware_v1.2.0.bin",
            "file_size": 985432,
            "md5": "def456...",
            "version": "1.2.0"
        }
    ]
}
```

## ESP32 分区表

确保使用 OTA 分区表（`partitions.csv`）：

```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x4000
otadata,  data, ota,     0xd000,   0x2000
phy_init, data, phy,     0xf000,   0x1000
ota_0,    app,  ota_0,   0x10000,  0x1E0000
ota_1,    app,  ota_1,   0x1F0000, 0x1E0000
```

## 测试流程

### 手动测试

1. **启动 OrangePi HTTP 服务器**：
   ```bash
   python3 /opt/ota_gateway/ota_http_server.py --port 8000
   ```

2. **放置测试固件**：
   ```bash
   cp firmware.bin /opt/ota_firmware/
   ```

3. **ESP32 测试下载**：
   ```cpp
   OTAServer ota;
   ota.setURL("http://192.168.4.1:8000/firmware.bin");
   ota.start();
   ```

### 端到端测试

1. 确保 OrangePi 已连接云端 MQTT
2. 从云端发送固件更新通知
3. 观察 OrangePi 下载固件
4. 观察 ESP32 自动升级

## 故障排除

### ESP32 下载失败

1. 检查 WiFi 连接状态
2. 确认 OrangePi IP 地址正确
3. 检查 HTTP 服务器是否运行：`curl http://192.168.4.1:8000/health`

### OrangePi 无法下载固件

1. 检查 4G 网络连接：`ping 8.8.8.8`
2. 检查云端固件 URL 是否可访问
3. 查看日志：`journalctl -u ota-gateway -f`

### OTA 升级后无法启动

ESP-IDF 支持自动回滚：
- 新固件启动后需调用 `esp_ota_mark_app_valid_cancel_rollback()`
- 否则重启后会自动回滚到旧固件

## 版本历史

- **v1.0.0**: 初始 HTTP OTA 实现
  - ESP32 端 `OTAServer` 类
  - OrangePi HTTP 服务器
  - MQTT 集成
