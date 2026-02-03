# HybridLink-Edge-Gateway-Project

基于ESP32-S3和OrangePi的异构架构工业边缘计算网关项目，用于工业现场的高频信号采集、边缘计算和数据上云。

## 项目概述

这是一个工业级边缘计算网关解决方案，采用异构架构设计：

- **ESP32-S3**：负责实时数据采集、边缘计算和本地控制
- **OrangePi + EC20**：作为网关和4G通信模块，负责数据上云和固件分发

## 系统架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           完整 OTA 升级流程                               │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────┐   MQTT (4G)   ┌───────────────┐   HTTP (WiFi)   ┌───────────┐
│  云端    │ ──通知────→   │   OrangePi    │ ←───请求固件─── │  ESP32-S3 │
│ (EMQX)  │               │ (网关+HTTP服务)│ ────传输固件──→ │  (设备端)  │
└─────────┘               └───────────────┘                 └───────────┘
     │                           │
     │   4G 下载固件              │ WiFi 局域网
     └───────────────────────────┘
```

## 核心功能

### 1. AMP双核架构设计 (ESP32-S3)
- **Core 0 (协议核)**: 运行Wi-Fi和I2C通信任务，处理与OrangePi和传感器的数据交互
- **Core 1 (计算核)**: 运行高优先级的计算任务，使用ESP-DSP库进行FFT频谱分析
- 利用StreamBuffer实现无锁跨核通信，提升系统实时性

### 2. 边缘计算实现
- 集成ESP-DSP库，在MCU端实现1024点FFT运算
- 在边缘侧直接提取振动信号特征，减少90%的无效数据上云流量
- 实时分析传感器数据并提取关键指标

### 3. HTTP OTA升级方案
- 设计基于esp_https_ota的固件升级方案
- OrangePi作为固件分发服务器，支持双分区回滚机制
- 确保远程升级的可靠性

### 4. 4G链路管理
- 在Linux端实现EC20模组的AT指令集封装与PPP拨号守护进程
- 确保网关在工业现场的7x24小时在线

### 5. 传感器集成
- BNO055九轴传感器：用于姿态检测和线性加速度测量
- LED指示灯：状态显示和用户交互
- WiFi通信：与局域网设备通信

## 硬件环境

- **主控芯片**: ESP32-S3 (双核 240MHz)
- **协处理器**: OrangePi Zero 2W
- **通信模块**: Quectel EC20 4G模组
- **传感器**: BNO055九轴传感器

## 软件环境

- **ESP32-S3**: FreeRTOS (ESP-IDF)
- **OrangePi**: Linux
- **通信协议**: MQTT, HTTP
- **编程语言**: C/C++ (ESP32端), Python (OrangePi端)

## 项目结构

```
HybridLink-Edge-Gateway-Project/
├── components/                 # 组件目录
│   ├── Core/                   # 核心基类
│   ├── OTAServer/              # OTA升级服务
│   ├── bno055/                 # BNO055传感器驱动
│   ├── calculate/              # DSP计算引擎
│   ├── led/                    # LED控制
│   └── network/                # 网络通信 (WiFi/MQTT)
├── docs/                       # 文档
│   ├── HTTP_OTA_Guide.md       # OTA升级指南
│   └── Target.md               # 项目设计方案
├── main/                       # 主程序
├── orangepi_gateway/           # OrangePi端网关程序
│   ├── mqtt_ota_client.py      # MQTT OTA客户端
│   ├── ota_http_server.py      # HTTP固件服务器
│   └── requirements.txt        # Python依赖
└── tools/                      # 工具脚本
    └── firmware_uploader.py    # 固件上传工具
```

## 快速开始

### ESP32-S3端编译和烧录

```bash
# 设置ESP-IDF环境
idf.py build
idf.py flash monitor
```

### OrangePi端部署

```bash
# 安装依赖
pip install -r orangepi_gateway/requirements.txt

# 启动MQTT OTA客户端
python orangepi_gateway/mqtt_ota_client.py

# 启动HTTP固件服务器
python orangepi_gateway/ota_http_server.py
```

### 网络配置

修改`main/APPConfig.h`中的WiFi配置：

```c
#define SSID "your_wifi_ssid"
#define PASSWORD "your_wifi_password"
#define MQTT_BROKER_URL "mqtt://your_mqtt_broker:1883"
```

## 核心技术亮点

1. **面向对象的FreeRTOS封装**: 使用C++封装FreeRTOS任务，实现优雅的任务管理
2. **双核异构架构**: 通信与计算任务分离，提高系统实时性
3. **边缘智能计算**: 在MCU端实现信号处理算法，减少云端负载
4. **可靠的OTA机制**: 双分区固件升级，支持自动回滚
5. **工业级稳定性**: 7x24小时运行设计，具备完善的错误处理机制

## 应用场景

- 工业设备状态监测
- 振动分析与故障预警
- 远程设备监控
- 边缘AI推理应用
- IoT数据采集网关

## 开发维护

本项目遵循现代嵌入式开发最佳实践，代码结构清晰，易于扩展和维护。