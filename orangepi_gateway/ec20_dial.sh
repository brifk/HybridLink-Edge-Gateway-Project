#!/bin/bash
#
# EC20 4G拨号脚本
# 功能：开机自动拨号、掉线重连
# 适用于：OrangePi + EC20 4G模块
#

# ============== 配置 ==============
DEVICE="/dev/ttyUSB2"           # EC20的AT命令端口(通常是USB2)
PPP_DEVICE="/dev/ttyUSB3"       # PPP拨号端口(通常是USB3)
APN="cmnet"                     # 运营商APN (中国移动: cmnet, 中国联通: 3gnet, 中国电信: ctnet)
DIAL_NUMBER="*99#"              # 拨号号码
USERNAME=""                     # PPP用户名(大多数情况留空)
PASSWORD=""                     # PPP密码(大多数情况留空)
LOG_FILE="/var/log/ec20_dial.log"
LOCK_FILE="/var/run/ec20_dial.lock"
CHECK_INTERVAL=30               # 检查间隔(秒)
MAX_RETRY=5                     # 最大重试次数
DNS1="8.8.8.8"
DNS2="114.114.114.114"

# ============== 日志函数 ==============
log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_FILE"
}

# ============== 发送AT命令 ==============
send_at_command() {
    local cmd="$1"
    local timeout="${2:-3}"
    
    # 发送AT命令并读取响应
    echo -e "${cmd}\r" > "$DEVICE"
    sleep "$timeout"
    cat "$DEVICE" 2>/dev/null | head -20
}

# ============== 检查模块是否就绪 ==============
check_module() {
    log "Checking EC20 module..."
    
    # 等待USB设备出现
    for i in {1..30}; do
        if [ -e "$DEVICE" ] && [ -e "$PPP_DEVICE" ]; then
            log "EC20 USB devices found"
            break
        fi
        sleep 1
    done
    
    if [ ! -e "$DEVICE" ]; then
        log "ERROR: Device $DEVICE not found"
        return 1
    fi
    
    # 配置串口
    stty -F "$DEVICE" 115200 cs8 -cstopb -parenb
    
    # 测试AT命令
    local response=$(send_at_command "AT" 2)
    if echo "$response" | grep -q "OK"; then
        log "Module responding to AT commands"
        return 0
    else
        log "ERROR: Module not responding"
        return 1
    fi
}

# ============== 检查SIM卡 ==============
check_sim() {
    log "Checking SIM card..."
    
    local response=$(send_at_command "AT+CPIN?" 3)
    if echo "$response" | grep -q "READY"; then
        log "SIM card ready"
        return 0
    else
        log "ERROR: SIM card not ready"
        return 1
    fi
}

# ============== 检查网络注册 ==============
check_network() {
    log "Checking network registration..."
    
    # 检查CS网络
    local creg=$(send_at_command "AT+CREG?" 3)
    # 检查PS网络
    local cgreg=$(send_at_command "AT+CGREG?" 3)
    
    # 判断是否注册成功 (1=本地, 5=漫游)
    if echo "$cgreg" | grep -qE "\+CGREG: [0-9],[1|5]"; then
        log "Network registered (PS domain)"
        return 0
    elif echo "$creg" | grep -qE "\+CREG: [0-9],[1|5]"; then
        log "Network registered (CS domain)"
        return 0
    else
        log "ERROR: Network not registered"
        return 1
    fi
}

# ============== 检查信号强度 ==============
check_signal() {
    log "Checking signal strength..."
    
    local response=$(send_at_command "AT+CSQ" 2)
    local rssi=$(echo "$response" | grep "+CSQ:" | sed 's/.*+CSQ: \([0-9]*\),.*/\1/')
    
    if [ -n "$rssi" ] && [ "$rssi" != "99" ] && [ "$rssi" -gt 5 ]; then
        log "Signal strength: $rssi (OK)"
        return 0
    else
        log "WARNING: Signal strength: $rssi (weak)"
        return 0  # 信号弱也尝试连接
    fi
}

# ============== 配置APN ==============
configure_apn() {
    log "Configuring APN: $APN"
    
    # 设置PDP上下文
    send_at_command "AT+CGDCONT=1,\"IP\",\"$APN\"" 2
    sleep 1
    
    return 0
}

# ============== 创建PPP配置 ==============
create_ppp_config() {
    log "Creating PPP configuration..."
    
    # 创建peer配置文件
    cat > /etc/ppp/peers/ec20 << EOF
$PPP_DEVICE
115200
connect '/usr/sbin/chat -v -f /etc/ppp/peers/ec20-chat'
noauth
defaultroute
usepeerdns
nodetach
debug
logfile /var/log/ppp-ec20.log
EOF

    # 创建chat脚本
    cat > /etc/ppp/peers/ec20-chat << EOF
ABORT 'BUSY'
ABORT 'NO CARRIER'
ABORT 'NO DIALTONE'
ABORT 'ERROR'
ABORT 'NO ANSWER'
TIMEOUT 30
'' AT
OK ATE0
OK ATH0
OK AT+CGDCONT=1,"IP","$APN"
OK ATD$DIAL_NUMBER
CONNECT ''
EOF

    chmod 600 /etc/ppp/peers/ec20
    chmod 600 /etc/ppp/peers/ec20-chat
}

# ============== 启动PPP连接 ==============
start_ppp() {
    log "Starting PPP connection..."
    
    # 先停止已有连接
    killall pppd 2>/dev/null
    sleep 2
    
    # 启动PPP
    pppd call ec20 &
    local ppp_pid=$!
    
    # 等待连接建立
    for i in {1..30}; do
        if ip link show ppp0 >/dev/null 2>&1; then
            sleep 2
            if ip addr show ppp0 | grep -q "inet "; then
                local ip=$(ip addr show ppp0 | grep "inet " | awk '{print $2}' | cut -d'/' -f1)
                log "PPP connected! IP: $ip"
                
                # 设置DNS
                echo "nameserver $DNS1" > /etc/resolv.conf
                echo "nameserver $DNS2" >> /etc/resolv.conf
                
                return 0
            fi
        fi
        sleep 1
    done
    
    log "ERROR: PPP connection timeout"
    killall pppd 2>/dev/null
    return 1
}

# ============== 检查连接状态 ==============
check_connection() {
    # 检查ppp0接口是否存在
    if ! ip link show ppp0 >/dev/null 2>&1; then
        return 1
    fi
    
    # ping测试
    if ping -I ppp0 -c 1 -W 5 8.8.8.8 >/dev/null 2>&1; then
        return 0
    fi
    
    return 1
}

# ============== 主连接函数 ==============
dial() {
    local retry=0
    
    while [ $retry -lt $MAX_RETRY ]; do
        log "Dial attempt $((retry + 1))/$MAX_RETRY"
        
        # 检查模块
        if ! check_module; then
            retry=$((retry + 1))
            sleep 5
            continue
        fi
        
        # 检查SIM卡
        if ! check_sim; then
            retry=$((retry + 1))
            sleep 5
            continue
        fi
        
        # 检查网络
        if ! check_network; then
            retry=$((retry + 1))
            sleep 5
            continue
        fi
        
        # 检查信号
        check_signal
        
        # 配置APN
        configure_apn
        
        # 启动PPP
        if start_ppp; then
            log "Dial successful!"
            return 0
        fi
        
        retry=$((retry + 1))
        sleep 5
    done
    
    log "ERROR: Dial failed after $MAX_RETRY attempts"
    return 1
}

# ============== 守护进程模式 ==============
daemon_mode() {
    log "Starting EC20 dial daemon..."
    
    # 创建PPP配置
    create_ppp_config
    
    # 初始连接
    dial
    
    # 监控循环
    while true; do
        sleep $CHECK_INTERVAL
        
        if ! check_connection; then
            log "Connection lost, reconnecting..."
            dial
        fi
    done
}

# ============== 主入口 ==============
case "$1" in
    start)
        if [ -f "$LOCK_FILE" ]; then
            log "Daemon already running"
            exit 1
        fi
        
        echo $$ > "$LOCK_FILE"
        trap "rm -f $LOCK_FILE" EXIT
        
        daemon_mode
        ;;
    
    stop)
        if [ -f "$LOCK_FILE" ]; then
            kill $(cat "$LOCK_FILE") 2>/dev/null
            rm -f "$LOCK_FILE"
        fi
        killall pppd 2>/dev/null
        log "Stopped"
        ;;
    
    restart)
        $0 stop
        sleep 2
        $0 start
        ;;
    
    status)
        if check_connection; then
            ip addr show ppp0
            echo "Status: Connected"
        else
            echo "Status: Disconnected"
        fi
        ;;
    
    dial)
        create_ppp_config
        dial
        ;;
    
    *)
        echo "Usage: $0 {start|stop|restart|status|dial}"
        exit 1
        ;;
esac
