# ============================================================
# BLE WiFi Provisioning - Makefile
#
# 目标平台: Orange Pi Zero2 (Debian Linux, ARM64)
# 依赖: libglib2.0-dev, libdbus-1-dev
#
# 安装依赖:
#   sudo apt-get install -y libglib2.0-dev libdbus-1-dev bluez
#
# 编译:
#   make
#
# 安装 systemd 服务:
#   sudo make install
#
# 卸载:
#   sudo make uninstall
# ============================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 \
          $(shell pkg-config --cflags glib-2.0 gio-2.0 gio-unix-2.0)
LDFLAGS = $(shell pkg-config --libs glib-2.0 gio-2.0 gio-unix-2.0) \
          -lpthread

SRCS    = main.c ble_server.c wifi_scan.c wifi_connect.c
OBJS    = $(SRCS:.c=.o)
TARGET  = ble_provision

PREFIX  = /usr/local
SYSTEMD = /etc/systemd/system

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "Build successful: $(TARGET)"
	@echo "Run with: sudo ./$(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -m 0755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	install -m 0644 ble_provision.service $(SYSTEMD)/ble_provision.service
	systemctl daemon-reload
	systemctl enable ble_provision.service
	@echo ""
	@echo "Installed. Start with: sudo systemctl start ble_provision"

uninstall:
	systemctl stop ble_provision.service 2>/dev/null || true
	systemctl disable ble_provision.service 2>/dev/null || true
	rm -f $(PREFIX)/bin/$(TARGET)
	rm -f $(SYSTEMD)/ble_provision.service
	systemctl daemon-reload

# 依赖关系
main.o:         main.c ble_server.h wifi_connect.h
ble_server.o:   ble_server.c ble_server.h wifi_scan.h wifi_connect.h
wifi_scan.o:    wifi_scan.c wifi_scan.h
wifi_connect.o: wifi_connect.c wifi_connect.h
