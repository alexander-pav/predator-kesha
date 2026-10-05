#!/usr/bin/env python3

import sys
import socket
import struct
import os
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QTabWidget, QRadioButton, QComboBox, QSlider, QLabel,
    QPushButton, QCheckBox, QFormLayout, QGroupBox, QColorDialog
)
from PyQt6.QtCore import Qt, QSize
from PyQt6.QtGui import QIcon, QPixmap, QColor, QPainter
from PyQt6.QtSvg import QSvgRenderer

SOCKET_PATH = "/tmp/predator_kesha.sock"
CONFIG_PATH = "/etc/predator_kesha.conf"

EFFECTS = {
    "Static": 1, "Breathing": 2, "Neon": 3, "Wave": 4, "Ripple": 5, "Zoom": 6, "Snake": 7, "Disco": 8, "Shifting": 9, "Off": 0
}
DEVICES = {"Keyboard": 0, "Lid": 1}
DIRECTIONS = ["None", "Right", "Left"]


class PredatorKeshaGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Predator Kesha Control")
        self.setMinimumSize(450, 550)
        self.config = self.load_config()
        self.current_rgb = QColor(255, 255, 255)

        self.device_states = {
            "Keyboard": {
                "effect": self.config.get('kbd_eff', 0),
                "speed": self.config.get('kbd_spd', 0),
                "direction": self.config.get('kbd_dir', 0),
                "zone": self.config.get('kbd_zone', 0),
                "color": QColor(self.config.get('kbd_r', 255), self.config.get('kbd_g', 255), self.config.get('kbd_b', 255)),
                "brightness": self.config.get('kbd_bri', 100)
            },
            "Lid": {
                "effect": self.config.get('lid_eff', 0),
                "speed": self.config.get('lid_spd', 0),
                "direction": self.config.get('lid_dir', 0),
                "zone": self.config.get('lid_zone', 0),
                "color": QColor(self.config.get('lid_r', 255), self.config.get('lid_g', 255), self.config.get('lid_b', 255)),
                "brightness": self.config.get('lid_bri', 100)
            }
        }
        #self.set_icon_from_svg(SVG_ICON)
        icon = QIcon ("/usr/share/icons/hicolor/scalable/apps/predator-kesha.svg");
        self.setWindowIcon(icon)
        self.init_ui()

    def load_config(self):
        data = {}
        if not os.path.exists(CONFIG_PATH): return data
        try:
            with open(CONFIG_PATH, 'r') as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('#'): continue
                    if '=' in line:
                        key, val = line.split('=', 1)
                        try: data[key] = int(val)
                        except ValueError: data[key] = val
        except Exception as e: print(f"Config error: {e}")
        return data

    def send_command(self, cmd_type, args):
        padded_args = list(args) + [0] * (10 - len(args))
        packet = struct.pack('11i', cmd_type, *padded_args)
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.connect(SOCKET_PATH)
                client.sendall(packet)
        except Exception as e: print(f"Communication error: {e}")

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)
        self.tabs = QTabWidget()
        layout.addWidget(self.tabs)
        self.setup_power_tab()
        self.setup_rgb_tab()
        self.setup_battery_tab()

    def setup_power_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        group = QGroupBox("System Power Profile")
        group_layout = QVBoxLayout(group)
        modes = ["Eco+", "Eco", "Quiet", "Balanced", "Perf", "Turbo"]
        self.power_buttons = []
        current_mode = self.config.get('power_mode', 3)
        for i, name in enumerate(modes):
            rb = QRadioButton(name)
            if i == current_mode: rb.setChecked(True)
            rb.toggled.connect(self.on_power_toggled)
            group_layout.addWidget(rb)
            self.power_buttons.append(rb)
        layout.addWidget(group)
        layout.addStretch()
        self.tabs.addTab(tab, "Power")

    def on_power_toggled(self):
        for i, rb in enumerate(self.power_buttons):
            if rb.isChecked():
                self.send_command(1, [i])
                break

    def setup_rgb_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        basic_group = QGroupBox('Device Effect')
        form = QFormLayout(basic_group)
        self.combo_dev = QComboBox()
        self.combo_dev.addItems(list(DEVICES.keys()))
        self.combo_dev.currentIndexChanged.connect(self.on_device_changed)
        self.combo_eff = QComboBox()
        self.combo_eff.addItems(list(EFFECTS.keys()))
        self.combo_eff.currentIndexChanged.connect(self.on_effect_changed)
        form.addRow("Device:", self.combo_dev)
        form.addRow("Effect:", self.combo_eff)
        layout.addWidget(basic_group)
        self.color_group = QGroupBox("Color")
        color_layout = QVBoxLayout(self.color_group)
        self.color_preview = QPushButton()
        self.color_preview.setMinimumHeight(32)
        self.color_preview.setCursor(Qt.CursorShape.PointingHandCursor)
        #self.color_preview.setToolTip("Click to pick a color")
        self.color_preview.clicked.connect(self.pick_color)
        color_layout.addWidget(self.color_preview)
        layout.addWidget(self.color_group)

        self.motion_group = QGroupBox("Motion Settings")
        motion_layout = QFormLayout(self.motion_group)
        speed_layout = QHBoxLayout()
        self.slider_speed = QSlider(Qt.Orientation.Horizontal)
        self.slider_speed.setRange(0, 9)
        #self.label_speed = QLabel()
        self.label_speed = QLabel(str(self.slider_speed.value()))
        self.label_speed.setFixedWidth(20)
        self.label_speed.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        speed_layout.addWidget(self.slider_speed)
        speed_layout.addWidget(self.label_speed)
        self.slider_speed.valueChanged.connect(self.update_speed_labels)
        self.slider_speed.valueChanged.connect(self.apply_rgb)
        self.combo_dir = QComboBox()
        self.combo_dir.addItems(DIRECTIONS)
        self.combo_dir.currentIndexChanged.connect(self.apply_rgb)
        motion_layout.addRow("Speed:", speed_layout)
        motion_layout.addRow("Direction:", self.combo_dir)
        layout.addWidget(self.motion_group)

        self.bright_group = QGroupBox("Brightness")
        bright_layout = QHBoxLayout(self.bright_group)
        self.slider_bri = QSlider(Qt.Orientation.Horizontal)
        self.slider_bri.setRange(0, 100)
        self.label_bri = QLabel("100%")
        self.label_bri.setFixedWidth(80)
        self.label_bri.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self.slider_bri.valueChanged.connect(self.update_brightness_labels)
        self.slider_bri.valueChanged.connect(self.apply_rgb)
        bright_layout.addWidget(self.slider_bri)
        bright_layout.addWidget(self.label_bri)
        layout.addWidget(self.bright_group)

        self.timeout_group = QGroupBox(f"Lighting Timeout")
        timeout_layout = QHBoxLayout(self.timeout_group)
        self.slider_timeout = QSlider(Qt.Orientation.Horizontal)
        self.slider_timeout.setRange(0, 120)
        t_val = self.config.get('timeout', 0)
        self.slider_timeout.setValue(t_val)

        self.label_timeout = QLabel("Always On" if t_val == 0 else f"{t_val} sec")
        self.label_timeout.setFixedWidth(80)
        self.label_timeout.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)

        self.slider_timeout.valueChanged.connect(self.update_timeout_labels)
        self.slider_timeout.valueChanged.connect(self.apply_timeout)

        timeout_layout.addWidget(self.slider_timeout)
        timeout_layout.addWidget(self.label_timeout)
        layout.addWidget(self.timeout_group)
        self.update_timeout_labels()

        layout.addStretch()

        self.on_device_changed()
        self.tabs.addTab(tab, "RGB")

    def on_device_changed(self):
        dev_name = self.combo_dev.currentText()
        state = self.device_states[dev_name]
        self.combo_eff.blockSignals(True)
        self.slider_speed.blockSignals(True)
        self.combo_dir.blockSignals(True)
        self.slider_bri.blockSignals(True)
        self.current_rgb = state['color']
        self.update_color_preview()
        eff_list = list(EFFECTS.keys())
        for i, name in enumerate(eff_list):
            if EFFECTS[name] == state['effect']:
                self.combo_eff.setCurrentIndex(i)
                break
        self.slider_speed.setValue(state['speed'])
        self.combo_dir.setCurrentIndex(state['direction'])
        self.slider_bri.setValue(state['brightness'])
        self.combo_eff.blockSignals(False)
        self.slider_speed.blockSignals(False)
        self.combo_dir.blockSignals(False)
        self.slider_bri.blockSignals(False)
        self.update_brightness_labels()
        self.update_speed_labels()

        self.on_effect_changed()

    def on_effect_changed(self):
        is_static = (self.combo_eff.currentText() == "Static")
        is_off = (self.combo_eff.currentText() == "Off")
        self.color_group.setVisible(is_static)
        self.motion_group.setVisible(not is_static and not is_off)
        self.bright_group.setVisible(not is_off)
        self.timeout_group.setVisible(not is_off)
        self.apply_rgb()

    def update_color_preview(self):
        self.color_preview.setStyleSheet(
            f"background-color: {self.current_rgb.name()}; "
            f"border: 1px solid #555; border-radius: 4px;"
        )

    def pick_color(self):
        color = QColorDialog.getColor(self.current_rgb, self)
        if color.isValid():
            self.current_rgb = color
            self.update_color_preview()
            self.apply_rgb()

    def apply_rgb(self):
        dev_name = self.combo_dev.currentText()
        state = self.device_states[dev_name]
        state['effect'] = EFFECTS[self.combo_eff.currentText()]
        state['speed'] = self.slider_speed.value()
        state['direction'] = self.combo_dir.currentIndex()
        state['brightness'] = self.slider_bri.value()
        state['color'] = self.current_rgb
        dev_idx = DEVICES[dev_name]
        eff_idx = state['effect']
        #if self.check_enable_rgb.isChecked():
        r, g, b = self.current_rgb.red(), self.current_rgb.green(), self.current_rgb.blue()
        #else:
        #    r, g, b = self.current_rgb.red(), self.current_rgb.green(), self.current_rgb.blue()
        #    eff_idx = 0;
        #    #r, g, b = 0, 0, 0
        speed, direction = (0, 0) if eff_idx == 1 else (state['speed'], state['direction'])
        args = [dev_idx, eff_idx, speed, direction, 0, r, g, b, state['brightness']]
        print(args)
        self.send_command(2, args)

    def setup_battery_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        self.check_batt = QCheckBox("Enable Battery Limit")
        self.check_batt.setChecked(bool(self.config.get('battery_status', 1)))
        self.check_batt.toggled.connect(self.apply_battery)
        layout.addWidget(self.check_batt)
        form_container = QVBoxLayout()
        def create_slider_row(label_text, default_val):
            row_widget = QWidget()
            row_layout = QHBoxLayout(row_widget)
            lbl = QLabel(f"{label_text} ({default_val}%):")
            lbl.setFixedWidth(120)
            sld = QSlider(Qt.Orientation.Horizontal)
            sld.setRange(0, 100)
            sld.setValue(default_val)
            row_layout.addWidget(lbl)
            row_layout.addWidget(sld)
            return sld, lbl, row_widget
        self.slider_upper, self.lbl_upper, widget_u = create_slider_row("Upper Limit", self.config.get('battery_upper', 80))
        self.slider_lower, self.lbl_lower, widget_l = create_slider_row("Lower Limit", self.config.get('battery_lower', 75))
        form_container.addWidget(widget_u)
        form_container.addWidget(widget_l)
        layout.addLayout(form_container)
        self.slider_upper.valueChanged.connect(self.update_battery_labels)
        self.slider_upper.valueChanged.connect(self.apply_battery)
        self.slider_lower.valueChanged.connect(self.update_battery_labels)
        self.slider_lower.valueChanged.connect(self.apply_battery)
        layout.addStretch()
        self.tabs.addTab(tab, "Battery")

    def update_battery_labels(self):
        self.lbl_upper.setText(f"Upper Limit ({self.slider_upper.value()}%):")
        self.lbl_lower.setText(f"Lower Limit ({self.slider_lower.value()}%):")

    def apply_battery(self):
        self.send_command(3, [int(self.check_batt.isChecked()), self.slider_upper.value(), self.slider_lower.value()])


    def apply_timeout(self):
        self.send_command(4, [self.slider_timeout.value()])

    def update_brightness_labels(self):
        self.label_bri.setText(f"{self.slider_bri.value()}%")

    def update_speed_labels(self):
        self.label_speed.setText(str(self.slider_speed.value()))

    def update_timeout_labels(self):
        val = self.slider_timeout.value()
        if val == 0:
            self.label_timeout.setText("Always On")
        else:
            self.label_timeout.setText(f"{val} sec")



def main():
    app = QApplication(sys.argv)
    window = PredatorKeshaGUI()
    window.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
