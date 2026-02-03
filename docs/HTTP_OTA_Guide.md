# HTTP OTA 升级指南

## 概述

本项目实现了基于 HTTP 的 OTA（Over-The-Air）固件升级方案，采用 **OrangePi 作为固件分发服务器**，ESP32-S3 通过 WiFi 主动拉取固件的架构。

## OTA 通信链路

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           完整 OTA 升级流程                               │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────┐   MQTT (4G)   ┌───────────────┐   HTTP (WiFi)   ┌───────────┐
│  云端   │ ────通知────→ │   OrangePi    │ ←───请求固件─── │  ESP32-S3 │
│ (EMQX)  │               │ (网关+HTTP服务)│ ────传输固件──→ │  (设备端)  │
└─────────┘               └───────────────┘                 └───────────┘
     │                           │
     │   4G 下载固件              │ WiFi 局域网
     └───────────────────────────┘

```

### 升级流程详解

| 步骤 | 说明 | 通信方式 |
|------|------|----------|
| 1 | 云端通过 MQTT 发送固件更新通知给 OrangePi | MQTT over 4G |
| 2 | OrangePi 从云端下载固件到本地 `/opt/ota_firmware/` | HTTP over 4G |
| 3 | OrangePi 启动 HTTP 服务器（端口 8000） | - |
| 4 | OrangePi 通过 MQTT 通知 ESP32 有新固件可用 | MQTT over WiFi |
| 5 | ESP32 使用 `esp_https_ota` API 从 OrangePi 拉取固件 | HTTP over WiFi |
| 6 | ESP32 写入 OTA 分区，验证后重启 | - |

## 系统架构

### ESP32-S3 端 (固件接收方)

基于 `OTAServer` C++ 类封装，继承自 `Thread` 基类，以独立任务方式运行。

**核心特性：**
- 使用 ESP-IDF 的 `esp_https_ota` 高级 API
- 支持下载进度回调（每 10% 上报一次）
- 双分区轮换升级（`ota_0` / `ota_1`）
- 支持自动回滚机制

**启动时自检代码（main.cpp）：**
```cpp
extern "C" void app_main()
{
    // HTTP OTA 首启自检
    OTAServer::printPartitionInfo();  // 打印当前分区信息
    ESP_LOGI(TAG, "Current firmware version: %s", OTAServer::getCurrentVersion());
    
    // ... 其他初始化代码
}
```

**触发 OTA 升级示例：**
```cpp
#include "OTAServer.hpp"

// 创建 OTA 任务实例
auto ota = std::make_unique<OTAServer>();

// 设置固件下载 URL（OrangePi HTTP 服务器地址）
ota->setURL("http://192.168.4.1:8000/firmware_v1.2.0.bin");

// 设置状态回调（可选）
ota->setStatusCallback([](OTAStatus status, int progress, const char* msg) {
    switch (status) {
        case OTAStatus::DOWNLOADING:
            ESP_LOGI("OTA", "下载进度: %d%%", progress);
            break;
        case OTAStatus::SUCCESS:
            ESP_LOGI("OTA", "升级成功，即将重启");
            break;
        case OTAStatus::FAILED:
            ESP_LOGE("OTA", "升级失败: %s", msg);
            break;
        default:
            break;
    }
});

// 启动 OTA 任务（继承自 Thread，运行在独立 FreeRTOS 任务中）
ota->start();
```

### OrangePi 端 (固件分发方)

运行两个核心 Python 服务：

| 服务 | 文件 | 职责 |
|------|------|------|
| HTTP 固件服务器 | `ota_http_server.py` | 提供固件下载、固件信息查询 |
| MQTT OTA 客户端 | `mqtt_ota_client.py` | 接收云端通知、下载固件、通知 ESP32 |

**服务启动流程：**
```bash
# 1. 启动 4G 拨号（EC20 模组）
sudo ./ec20_dial.sh

# 2. 启动 MQTT OTA 客户端（会自动启动 HTTP 服务器）
python3 mqtt_ota_client.py
```

## 分区表配置

项目使用自定义分区表 `partitions.csv`，支持双分区 OTA：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     ,        0x4000,
otadata,  data, ota,     ,        0x2000,
phy_init, data, phy,     ,        0x1000,

# OTA 双分区（每个 1.5MB）
ota_0,    app,  ota_0,   ,        1536K,
ota_1,    app,  ota_1,   ,        1536K,

# LittleFS 存储区（约 896KB）
storage,  data, littlefs,  ,      0xE0000,
```

**分区说明：**
- `ota_0` / `ota_1`：交替存储新旧固件，支持失败回滚
- `otadata`：记录当前启动分区和回滚状态
- `storage`：LittleFS 文件系统，用于配置存储

## MQTT 主题设计

| 主题 | 方向 | 说明 |
|------|------|------|
| `ota/{device_id}/firmware/notify` | 云端 → OrangePi | 固件更新通知 |
| `ota/{device_id}/status` | OrangePi → 云端 | 网关状态上报 |
| `ota/{device_id}/progress` | OrangePi → 云端 | 下载进度上报 |
| `esp32/{device_id}/ota/upgrade` | OrangePi → ESP32 | 通知 ESP32 升级 |
| `esp32/{device_id}/ota/status` | ESP32 → OrangePi | ESP32 升级状态 |

### 云端固件通知格式

```json
{
    "version": "1.2.0",
    "download_url": "https://cloud.example.com/firmware/v1.2.0/firmware.bin",
    "md5": "abc123def456...",
    "size": 1234567,
    "release_notes": "修复 BNO055 传感器读取问题"
}
```

### ESP32 升级通知格式

```json
{
    "action": "upgrade",
    "url": "http://192.168.4.1:8000/firmware_v1.2.0.bin",
    "version": "1.2.0",
    "md5": "abc123def456..."
}
```

## HTTP API 端点

OrangePi HTTP 服务器（默认端口 8000）：

| 端点 | 方法 | 说明 |
|------|------|------|
| `/{filename}.bin` | GET | 下载指定固件文件 |
| `/info` 或 `/info.json` | GET | 获取所有固件列表（JSON） |
| `/health` | GET | 服务健康检查 |

**固件列表响应示例：**
```json
{
    "status": "ok",
    "firmware_count": 1,
    "firmware_list": [
        {
            "file_name": "firmware_v1.2.0.bin",
            "file_size": 985432,
            "md5": "a1b2c3d4e5f6...",
            "version": "1.2.0"
        }
    ]
}
```

## OTA 回滚机制

ESP-IDF 提供自动回滚功能，防止升级失败导致设备变砖：

```cpp
// 新固件启动后，执行自检
if (OTAServer::performDiagnostic()) {
    // 自检通过，标记固件有效
    OTAServer::markAppValid();
    ESP_LOGI(TAG, "固件验证通过，取消回滚");
} else {
    // 自检失败，触发回滚
    OTAServer::markAppInvalidAndRollback();
    // 设备会自动重启并回滚到旧固件
}
```

**回滚触发条件：**
1. 新固件启动后未调用 `esp_ota_mark_app_valid_cancel_rollback()`
2. 新固件启动过程中发生崩溃
3. 手动调用 `esp_ota_mark_app_invalid_rollback_and_reboot()`

## 部署指南

### OrangePi 环境配置

```bash
# 1. 安装依赖
pip3 install -r requirements.txt

# 2. 创建固件存储目录
sudo mkdir -p /opt/ota_firmware
sudo chmod 755 /opt/ota_firmware

# 3. 配置 MQTT 连接（编辑 mqtt_ota_client.py 中的参数）
MQTT_HOST = "your-emqx-server.com"
MQTT_PORT = 1883

# 4. 启动服务
python3 mqtt_ota_client.py
```

### ESP32 编译配置

确保 `sdkconfig` 中启用 OTA 相关选项：
```
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

## 测试流程

### 本地测试（无云端）

1. **OrangePi 启动 HTTP 服务器**：
   ```bash
   python3 ota_http_server.py --port 8000 --firmware-dir /opt/ota_firmware
   ```

2. **放置测试固件**：
   ```bash
   cp build/HybridLink.bin /opt/ota_firmware/firmware_v1.0.0.bin
   ```

3. **ESP32 手动触发升级**：
   ```cpp
   auto ota = std::make_unique<OTAServer>();
   ota->setURL("http://192.168.4.1:8000/firmware_v1.0.0.bin");
   ota->start();
   ```

### 端到端测试

1. 确保 OrangePi 4G 网络正常：`ping 8.8.8.8`
2. 确保 ESP32 WiFi 已连接到 OrangePi 所在局域网
3. 从云端 MQTT 发送固件更新通知
4. 观察完整升级流程

## 故障排除

| 问题 | 排查步骤 |
|------|----------|
| ESP32 下载超时 | 1. 检查 WiFi 连接 `WifiStation::isConnected()` <br> 2. 测试 HTTP 可达性 `curl http://192.168.4.1:8000/health` |
| 固件校验失败 | 1. 检查固件文件完整性 <br> 2. 确认分区大小足够（1.5MB） |
| 升级后无法启动 | 1. 检查 `esp_ota_mark_app_valid` 是否调用 <br> 2. 等待自动回滚或手动刷入旧固件 |
| OrangePi 下载失败 | 1. 检查 4G 网络：`ping 8.8.8.8` <br> 2. 检查云端 URL 可访问性 |

## 版本历史

- **v1.0.0**: 初始 HTTP OTA 实现
  - ESP32 端 `OTAServer` 类（继承 Thread）
  - OrangePi HTTP 服务器 + MQTT 客户端
  - 双分区 OTA 支持
  - 自动回滚机制
