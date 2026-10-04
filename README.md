# Predator Kesha

Predator Kesha is a Linux control daemon, CLI and simple GUI for newer Acer Predator laptops. I originally wrote this for my own laptop because other tools didn't support the latest models.


<img width="340" height="437" alt="gui1" src="https://github.com/user-attachments/assets/cab287ac-d9b3-44ae-8ca5-84367df9779f" /><img width="340" height="437" alt="gui2" src="https://github.com/user-attachments/assets/d5d57509-d27d-4945-8bfc-ae4e66091056" />
<img width="1654" height="1084" alt="cli1" src="https://github.com/user-attachments/assets/525f7a30-9f2a-480f-9a67-ba1d70394a3c" />



Newer Acer laptops (mostly 2024+ models) use internal USB HID devices to manage RGB lighting and power profiles, replacing the traditional `acer-wmi` kernel module. This project communicates directly with these devices using `/dev/hidraw`. 

**The program runs entirely in user-space, which means you do not need to compile or install any custom kernel modules.**

## Features
* **Power Profiles:** Switch between Eco+, Eco, Quiet, Balanced, Performance, and Turbo modes.
* **RGB Control:** Change colors, effects, speed, and brightness for the keyboard and lid.
* **Battery Charge Limit:** Set upper and lower charging limits to protect your battery health.
* **Keyboard Timeout:** Configure how long the keyboard stays lit when idle.
* **Hardware Button Support:** The physical mode-switch button on the laptop is monitored and works as expected.
* **Sleep Workaround:** Automatically turns off RGB and switches to Eco+ mode when the system suspends.

## Supported Models
Currently tested on:
* **Acer Predator Helios Neo 16s (2025)** (EC: `1025:174b`, KB: `0cf2:5130`)

*Note: Acer likely uses the same HID controllers across multiple recent laptop models. If this tool works for your laptop out of the box, please open an Issue with your exact model name so I can add it to the supported list!*

## Dependencies
Make sure you have the following packages installed:
* **For the C daemon:** `make`, `gcc` (or `clang`), `libudev-dev`
* **For the Python GUI:** `python3`, `python3-pyqt6`

## Installation

The project includes a `Makefile` for easy installation.

1. **Build the daemon:**
```bash
make
```

Install the service and GUI:
This will install the binary, enable the systemd service, and add a GUI shortcut to your application menu.

```bash
sudo make install
```

2. **Run the application:**
You can now launch Predator Kesha from your app menu or run predator-kesha-gui in the terminal.

## Uninstall
To completely remove the program, service, and GUI:
```bash
sudo make uninstall
```


## Useful BIOS Tricks for Acer Predator Helios Neo 16s on Linux

While working on this project, I found a few useful hardware tricks for running Linux on modern Acer laptops:

1. **Accessing the Advanced BIOS Tab:**
Turn off the laptop. Hold Fn + Tab and press the Power button. Keep holding Fn + Tab until you enter the BIOS. A second "Advanced" tab will appear.
Warning: Do not change settings here unless you know what they do. You can brick your laptop.

2. **Hidden Options in Normal BIOS:**
Pressing Ctrl + S in the "Main" BIOS tab reveals hidden options (like VMD settings, which you usually need to change to install Linux on an NVMe drive).

3. **Ethernet stops working after suspend (r8169 bug):**
If your Ethernet adapter disappears after waking from sleep (dmesg shows Unable to change power state from D3cold to D0), you can fix it via the Advanced BIOS:

    Enter the Advanced BIOS (Fn+Tab+Power).

    Go to Advanced -> ACPI Table -> ACPI Settings.

    Change Native ASPM to Disabled.

4. **About acer_wmi:**
To keep standard ACPI features working alongside this program, make sure the acer_wmi kernel module is loaded with the predator_v4=1 parameter in your modprobe config.




