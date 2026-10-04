#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#include <libudev.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <errno.h>


#define KEYBOARD_RGB_ID  0x21
#define LID_RGB_ID       0x83
#define BUTTON_RGB_ID    0x65

#define EFFECT_OFF          0x01
#define EFFECT_STATIC       0x02
#define EFFECT_BREATHING    0x04
#define EFFECT_NEON         0x05
#define EFFECT_WAVE         0x07
#define EFFECT_RIPPLE       0x08
#define EFFECT_ZOOM         0x09
#define EFFECT_SNAKE        0x0a
#define EFFECT_DISCO        0x0b
#define EFFECT_SHIFTING     0xff



#define SOCKET_PATH "/tmp/predator_kesha.sock"
#define CONFIG_PATH "/etc/predator_kesha.conf"



#define CLR_RED     "\x1b[31m"
#define CLR_GREEN   "\x1b[32m"
#define CLR_YELLOW  "\x1b[33m"
#define CLR_RESET   "\x1b[0m"
#define CLR_BOLD    "\x1b[1m"

// clang ./predator_kesha3.c -o ./predator_kesha -ludev -lpthread

typedef struct {
    const char *model_name;
    uint16_t ec_vid;
    uint16_t ec_pid;
    uint16_t kb_vid;
    uint16_t kb_pid;
} SupportedModel;


// EC HID - CTRL, KB HID - RGB
// MODEL NAME,  EC VID PID,  KB VID PID
//Predator Helios Neo 16s
static const SupportedModel SUPPORTED_MODELS[] = {
    {"Acer 2025",  0x1025, 0x174b,  0x0cf2, 0x5130},
    {NULL, 0, 0, 0, 0} // Sentinel
};

typedef struct {
    uint16_t ec_vid;
    uint16_t ec_pid;
    uint16_t kb_vid;
    uint16_t kb_pid;

    int power_mode;

    int battery_status;
    int battery_upper;
    int battery_lower;

    int timeout_sec;
    int brightness;

    int kbd_eff; int kbd_spd; int kbd_dir; int kbd_zone;
    int kbd_r; int kbd_g; int kbd_b; int kbd_bri;

    int lid_eff; int lid_spd; int lid_dir; int lid_zone;
    int lid_r; int lid_g; int lid_b; int lid_bri;
} Config;



typedef enum {
    //CMD_EXIT = 0,
    CMD_POWER = 1,
    CMD_RGB = 2,
    CMD_BATTERY = 3,
    CMD_TIMEOUT = 4,
    CMD_SUSPEND = 5,
    CMD_WAKEUP = 6
} CommandType;

typedef struct {
    CommandType cmd_type; // 1:power, 2:rgb,  3:battery, 4:timeout
    int args[10];
} CommandPacket;


int g_ec_fd = -1;
int g_kb_fd = -1;
Config g_cfg;
bool g_suspended;


bool find_device_by_id(uint16_t vid, uint16_t pid, int *out_fd);
void list_hid_devices();

bool load_config();
void save_default_config();
void save_config();

bool hidraw_read(int fd, const uint8_t *data, size_t len);



bool update_button_color(int power_mode, int fd_kb);
bool get_power_mode(int *mode, int fd_ec);
bool apply_power_mode(int mode, int fd_ec);
bool apply_rgb(int dev_type, int effect_idx, int speed, int dir, int zone,
               int r, int g, int b, int brightness, int fd);
bool apply_battery_limit(int status, int upper, int lower, int fd);

bool get_keyboard_bri_timeout(int *brightness, int *timeout_sec, int fd);
bool set_keyboard_bri_timeout(int brightness, int timeout_sec, int fd);
void print_usage(const char *prog_name);
bool detect_and_configure_hardware();



bool find_device_by_id(uint16_t vid, uint16_t pid, int *out_fd) {
    *out_fd = -1; //  -1 in case nothing is found
    struct udev *udev = udev_new();
    if (!udev) return false;

    struct udev_enumerate *en = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(en, "hidraw");
    udev_enumerate_scan_devices(en);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(en);
    struct udev_list_entry *dev_list_entry;
    bool found = false;

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);
        const char *devnode = udev_device_get_devnode(dev);

        if (devnode) {
            int fd = open(devnode, O_RDWR | O_NONBLOCK);
            if (fd >= 0) {
                struct hidraw_devinfo info;
                if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0) {
                    if (info.vendor == vid && info.product == pid) {
                        *out_fd = fd;
                        found = true;
                        udev_device_unref(dev);
                        break;
                    }
                }
                close(fd);
            }
        }
        udev_device_unref(dev);
    }
    udev_enumerate_unref(en);
    udev_unref(udev);
    return found;
}

void list_hid_devices() {
    if (geteuid() != 0) {
        fprintf(stderr, CLR_RED "[ERR] MUST BE RUN AS ROOT%s\n", CLR_RESET);
        return;
    }
    struct udev *udev = udev_new();
    struct udev_enumerate *en = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(en, "hidraw");
    udev_enumerate_scan_devices(en);
    struct udev_list_entry *devices = udev_enumerate_get_list_entry(en);
    struct udev_list_entry *dev_list_entry;

    printf("%-20s %-15s %s \n", "Device", "VID:PID", "Name");
    printf("------------------------------------------------------------\n");

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);
        const char *devnode = udev_device_get_devnode(dev);
        if (devnode) {
            int fd = open(devnode, O_RDWR);
            if (fd >= 0) {
                struct hidraw_devinfo info;
                char name[256] = {0};
                ioctl(fd, HIDIOCGRAWINFO, &info);
                ioctl(fd, HIDIOCGRAWNAME(256), name);
                printf("%-20s %04x:%04x     %s ", devnode, info.vendor, info.product, name);

                // Search KB_HID
                uint8_t report[11] = {0xa1};
                if (hidraw_read(fd, report, sizeof(report))) {
                    // resp: a1 03 65 21 83 00 00 00 00 00 00
                    int target_count = report[1];
                    if (target_count > 0 && target_count < 10) {
                        printf("<- FOUND ACER");
                        for (int i=0; i<target_count; i++) {
                            int target_id = report[2+i];

                            //printf("%02x \n" , report[2+i]);
                            if        (target_id == KEYBOARD_RGB_ID) {
                                printf(" KEYBOARD");
                            } else if (target_id == LID_RGB_ID) {
                                printf(" LID");
                            } else if (target_id == BUTTON_RGB_ID) {
                                printf(" BUTTON");
                            }
                        }
                        printf(" RGB HID ");
                    }

                }

                {
                    // GetHIDSpecVersion
                    uint8_t report[65] = {0xa0, 0x00, 0xa0, 0x00, 0x00, 0x00};
                    if (hidraw_read(fd, report, sizeof(report))) {
                        // resp: a0 00 e0 00 00 00 ff 00 06
                        int resp = report[2];
                        if (resp == 0xe0) {
                            printf("<- FOUND ACER EC HID ");
                        }
                        //for (int i=0;i<(int)sizeof(report);i++) {
                        //    fprintf(stderr, "%02x ", report[i]);
                        //}

                        //fprintf(stderr, "\n");
                    }
                }

                {
                    uint8_t report[65] = {0xa0, 0x00, 0xa0, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00};
                    if (hidraw_read(fd, report, sizeof(report))) {

                        for (int i=0;i<(int)sizeof(report);i++) {
                            fprintf(stderr, "%02x ", report[i]);
                        }

                        fprintf(stderr, "\n");
                    }
                }
                printf("\n");

                close(fd);
            }
        }
        udev_device_unref(dev);
    }
    udev_enumerate_unref(en);
    udev_unref(udev);
}

bool load_config() {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        perror("Failed to open config fiel");
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if      (strcmp(key, "ec_vid") == 0) g_cfg.ec_vid = strtol(val, NULL, 16);
            else if (strcmp(key, "ec_pid") == 0) g_cfg.ec_pid = strtol(val, NULL, 16);
            else if (strcmp(key, "kb_vid") == 0) g_cfg.kb_vid = strtol(val, NULL, 16);
            else if (strcmp(key, "kb_pid") == 0) g_cfg.kb_pid = strtol(val, NULL, 16);
            else if (strcmp(key, "power_mode") == 0) g_cfg.power_mode = atoi(val);
            else if (strcmp(key, "battery_status") == 0) g_cfg.battery_status = atoi(val);
            else if (strcmp(key, "battery_upper") == 0) g_cfg.battery_upper = atoi(val);
            else if (strcmp(key, "battery_lower") == 0) g_cfg.battery_lower = atoi(val);
            else if (strcmp(key, "timeout_sec") == 0) g_cfg.timeout_sec = atoi(val);
            else if (strcmp(key, "brightness") == 0) g_cfg.brightness = atoi(val);
            else if (strcmp(key, "kbd_eff") == 0) g_cfg.kbd_eff = atoi(val);
            else if (strcmp(key, "kbd_spd") == 0) g_cfg.kbd_spd = atoi(val);
            else if (strcmp(key, "kbd_dir") == 0) g_cfg.kbd_dir = atoi(val);
            else if (strcmp(key, "kbd_zone") == 0) g_cfg.kbd_zone = atoi(val);
            else if (strcmp(key, "kbd_r") == 0) g_cfg.kbd_r = atoi(val);
            else if (strcmp(key, "kbd_g") == 0) g_cfg.kbd_g = atoi(val);
            else if (strcmp(key, "kbd_b") == 0) g_cfg.kbd_b = atoi(val);
            else if (strcmp(key, "kbd_bri") == 0) g_cfg.kbd_bri = atoi(val);
            else if (strcmp(key, "lid_eff") == 0) g_cfg.lid_eff = atoi(val);
            else if (strcmp(key, "lid_spd") == 0) g_cfg.lid_spd = atoi(val);
            else if (strcmp(key, "lid_dir") == 0) g_cfg.lid_dir = atoi(val);
            else if (strcmp(key, "lid_zone") == 0) g_cfg.lid_zone = atoi(val);
            else if (strcmp(key, "lid_r") == 0) g_cfg.lid_r = atoi(val);
            else if (strcmp(key, "lid_g") == 0) g_cfg.lid_g = atoi(val);
            else if (strcmp(key, "lid_b") == 0) g_cfg.lid_b = atoi(val);
            else if (strcmp(key, "lid_bri") == 0) g_cfg.lid_bri = atoi(val);
        }
    }
    fclose(f);
    return true;
}

void save_default_config() {
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        perror("Failed to open config for writing");
        return;
    }
    fprintf(f, "# Predator Kesha Config\n");
    fprintf(f, "ec_vid=0000\nec_pid=0000\n");
    fprintf(f, "kb_vid=0000\nkb_pid=0000\n");
    fprintf(f, "power_mode=3\nbattery_status=1\nbattery_upper=100\nbattery_lower=100\n");
    fprintf(f, "kbd_eff=0\nkbd_spd=0\nkbd_dir=0\nkbd_zone=0\n");
    fprintf(f, "kbd_r=255\nkbd_g=255\nkbd_b=255\nkbd_bri=100\n");
    fprintf(f, "lid_eff=0\nlid_spd=0\nlid_dir=0\nlid_zone=0\n");
    fprintf(f, "lid_r=255\nlid_g=255\nlid_b=255\nlid_bri=100\n");
    fclose(f);
}

void save_config() {
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        perror("Failed to open config for writing");
        return;
    }
    fprintf(f, "# Predator Kesha Config\n");
    fprintf(f, "ec_vid=%04x\nec_pid=%04x\n", g_cfg.ec_vid, g_cfg.ec_pid);
    fprintf(f, "kb_vid=%04x\nkb_pid=%04x\n", g_cfg.kb_vid, g_cfg.kb_pid);
    fprintf(f, "power_mode=%d\nbattery_status=%d\nbattery_upper=%d\nbattery_lower=%d\n",
            g_cfg.power_mode, g_cfg.battery_status, g_cfg.battery_upper, g_cfg.battery_lower);
    fprintf(f, "timeout_sec=%d\n", g_cfg.timeout_sec);
    fprintf(f, "brightness=%d\n", g_cfg.brightness);
    fprintf(f, "kbd_eff=%d\nkbd_spd=%d\nkbd_dir=%d\nkbd_zone=%d\n",
            g_cfg.kbd_eff, g_cfg.kbd_spd, g_cfg.kbd_dir, g_cfg.kbd_zone);
    fprintf(f, "kbd_r=%d\nkbd_g=%d\nkbd_b=%d\nkbd_bri=%d\n",
            g_cfg.kbd_r, g_cfg.kbd_g, g_cfg.kbd_b, g_cfg.kbd_bri);
    fprintf(f, "lid_eff=%d\nlid_spd=%d\nlid_dir=%d\nlid_zone=%d\n",
            g_cfg.lid_eff, g_cfg.lid_spd, g_cfg.lid_dir, g_cfg.lid_zone);
    fprintf(f, "lid_r=%d\nlid_g=%d\nlid_b=%d\nlid_bri=%d\n",
            g_cfg.lid_r, g_cfg.lid_g, g_cfg.lid_b, g_cfg.lid_bri);
    fclose(f);
}


bool hidraw_write(int fd, const uint8_t *data, size_t len) {
    if (fd >= 0) {
        int ret = ioctl(fd, HIDIOCSFEATURE(len), (void *)data) == 0;
        if (ret < 0) {
            perror("Failed to write HIDRAW dev");
            return false;
        }
        return true;
    }
    return false;
}

bool hidraw_read(int fd, const uint8_t *data, size_t len) {
    if (fd >= 0) {

        if (hidraw_write(fd, data, len) == false) return false;

        int ret = ioctl(fd, HIDIOCGFEATURE(len), (void *)data) == 0;

        if (ret < 0) {
            perror("Failed to read HIDRAW dev");
            return false;
        }
        return true;
    }
    return false;
}


bool update_button_color(int power_mode, int fd_kb) {

    // Update button color based on new mode
    int br = 255, bg = 255, bb = 255;
    switch (power_mode) {
        case -1:  br=0;   bg=0;   bb=0;     break; // 0ff (sleep)
        case  0:  br=0;   bg=0;   bb=0;     break; // Eco+ (Black)
        case  1:  br=0;   bg=255; bb=0;     break; // Eco (Green)
        case  2:  br=255; bg=255; bb=255;   break; // Quiet (White)
        case  3:  br=0;   bg=175; bb=200;   break; // Balanced (Blue)
        case  4:  br=255; bg=255; bb=0;     break; // Perf (Yellow)
        case  5:  br=200; bg=0;   bb=255;   break; // Turbo (Pink)

    }

    return apply_rgb(2, 1, 5, 0, 0, br, bg, bb, 100, fd_kb);
}


bool get_power_mode(int *mode, int fd_ec) {

    uint8_t report[65] = {0xa0, 0x00, 0xa0, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00};

    if (hidraw_read(fd_ec, report, sizeof(report)) == false)
        return false;

    // a0 00 e0 01 00 04
    for (int i=0;i<(int)sizeof(report);i++) {
        //printf("%02x ", report[i]);
    }
    //printf("\n");

    if (report[2] != 0xe0) {
        fprintf(stderr, CLR_RED "[ERR] Unknown error %s\n", CLR_RESET);
        return false;
    }
    if (report[5] > 0x5) {
        // Unknown power mode
        fprintf(stderr, CLR_RED "[ERR] Unknown power mode recieved %s\n", CLR_RESET);
        return false;
    }

    switch (report[5]) {
        case 0x05: *mode = 0; break;  // Eco+
        case 0x04: *mode = 1; break;  // Eco
        case 0x03: *mode = 2; break;  // Quiet
        case 0x02: *mode = 3; break;  // Balanced
        case 0x01: *mode = 4; break;  // Perf
        case 0x00: *mode = 5; break;  // Turbo
    }
    return true;
}

bool apply_power_mode(int mode, int fd_ec) {
    //fprintf(stderr, "apply_power_mode mode:%d fd_ec:%d \n", mode, fd_ec);

    if (mode < 0 || mode > 5) return false;

    //(0:eco+, 1:eco, 2:quiet, 3:balanced, 4:perf, 5:turbo)
    uint8_t mode_map[] = {0x05, 0x04, 0x03, 0x02, 0x01, 0x00};

    // TODO: uint8_t report[65] = {0xa0, 0x00, 0xa0, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00};

    uint8_t report[65] = {0};
    report[0] = 0xa0;
    report[1] = 0x00;
    report[2] = 0xa0;
    report[3] = 0x01;
    report[4] = 0x00;
    report[5] = 0x01;
    report[6] = mode_map[mode];

    return hidraw_write(fd_ec, report, sizeof(report));
}

bool apply_rgb(int dev_type, int effect_idx, int speed, int dir, int zone,
               int r, int g, int b, int brightness, int fd) {

    //fprintf(stderr, "apply_rgb dev_type:%d brightness:%d fd:%d\n", dev_type, brightness, fd);

    uint8_t device_id, zone_mask;
    if (dev_type == 0) { device_id = KEYBOARD_RGB_ID; zone_mask = 0x0F; }
    else if (dev_type == 1) { device_id = LID_RGB_ID; zone_mask = 0x00; }
    else { device_id = BUTTON_RGB_ID; zone_mask = 0x00; }

    uint8_t effects[] = {EFFECT_OFF, EFFECT_STATIC, EFFECT_BREATHING, EFFECT_NEON, EFFECT_WAVE,
        EFFECT_RIPPLE, EFFECT_ZOOM, EFFECT_SNAKE, EFFECT_DISCO, EFFECT_SHIFTING};

    if (dev_type == 0 && zone >= 1 && zone <= 4) {
        uint8_t zones[] = {0x01, 0x02, 0x04, 0x08};
        zone_mask = zones[zone-1];
    }

    //fprintf(stderr, "!!!%d  \n", effects[effect_idx]);
    uint8_t report[11] = {0xa4, device_id, effects[effect_idx], (uint8_t)brightness,
        (uint8_t)speed, (uint8_t)dir, (uint8_t)r, (uint8_t)g, (uint8_t)b, zone_mask, 0x00};
    //fprintf(stderr, CLR_RED "[ERR] apply_rgb [%d] [%d] [%d] [%d] %s\n", brightness, r, g, b, CLR_RESET);


    return hidraw_write(fd, report, sizeof(report));
}



bool apply_battery_limit(int status, int upper, int lower, int fd) {
    //fprintf(stderr, "apply_battery_limit upper:%d lower:%d\n", upper, lower);

    uint8_t report[65] = {0};
    report[0] = 0xa0;
    report[1] = 0x00;
    report[2] = 0xa0;
    report[3] = 0x03;
    report[4] = 0x0b;
    report[5] = 0x01;
    report[6] = 0x03;
    report[7] = (uint8_t)status;
    report[8] = (uint8_t)upper;
    report[9] = (uint8_t)lower;

    return hidraw_write(fd, report, sizeof(report));
}

bool get_keyboard_bri_timeout(int *brightness, int *timeout_sec, int fd) {

    uint8_t report[65] = {0xa0, 0x00, 0xa0, 0x0a, 0x00, 0x02, 0x02};

    if (hidraw_read(fd, report, sizeof(report)) == false)
        return false;

    // a0 00 e0 0a 00 00 02 01 00 64 00 0a 00
    for (int i=0;i<(int)sizeof(report);i++) {
        fprintf(stderr, "%02x ", report[i]);
    }
    fprintf(stderr, "\n");

    if (report[2] != 0xe0)
        return false;
    *brightness  = report[9];
    *timeout_sec = report[11];

    return true;
}


bool set_keyboard_bri_timeout(int brightness, int timeout_sec, int fd) {

    uint8_t report[65] = {0xa0, 0x00, 0xa0, 0x0a, 0x00, 0x01, 0x02,
        0x01,
        0x00, (uint8_t)brightness, // STUPID: Only these values are valid 0 25 50 75 100
        0x00, (uint8_t)timeout_sec
    };


    return hidraw_write(fd, report, sizeof(report));
}


// Sleep workaround, turn off rgb, set power mode to ECO+
void suspend() {
    g_suspended = true;
    apply_rgb(0, 0, 0, 0, 0, 0, 0, 0, 0, g_kb_fd);
    apply_rgb(1, 0, 0, 0, 0, 0, 0, 0, 0, g_kb_fd);
    apply_rgb(2, 0, 0, 0, 0, 0, 0, 0, 0, g_kb_fd);
    //update_button_color(-1, g_kb_fd);
    apply_power_mode(0, g_ec_fd); // ECO+
    //get_keyboard_bri_timeout(0, 0, g_ec_fd);
}

// Sleep workaround, turn off rgb, set power mode to ECO+
void wakeup() {
    apply_rgb(0, g_cfg.kbd_eff, g_cfg.kbd_spd, g_cfg.kbd_dir, g_cfg.kbd_zone,
              g_cfg.kbd_r, g_cfg.kbd_g, g_cfg.kbd_b, g_cfg.kbd_bri, g_kb_fd);
    apply_rgb(1, g_cfg.lid_eff, g_cfg.lid_spd, g_cfg.lid_dir, g_cfg.lid_zone,
              g_cfg.lid_r, g_cfg.lid_g, g_cfg.lid_b, g_cfg.lid_bri, g_kb_fd );

    if (apply_power_mode(g_cfg.power_mode, g_ec_fd)) {
        update_button_color(g_cfg.power_mode, g_kb_fd);
    }
    g_suspended = false;
}

void* button_monitor_thread(void* arg) {
    //printf("start button_monitor_thread:\n");
    int sleep_time = 50000; //50 ms
    int since_checked = 0;

    (void)arg;
    unsigned char buf[64];
    while (1) {
        if (g_suspended) {
            sleep(1);
            continue;
        }
        if (g_ec_fd >= 0 && g_kb_fd >= 0) {
            memset(buf, 0, sizeof(buf));
            if (read(g_ec_fd, buf, sizeof(buf)) > 0) {
                // TODO:
                //Detected key: 04 7d ff lights down
                //Detected key: 04 7c ff lights up
                printf("Detected key: %02x %02x %02x \n", buf[0], buf[1], buf[2]);

                if (buf[0] == 0x04 && buf[1] == 0x85 && buf[2] == 0xff) {
                    printf("Mode button was pressed \n");

                    int next_mode = (g_cfg.power_mode + 1) % 6; //???
                    if (apply_power_mode(next_mode, g_ec_fd)) {
                        update_button_color(next_mode, g_kb_fd);
                        g_cfg.power_mode = next_mode;
                        save_config();
                    }
                }
            } else {

                // Check current values every 5 sec
                if (since_checked >= 5*1000000) {
                    int current_mode = 0;
                    if (get_power_mode(&current_mode, g_ec_fd)) {
                        if (g_cfg.power_mode != current_mode) {
                            printf("Power mode was changed [%d] -> [%d] \n", g_cfg.power_mode, current_mode);
                            update_button_color(current_mode, g_kb_fd);
                            g_cfg.power_mode = current_mode;
                            save_config();
                        }
                    }
                    since_checked = 0;
                }
            }
        }
        usleep(sleep_time);
        since_checked += sleep_time;
    }
    return NULL;
}


void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);
    signal(SIGCHLD, SIG_IGN);
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    umask(0);
    chdir("/");
    for (int x = sysconf(_SC_OPEN_MAX); x >= 0; x--) close(x);
}

bool detect_and_configure_hardware() {
    int tmp_ec = -1, tmp_kb = -1;

    // Check HIDRAWs from the config
    if (find_device_by_id(g_cfg.ec_vid, g_cfg.ec_pid, &tmp_ec) &&
        find_device_by_id(g_cfg.kb_vid, g_cfg.kb_pid, &tmp_kb)) {
        if (tmp_ec >= 0) close(tmp_ec);
        if (tmp_kb >= 0) close(tmp_kb);
        return true;
    }

    // Updating HIDRAW devs
    if (tmp_ec >= 0) close(tmp_ec);
    if (tmp_kb >= 0) close(tmp_kb);

    // Check system's HIDRAWs is supported
    for (int i = 0; SUPPORTED_MODELS[i].model_name != NULL; i++) {
        int m_ec = -1, m_kb = -1;
        if (find_device_by_id(SUPPORTED_MODELS[i].ec_vid, SUPPORTED_MODELS[i].ec_pid, &m_ec) &&
            find_device_by_id(SUPPORTED_MODELS[i].kb_vid, SUPPORTED_MODELS[i].kb_pid, &m_kb)) {

            g_cfg.ec_vid = SUPPORTED_MODELS[i].ec_vid;
            g_cfg.ec_pid = SUPPORTED_MODELS[i].ec_pid;
            g_cfg.kb_vid  = SUPPORTED_MODELS[i].kb_vid;
            g_cfg.kb_pid  = SUPPORTED_MODELS[i].kb_pid;

            save_config();
            printf(CLR_GREEN "Detected model: %s. Config updated.%s\n", SUPPORTED_MODELS[i].model_name, CLR_RESET);

            if (m_ec >= 0) close(m_ec);
            if (m_kb >= 0) close(m_kb);
            return true;
        }
        if (m_ec >= 0) close(m_ec);
        if (m_kb >= 0) close(m_kb);
    }



    fprintf(stderr, CLR_RED "[ERR] Laptop is not supported.%s\n", CLR_RESET);
    fprintf(stderr, "Please use '--list' to find your VID:PID \n");

    return false;
}

void run_daemon() {
    //daemonize();
    // Wait HW init
    sleep(1);

    if (!load_config()) save_default_config();
    load_config();

    if (!detect_and_configure_hardware()) return;

    find_device_by_id(g_cfg.ec_vid, g_cfg.ec_pid, &g_ec_fd);
    find_device_by_id(g_cfg.kb_vid, g_cfg.kb_pid, &g_kb_fd);


    // APPLY CONFIG VALUES AT STARTUP
    if (apply_power_mode(g_cfg.power_mode, g_ec_fd) ) update_button_color(g_cfg.power_mode, g_kb_fd);


    apply_battery_limit(g_cfg.battery_status, g_cfg.battery_upper, g_cfg.battery_lower, g_ec_fd);
    //set_keyboard_bri_timeout(g_cfg.timeout, g_ec_fd);


    usleep(1000);
    apply_rgb(0, g_cfg.kbd_eff, g_cfg.kbd_spd, g_cfg.kbd_dir, g_cfg.kbd_zone,
              g_cfg.kbd_r, g_cfg.kbd_g, g_cfg.kbd_b, g_cfg.kbd_bri, g_kb_fd);
    apply_rgb(1, g_cfg.lid_eff, g_cfg.lid_spd, g_cfg.lid_dir, g_cfg.lid_zone,
              g_cfg.lid_r, g_cfg.lid_g, g_cfg.lid_b, g_cfg.lid_bri, g_kb_fd );



    pthread_t thread_id;
    pthread_create(&thread_id, NULL, button_monitor_thread, NULL);




    int server_fd, client_fd;
    struct sockaddr_un addr;
    unlink(SOCKET_PATH);
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) exit(EXIT_FAILURE);
    chmod(SOCKET_PATH, 0666);
    listen(server_fd, 5);

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;
        CommandPacket pkt;
        if (read(client_fd, &pkt, sizeof(pkt)) > 0) {
            //printf("pkt.cmd_type:%d\n", pkt.cmd_type);

            switch (pkt.cmd_type) {
                case CMD_POWER:
                    if (apply_power_mode(pkt.args[0], g_ec_fd)) {
                        update_button_color(pkt.args[0], g_kb_fd);
                        g_cfg.power_mode = pkt.args[0];
                        save_config();
                    }
                    break;
                case CMD_RGB:
                    // TODO: better naming
                    apply_rgb(pkt.args[0], pkt.args[1], pkt.args[2], pkt.args[3],
                                pkt.args[4], pkt.args[5], pkt.args[6], pkt.args[7],
                                pkt.args[8], g_kb_fd);
                    if (pkt.args[0] == 0) {
                        g_cfg.kbd_eff = pkt.args[1]; g_cfg.kbd_spd = pkt.args[2];
                        g_cfg.kbd_dir = pkt.args[3]; g_cfg.kbd_zone = pkt.args[4];
                        g_cfg.kbd_r = pkt.args[5];   g_cfg.kbd_g = pkt.args[6];
                        g_cfg.kbd_b = pkt.args[7];   g_cfg.kbd_bri = pkt.args[8];
                    } else if (pkt.args[0] == 1) {
                        g_cfg.lid_eff = pkt.args[1]; g_cfg.lid_spd = pkt.args[2];
                        g_cfg.lid_dir = pkt.args[3]; g_cfg.lid_zone = pkt.args[4];
                        g_cfg.lid_r = pkt.args[5];   g_cfg.lid_g = pkt.args[6];
                        g_cfg.lid_b = pkt.args[7];   g_cfg.lid_bri = pkt.args[8];
                    }
                    save_config();
                    break;
                case CMD_BATTERY:
                    apply_battery_limit(pkt.args[0], pkt.args[1], pkt.args[2], g_ec_fd);
                    g_cfg.battery_status = pkt.args[0];
                    g_cfg.battery_upper = pkt.args[1];
                    g_cfg.battery_lower = pkt.args[2];
                    save_config();
                    break;
                case CMD_TIMEOUT:
                    printf(" %d %d \n", g_cfg.kbd_bri, pkt.args[0]);
                    set_keyboard_bri_timeout(g_cfg.kbd_bri, pkt.args[0], g_ec_fd);
                    g_cfg.timeout_sec = pkt.args[0];
                    save_config();
                    break;
                case CMD_SUSPEND:
                    suspend();
                    break;
                case CMD_WAKEUP:
                    wakeup();
                    break;
            }
        }
        close(client_fd);
    }
}


void print_usage(const char *prog_name) {
    printf(CLR_BOLD "Usage:%s\n\n", prog_name);
    printf("  --daemon               Run as background service\n");
    printf("  --status               Show current configuration and hardware state\n");
    printf("  --list                 List all HIDRAW devices\n\n");
    printf("  Power Mode:\n");
    printf("    --power <0-5>  (0:eco+, 1:eco, 2:quiet, 3:balanced, 4:perf, 5:turbo)\n\n");
    printf("  RGB Control:\n");
    printf("    --rgb <dev> <eff> <spd> <dir> <zone> <r> <g> <b> <bri>\n");
    printf("      dev: 0:kbd, 1:lid, 2:btn\n");
    printf("      eff: off:0, 1:static, 2:breath, 3:neon, 4:wave, 5:ripple, 6:zoom, 7:snake, 8:disco, 9:shift\n");
    printf("      spd: 0-9 \n");
    printf("      dir: 0:none, 1:right, 2:left \n");
    printf("      zone: 0:all, 1-4:kbd zones\n");
    printf("      r,g,b: 0-255\n");
    printf("      bri: 0-100\n\n");
    printf("  Battery Limit:\n");
    printf("    --battery <status> <upper> <lower>\n");
    printf("      status: 0:off, 1:on\n");
    printf("      upper/lower: 0-100\n\n");
    printf("  Keyboard Lighting Timeout:\n");
    printf("    --timeout <seconds>\n");
    printf("      seconds: number of seconds before lighting turns off, 0: lighting always on\n\n");
    printf("  Sleep workaround (Turn off RGB, set power mode to ECO+):\n");
    printf("    --suspend \n");
    printf("    --wakeup \n\n");
    printf("%s\n", CLR_RESET);

}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"daemon",  no_argument,       0, 'd'},
        {"list",    no_argument,       0, 'l'},
        {"power",   required_argument, 0, 'p'},
        {"rgb",     required_argument, 0, 'r'},
        {"battery", required_argument, 0, 'b'},
        {"timeout", required_argument, 0, 't'},
        {"suspend", no_argument,       0, 's'},
        {"wakeup",  no_argument,       0, 'w'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int daemon_mode = 0;
    bool cmd_sent = false;
    CommandPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    while ((opt = getopt_long(argc, argv, "dl:p:r:b:t:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd': daemon_mode = 1; break;
            case 'l': list_hid_devices(); return 0;
            case 'p': {
                pkt.cmd_type = CMD_POWER;
                pkt.args[0] = atoi(optarg);
                cmd_sent = true; break;
            }
            case 'r': {
                pkt.cmd_type = CMD_RGB;
                pkt.args[0] = atoi(optarg);
                if (argc - optind < 8) {
                    fprintf(stderr, CLR_RED "Error: --rgb requires 9 params %s\n", CLR_RESET);
                    return EXIT_FAILURE;
                }
                for (int i = 1; i < 9; i++) {
                    pkt.args[i] = atoi(argv[optind++]);
                }
                cmd_sent = true; break;
            }
            case 'b': {
                pkt.cmd_type = CMD_BATTERY;

                pkt.args[0] = atoi(optarg);
                if (argc - optind < 2) {
                    fprintf(stderr, CLR_RED "Error: --battery requires 3 params %s\n", CLR_RESET);
                    return EXIT_FAILURE;
                }
                for (int i = 1; i < 3; i++) {
                    pkt.args[i] = atoi(argv[optind++]);
                }
                //fprintf(stderr, "pkt.args[0] [%d] pkt.args[0] [%d] pkt.args[0] [%d] \n", pkt.args[0], pkt.args[1], pkt.args[2]);

                cmd_sent = true; break;


            }
            case 't': {
                pkt.cmd_type = CMD_TIMEOUT;
                pkt.args[0] = atoi(optarg);
                cmd_sent = true; break;
            }
            case 's': {
                pkt.cmd_type = CMD_SUSPEND;
                cmd_sent = true; break;
            }
            case 'w': {
                pkt.cmd_type = CMD_WAKEUP;
                cmd_sent = true; break;
            }
            case 'h':
            default: print_usage(argv[0]); return 0;
        }
    }

    if (daemon_mode) {
        if (geteuid() != 0) {
            fprintf(stderr, CLR_RED "[ERR] DAEMON MUST BE RUN AS ROOT%s\n", CLR_RESET);
            return EXIT_FAILURE;
        }
        run_daemon();
        return 0;
    }

    if (cmd_sent) {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            send(sock, &pkt, sizeof(pkt), 0);
            fprintf(stderr, CLR_GREEN "Command sent to daemon successfully.%s\n", CLR_RESET);
            close(sock);
        } else {
            fprintf(stderr, CLR_YELLOW "Daemon not running. Start with --daemon ...%s\n", CLR_RESET);
            //return 1;


            fprintf(stderr, CLR_YELLOW "Daemon not running. Switching to Direct Mode...%s\n", CLR_RESET);

            if (geteuid() != 0) {
                fprintf(stderr, CLR_RED "[ERR] DIRECT MODE REQUIRES ROOT%s\n", CLR_RESET);
                return EXIT_FAILURE;
            }

            load_config();

            if (!detect_and_configure_hardware()) return 1;


            int fd_ec = -1, fd_kb = -1;
            find_device_by_id(g_cfg.ec_vid, g_cfg.ec_pid, &fd_ec);
            find_device_by_id(g_cfg.kb_vid, g_cfg.kb_pid, &fd_kb);


            if (pkt.cmd_type == CMD_POWER) {
                if (apply_power_mode(pkt.args[0], fd_ec)) {
                    update_button_color(pkt.args[0], fd_kb);
                    g_cfg.power_mode = pkt.args[0];
                }
            }
            else if (pkt.cmd_type == CMD_RGB) {
                apply_rgb(pkt.args[0], pkt.args[1], pkt.args[2], pkt.args[3],
                            pkt.args[4], pkt.args[5], pkt.args[6], pkt.args[7],
                            pkt.args[8], fd_kb);
                if (pkt.args[0] == 0) {
                    g_cfg.kbd_eff = pkt.args[1]; g_cfg.kbd_spd = pkt.args[2];
                    g_cfg.kbd_dir = pkt.args[3]; g_cfg.kbd_zone = pkt.args[4];
                    g_cfg.kbd_r = pkt.args[5];   g_cfg.kbd_g = pkt.args[6];
                    g_cfg.kbd_b = pkt.args[7];   g_cfg.kbd_bri = pkt.args[8];
                } else if (pkt.args[0] == 1) {
                    g_cfg.lid_eff = pkt.args[1]; g_cfg.lid_spd = pkt.args[2];
                    g_cfg.lid_dir = pkt.args[3]; g_cfg.lid_zone = pkt.args[4];
                    g_cfg.lid_r = pkt.args[5];   g_cfg.lid_g = pkt.args[6];
                    g_cfg.lid_b = pkt.args[7];   g_cfg.lid_bri = pkt.args[8];
                }
            }
            else if (pkt.cmd_type == CMD_BATTERY) {
                apply_battery_limit(pkt.args[0], pkt.args[1], pkt.args[2], fd_ec);
                g_cfg.battery_status = pkt.args[0];
                g_cfg.battery_upper = pkt.args[1];
                g_cfg.battery_lower = pkt.args[2];
            }
            else if (pkt.cmd_type == CMD_TIMEOUT) {
                set_keyboard_bri_timeout(100, pkt.args[0], fd_ec);
                g_cfg.timeout_sec = pkt.args[0];
            }

            save_config();

            if (fd_ec >= 0) close(fd_ec);
            if (fd_kb >= 0) close(fd_kb);


        }
    } else if (argc == 1) {
        print_usage(argv[0]);
    }

    return 0;
}
