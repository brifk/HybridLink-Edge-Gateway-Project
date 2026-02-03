#!/bin/bash
#
# OrangePi OTA 网关安装脚本
# HTTP OTA 方案：OrangePi 作为 HTTP 固件服务器
#

set -e

echo "================================"
echo "OrangePi OTA Gateway Installer"
echo "HTTP OTA Server Setup"
echo "================================"

# 检查是否为root用户
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/opt/ota_gateway"
FIRMWARE_DIR="/opt/ota_firmware"
SERVICE_NAME="ota-gateway"

# ============== 安装依赖 ==============
echo "[1/5] Installing dependencies..."

apt-get update
apt-get install -y \
    python3 \
    python3-pip \
    python3-venv \
    ppp \
    usb-modeswitch \
    usb-modeswitch-data

# 安装Python依赖
pip3 install paho-mqtt requests

echo "Dependencies installed."

# ============== 创建安装目录 ==============
echo "[2/5] Creating installation directories..."

mkdir -p "$INSTALL_DIR"
mkdir -p "$FIRMWARE_DIR"

# 复制程序文件
cp "$SCRIPT_DIR"/ota_http_server.py "$INSTALL_DIR/"
cp "$SCRIPT_DIR"/mqtt_ota_client.py "$INSTALL_DIR/"
cp "$SCRIPT_DIR"/ec20_dial.sh "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR"/*.sh

echo "Files copied to $INSTALL_DIR"
echo "Firmware directory: $FIRMWARE_DIR"

# ============== 配置EC20拨号服务 ==============
echo "[3/5] Configuring EC20 dial service..."

cat > /etc/systemd/system/ec20-dial.service << EOF
[Unit]
Description=EC20 4G Dial Service
After=network.target

[Service]
Type=simple
ExecStart=$INSTALL_DIR/ec20_dial.sh start
ExecStop=$INSTALL_DIR/ec20_dial.sh stop
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable ec20-dial.service
echo "EC20 dial service configured."

# ============== 配置OTA网关服务 ==============
echo "[4/5] Configuring OTA gateway service..."

# 创建配置文件
cat > "$INSTALL_DIR/config.env" << EOF
# MQTT 配置（连接云端 EMQX）
MQTT_HOST=your-emqx-server.com
MQTT_PORT=1883
MQTT_USER=
MQTT_PASS=

# 设备配置
DEVICE_ID=esp32_device_001

# 网关 IP（ESP32 通过 WiFi 连接到此地址）
GATEWAY_IP=192.168.4.1

# HTTP 服务器端口
HTTP_PORT=8000

# 固件存储目录
FIRMWARE_DIR=$FIRMWARE_DIR
EOF

# 创建启动脚本
cat > "$INSTALL_DIR/start_gateway.sh" << 'STARTEOF'
#!/bin/bash
# 加载配置
source /opt/ota_gateway/config.env

# 启动 MQTT OTA 客户端（包含 HTTP 服务器）
python3 /opt/ota_gateway/mqtt_ota_client.py \
    --mqtt-host "$MQTT_HOST" \
    --mqtt-port "$MQTT_PORT" \
    ${MQTT_USER:+--mqtt-user "$MQTT_USER"} \
    ${MQTT_PASS:+--mqtt-pass "$MQTT_PASS"} \
    --device-id "$DEVICE_ID" \
    --gateway-ip "$GATEWAY_IP" \
    --http-port "$HTTP_PORT" \
    --firmware-dir "$FIRMWARE_DIR"
STARTEOF

chmod +x "$INSTALL_DIR/start_gateway.sh"

# 创建systemd服务
cat > /etc/systemd/system/$SERVICE_NAME.service << EOF
[Unit]
Description=OTA Gateway Service (HTTP Server + MQTT Client)
After=network.target ec20-dial.service
Wants=ec20-dial.service

[Service]
Type=simple
EnvironmentFile=$INSTALL_DIR/config.env
ExecStart=$INSTALL_DIR/start_gateway.sh
Restart=always
RestartSec=10
WorkingDirectory=$INSTALL_DIR

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable $SERVICE_NAME.service

echo "OTA gateway service configured."

# ============== 配置防火墙（如果有） ==============
echo "[5/5] Configuring firewall (if applicable)..."

# 开放 HTTP 端口
if command -v ufw &> /dev/null; then
    ufw allow 8000/tcp
    echo "UFW: Port 8000 opened"
elif command -v firewall-cmd &> /dev/null; then
    firewall-cmd --permanent --add-port=8000/tcp
    firewall-cmd --reload
    echo "firewalld: Port 8000 opened"
else
    echo "No firewall detected, skipping..."
fi

# ============== 完成 ==============
echo ""
echo "================================"
echo "Installation Complete!"
echo "================================"
echo ""
echo "Configuration file: $INSTALL_DIR/config.env"
echo "Firmware directory: $FIRMWARE_DIR"
echo ""
echo "Before starting, edit the config file:"
echo "  sudo nano $INSTALL_DIR/config.env"
echo ""
echo "Commands:"
echo "  Start gateway:   sudo systemctl start $SERVICE_NAME"
echo "  Stop gateway:    sudo systemctl stop $SERVICE_NAME"
echo "  View logs:       sudo journalctl -u $SERVICE_NAME -f"
echo "  Start EC20 4G:   sudo systemctl start ec20-dial"
echo ""
echo "HTTP Server URL: http://<GATEWAY_IP>:8000/"
echo "Firmware Info:   http://<GATEWAY_IP>:8000/info"
echo ""
echo "OTA Flow:"
echo "  1. Cloud sends firmware update notification via MQTT"
echo "  2. OrangePi downloads firmware from cloud"
echo "  3. OrangePi notifies ESP32 via MQTT"
echo "  4. ESP32 downloads firmware via HTTP from OrangePi"
echo ""
