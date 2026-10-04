TARGET      := predator-kesha
SRC         := predator_kesha.c
# make CC=clang
CC          ?= gcc
CFLAGS      := -Wall -Wextra -O2
LDFLAGS     := -ludev -lpthread

PREFIX      := /usr/local
BINDIR      := $(PREFIX)/bin
SYSTEMDDIR  := /etc/systemd/system

INSTALL_BIN := $(TARGET)
INSTALL_SVC  := predator-kesha.service
INSTALL_SOCK  := /tmp/predator_kesha.sock


.PHONY: all clean install uninstall help

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	@echo "Installing binary to $(BINDIR)..."
	install -m 0755 $(TARGET) $(BINDIR)/$(TARGET)

	@echo "Installing systemd service..."
	install -m 0644 $(INSTALL_SVC) $(SYSTEMDDIR)/$(INSTALL_SVC)

	@echo "Reloading systemd and enabling service..."
	systemctl daemon-reload
	systemctl enable predator-kesha.service
	systemctl start predator-kesha.service

	@echo "Installing GUI to $(BINDIR)..."
	install -m 0755 gui/predator_kesha_gui.py $(BINDIR)/predator-kesha-gui
	install -m 0755 gui/predator-kesha.svg /usr/share/icons/hicolor/scalable/apps/predator-kesha.svg
	install -m 0644 gui/predator-kesha.desktop /usr/share/applications/predator-kesha.desktop

	@echo "Installation complete."

uninstall:
	echo "Removing files..."
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(SYSTEMDDIR)/$(INSTALL_SVC)
	systemctl disable predator-kesha.service 2>/dev/null || true
	systemctl daemon-reload

	rm -f $(BINDIR)/predator-kesha-gui
	rm -f /usr/share/icons/hicolor/scalable/apps/predator-kesha.svg
	rm -f /usr/share/applications/predator-kesha.desktop

	@echo "==> Uninstallation complete."

help:
	@echo "Usage:"
	@echo "  make             Build the application (default: gcc)"
	@echo "  make CC=clang    Build using clang"
	@echo "  make install     Install binary, config, and service (requires sudo)"
	@echo "  make uninstall   Remove all installed files (requires sudo)"
	@echo "  make clean       Remove build artifacts"
