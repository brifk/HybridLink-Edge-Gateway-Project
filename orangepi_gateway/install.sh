#!/bin/bash
#
# OrangePi OTA网关安装脚本
#

set -e

echo "================================"
echo "OrangePi OTA Gateway Installer"
echo "================================"

# 检查是否为root用户
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/opt/ota_gateway"
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
pip3 install \
    pyserial \
    paho-mqtt

echo "Dependencies installed."

# ============== 创建安装目录 ==============
echo "[2/5] Creating installation directory..."

mkdir -p "$INSTALL_DIR"
cp "$SCRIPT_DIR"/*.py "$INSTALL_DIR/"
cp "$SCRIPT_DIR"/ec20_dial.sh "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR"/*.sh

echo "Files copied to $INSTALL_DIR"

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
# MQTT配置
MQTT_HOST=your-emqx-server.com
MQTT_PORT=1883
MQTT_USER=
MQTT_PASS=

# 设备配置
DEVICE_ID=esp32_device_001

# 串口配置
SERIAL_PORT=/dev/ttyUSB0
SERIAL_BAUD=921600
EOF

# 创建systemd服务
cat > /etc/systemd/system/$SERVICE_NAME.service << EOF
[Unit]
Description=OTA Gateway Service
After=network.target ec20-dial.service
Wants=ec20-dial.service

[Service]
Type=simple
EnvironmentFile=$INSTALL_DIR/config.env
WorkingDirectory=$INSTALL_DIR
ExecStart=/usr/bin/python3 $INSTALL_DIR/mqtt_ota_client.py \
    --mqtt-host \$MQTT_HOST \
    --mqtt-port \$MQTT_PORT \
    --device-id \$DEVICE_ID \
    --serial-port \$SERIAL_PORT \
    --serial-baud \$SERIAL_BAUD
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable $SERVICE_NAME.service
echo "OTA gateway service configured."

# ============== 配置串口权限 ==============
echo "[5/5] Configuring serial port permissions..."

# 添加当前用户到dialout组
usermod -a -G dialout $SUDO_USER 2>/dev/null || true

# 创建udev规则
cat > /etc/udev/rules.d/99-ota-serial.rules << 'EOF'
# ESP32串口
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", SYMLINK+="esp32_ota", MODE="0666"
# EC20 4G模块
SUBSYSTEM=="tty", ATTRS{idVendor}=="2c7c", ATTRS{idProduct}=="0125", MODE="0666"
EOF

udevadm control --reload-rules
echo "Serial port permissions configured."

# ============== 完成 ==============
echo ""
echo "================================"
echo "Installation completed!"
echo "================================"
echo ""
echo "Next steps:"
echo "1. Edit configuration: nano $INSTALL_DIR/config.env"
echo "2. Configure your MQTT server address and credentials"
echo "3. Configure your APN in ec20_dial.sh if needed"
echo "4. Start services:"
echo "   sudo systemctl start ec20-dial"
echo "   sudo systemctl start $SERVICE_NAME"
echo ""
echo "View logs:"
echo "   sudo journalctl -u $SERVICE_NAME -f"
echo "   sudo journalctl -u ec20-dial -f"
echo ""
