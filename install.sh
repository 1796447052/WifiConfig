#!/bin/bash
# ============================================================
# BLE WiFi Provisioning - Installation Script
# 
# 目标平台: Orange Pi Zero2 (Debian Linux, ARM64)
# 用法: sudo ./install.sh [--uninstall] [--skip-build]
#
# 功能:
#   - 安装所有依赖
#   - 编译 Linux 端程序
#   - 安装 systemd 服务
#   - 配置开机自启
# ============================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 默认配置
PREFIX="/usr/local"
SYSTEMD_DIR="/etc/systemd/system"
SERVICE_NAME="ble_provision"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX_DIR="${SCRIPT_DIR}/linux"

# 解析命令行参数
UNINSTALL=false
SKIP_BUILD=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --uninstall|-u)
            UNINSTALL=true
            shift
            ;;
        --skip-build|-s)
            SKIP_BUILD=true
            shift
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "用法: sudo $0 [选项]"
            echo ""
            echo "选项:"
            echo "  --uninstall, -u    卸载程序和服务"
            echo "  --skip-build, -s   跳过编译步骤"
            echo "  --verbose, -v      显示详细输出"
            echo "  --help, -h         显示帮助信息"
            exit 0
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            exit 1
            ;;
    esac
done

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# 检查是否以 root 运行
check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "此脚本需要 root 权限运行"
        echo "请使用: sudo $0 $@"
        exit 1
    fi
}

# 检测系统架构
detect_arch() {
    ARCH=$(uname -m)
    log_info "检测到系统架构: $ARCH"
}

# 安装依赖
install_dependencies() {
    log_step "安装依赖包..."
    
    apt-get update
    
    # 基础构建工具
    apt-get install -y \
        build-essential \
        pkg-config
    
    # BLE 和 WiFi 相关依赖
    apt-get install -y \
        libglib2.0-dev \
        libdbus-1-dev \
        bluez \
        wpasupplicant \
        wireless-tools \
        iw
    
    # 可选但推荐的工具
    apt-get install -y \
        rfkill \
        dbus \
        || log_warn "部分可选工具安装失败，继续..."
    
    log_info "依赖安装完成"
}

# 编译程序
build_program() {
    if [[ "$SKIP_BUILD" == "true" ]]; then
        log_warn "跳过编译步骤"
        return
    fi
    
    log_step "编译 BLE WiFi Provisioning..."
    
    cd "$LINUX_DIR"
    
    # 清理旧的编译文件
    make clean 2>/dev/null || true
    
    # 编译
    if make; then
        log_info "编译成功"
    else
        log_error "编译失败"
        exit 1
    fi
    
    cd "$SCRIPT_DIR"
}

# 安装程序和服务
install_program() {
    log_step "安装程序和服务..."
    
    # 安装可执行文件
    install -m 0755 "${LINUX_DIR}/${SERVICE_NAME}" "${PREFIX}/bin/${SERVICE_NAME}"
    log_info "已安装: ${PREFIX}/bin/${SERVICE_NAME}"
    
    # 安装 systemd 服务文件
    install -m 0644 "${LINUX_DIR}/${SERVICE_NAME}.service" "${SYSTEMD_DIR}/${SERVICE_NAME}.service"
    log_info "已安装: ${SYSTEMD_DIR}/${SERVICE_NAME}.service"
    
    # 重新加载 systemd
    systemctl daemon-reload
    
    # 启用服务（开机自启）
    systemctl enable ${SERVICE_NAME}.service
    log_info "已启用开机自启"
}

# 配置蓝牙
configure_bluetooth() {
    log_step "配置蓝牙服务..."
    
    # 确保 bluetooth 服务运行
    if systemctl is-active --quiet bluetooth; then
        log_info "Bluetooth 服务已运行"
    else
        systemctl start bluetooth
        log_info "已启动 Bluetooth 服务"
    fi
    
    # 确保 dbus 服务运行
    if systemctl is-active --quiet dbus; then
        log_info "D-Bus 服务已运行"
    else
        systemctl start dbus
        log_info "已启动 D-Bus 服务"
    fi
    
    # 解除可能的蓝牙阻塞
    rfkill unblock bluetooth 2>/dev/null || true
}

# 卸载程序
uninstall_program() {
    log_step "卸载程序和服务..."
    
    # 停止并禁用服务
    systemctl stop ${SERVICE_NAME}.service 2>/dev/null || true
    systemctl disable ${SERVICE_NAME}.service 2>/dev/null || true
    
    # 删除服务文件
    rm -f "${SYSTEMD_DIR}/${SERVICE_NAME}.service"
    
    # 删除可执行文件
    rm -f "${PREFIX}/bin/${SERVICE_NAME}"
    
    # 重新加载 systemd
    systemctl daemon-reload
    
    log_info "卸载完成"
}

# 显示状态
show_status() {
    echo ""
    echo "========================================"
    echo "  BLE WiFi Provisioning 安装完成"
    echo "========================================"
    echo ""
    echo "服务状态:"
    systemctl status ${SERVICE_NAME}.service --no-pager || true
    echo ""
    echo "使用命令:"
    echo "  启动服务:   sudo systemctl start ${SERVICE_NAME}"
    echo "  停止服务:   sudo systemctl stop ${SERVICE_NAME}"
    echo "  查看状态:   sudo systemctl status ${SERVICE_NAME}"
    echo "  查看日志:   journalctl -u ${SERVICE_NAME} -f"
    echo ""
    echo "手动运行:"
    echo "  sudo ${PREFIX}/bin/${SERVICE_NAME}"
    echo ""
}

# 主函数
main() {
    echo ""
    echo "========================================"
    echo "  BLE WiFi Provisioning 安装脚本"
    echo "========================================"
    echo ""
    
    check_root "$@"
    detect_arch
    
    if [[ "$UNINSTALL" == "true" ]]; then
        uninstall_program
        echo ""
        log_info "BLE WiFi Provisioning 已成功卸载"
        exit 0
    fi
    
    install_dependencies
    build_program
    install_program
    configure_bluetooth
    show_status
    
    log_info "安装完成！"
}

# 运行主函数
main "$@"
