#!/bin/bash
# ============================================================
# BLE WiFi Provisioning - Quick Install Script
# 
# 从 GitHub Release 下载预编译的二进制文件并安装
# 用法: curl -fsSL https://raw.githubusercontent.com/1796447052/WifiConfig/master/quick-install.sh | sudo bash
# ============================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 配置
GITHUB_USER="1796447052"  # 替换为你的 GitHub 用户名
GITHUB_REPO="WifiConfig"
SERVICE_NAME="ble_provision"
PREFIX="/usr/local"
SYSTEMD_DIR="/etc/systemd/system"

# 日志函数
log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

# 检查 root 权限
if [[ $EUID -ne 0 ]]; then
    log_error "此脚本需要 root 权限运行"
    exit 1
fi

# 检测架构
ARCH=$(uname -m)
log_info "检测到系统架构: $ARCH"

if [[ "$ARCH" != "aarch64" && "$ARCH" != "arm64" ]]; then
    log_warn "此程序主要针对 ARM64 架构 (Orange Pi Zero2)"
    log_warn "当前架构: $ARCH"
    read -p "是否继续安装? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# 安装运行时依赖
log_step "安装运行时依赖..."
apt-get update
apt-get install -y \
    libglib2.0-0 \
    libdbus-1-3 \
    bluez \
    wpasupplicant \
    wireless-tools \
    iw \
    curl \
    rfkill

# 获取最新版本
log_step "获取最新版本..."
LATEST_RELEASE=$(curl -s "https://api.github.com/repos/${GITHUB_USER}/${GITHUB_REPO}/releases/latest" 2>/dev/null || echo "")

if [[ -z "$LATEST_RELEASE" ]]; then
    log_warn "无法获取最新版本，尝试下载最新构建..."
    DOWNLOAD_URL="https://github.com/${GITHUB_USER}/${GITHUB_REPO}/releases/latest/download/ble-provision-linux-arm64.tar.gz"
else
    # Try python3 first (handles both compact and spaced JSON), then fall back to grep/cut
    TAG=$(echo "$LATEST_RELEASE" | python3 -c "import sys,json; print(json.load(sys.stdin)['tag_name'])" 2>/dev/null)
    if [[ -z "$TAG" ]]; then
        TAG=$(echo "$LATEST_RELEASE" | grep -o '"tag_name" *: *"[^"]*"' | head -1 | cut -d'"' -f4)
    fi
    if [[ -n "$TAG" ]]; then
        log_info "最新版本: $TAG"
        DOWNLOAD_URL="https://github.com/${GITHUB_USER}/${GITHUB_REPO}/releases/download/${TAG}/ble-provision-linux-arm64.tar.gz"
    else
        log_warn "无法解析版本号，尝试下载最新构建..."
        DOWNLOAD_URL="https://github.com/${GITHUB_USER}/${GITHUB_REPO}/releases/latest/download/ble-provision-linux-arm64.tar.gz"
    fi
fi

# 下载
log_step "下载预编译文件..."
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

if curl -fsSL "$DOWNLOAD_URL" -o ble-provision.tar.gz; then
    log_info "下载完成"
else
    log_error "下载失败"
    log_error "请确保 GitHub Release 中存在预编译文件"
    exit 1
fi

# 解压
log_step "解压文件..."
tar -xzf ble-provision.tar.gz

# 安装
log_step "安装程序..."
install -m 0755 ${SERVICE_NAME} "${PREFIX}/bin/${SERVICE_NAME}"
install -m 0644 ${SERVICE_NAME}.service "${SYSTEMD_DIR}/${SERVICE_NAME}.service"

# 配置
log_step "配置服务..."
systemctl daemon-reload
systemctl enable ${SERVICE_NAME}.service

# 启动蓝牙
log_step "启动蓝牙服务..."
systemctl start bluetooth 2>/dev/null || true
rfkill unblock bluetooth 2>/dev/null || true

# 清理
rm -rf "$TEMP_DIR"

# 完成
echo ""
echo "========================================"
echo "  安装完成!"
echo "========================================"
echo ""
echo "启动服务:   sudo systemctl start ${SERVICE_NAME}"
echo "查看状态:   sudo systemctl status ${SERVICE_NAME}"
echo "查看日志:   journalctl -u ${SERVICE_NAME} -f"
echo ""

# 询问是否立即启动
read -p "是否立即启动服务? [Y/n] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    systemctl start ${SERVICE_NAME}
    systemctl status ${SERVICE_NAME} --no-pager
fi
