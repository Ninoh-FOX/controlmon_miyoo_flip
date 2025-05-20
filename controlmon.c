#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <ctype.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <SDL/SDL.h>

#include "cJSON.h"
#include <json-c/json.h>

#define BUTTON_MENU      BTN_MODE        // 316
#define BUTTON_SELECT    BTN_SELECT      // 314
#define BUTTON_START     BTN_START       // 315
#define BUTTON_L1        BTN_TL          // 310
#define BUTTON_R1        BTN_TR          // 311
#define BUTTON_A         BTN_SOUTH       // 304
#define BUTTON_B         BTN_EAST        // 305
#define BUTTON_X         BTN_NORTH       // 307
#define BUTTON_Y         BTN_WEST        // 308
#define BUTTON_L2        BTN_THUMBL      // 317
#define BUTTON_R2        BTN_THUMBR      // 318
// dpad
#define DPAD_AXIS_X      ABS_HAT0X // 16
#define DPAD_AXIS_Y      ABS_HAT0Y // 17
#define DPAD_LEFT   (ev1.type == EV_ABS && ev1.code == ABS_HAT0X && ev1.value == -1)
#define DPAD_RIGHT  (ev1.type == EV_ABS && ev1.code == ABS_HAT0X && ev1.value == 1)
#define DPAD_UP     (ev1.type == EV_ABS && ev1.code == ABS_HAT0Y && ev1.value == -1)
#define DPAD_DOWN   (ev1.type == EV_ABS && ev1.code == ABS_HAT0Y && ev1.value == 1)

#define BUTTON_VOLUMEUP      KEY_VOLUMEUP    // 115
#define BUTTON_VOLUMEDOWN    KEY_VOLUMEDOWN  // 114

#define BRIMAX      10
#define BRIMIN      0

#define RELEASED    0
#define PRESSED     1
#define REPEAT      2

static struct input_event ev1;
static int input_fd = 0;
static int volume_fd = 0;

char* load_file(char const* path) {
    char* buffer = 0;
    long length = 0;
    FILE * f = fopen(path, "rb");

    if (f) {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        buffer = (char*) malloc((length + 1) * sizeof(char));
        if (buffer) {
            fread(buffer, sizeof(char), length, f);
        }
        fclose(f);
        buffer[length] = '\0';
    }

    return buffer;
}

void modifyBrightness(int inc) {
	int brightness = 0;
    int lcd = 0;
    cJSON* request_json = NULL;
    cJSON* itemBrightness;
    cJSON* itemLcd;

    const char *settings_file = "/mnt/SDCARD/system.json";

	char *request_body = load_file(settings_file);
	request_json = cJSON_Parse(request_body);
	itemBrightness = cJSON_GetObjectItem(request_json, "brightness");
    itemLcd = cJSON_GetObjectItem(request_json, "lcd_frequency");

    brightness = inc;
    lcd = cJSON_GetNumberValue(itemLcd);
    if (brightness > BRIMAX) brightness = BRIMAX;
    if (brightness < BRIMIN) brightness = BRIMIN;
	
	cJSON_SetNumberValue(itemBrightness, brightness);
	FILE *file = fopen(settings_file, "w");
	char *test = cJSON_Print(request_json);
	fputs(test, file);
	fclose(file);
	
	cJSON_Delete(request_json);
	free(request_body);

    const int brightness_table[5][11] = {
        // lcd_frequency = 0
        {20, 34, 48, 62, 76, 90, 104, 118, 132, 146, 160},
        // lcd_frequency = 1
        {8, 21, 34, 47, 60, 74, 87, 100, 113, 126, 140},
        // lcd_frequency = 2
        {10, 19, 28, 37, 46, 55, 64, 73, 82, 91, 100},
        // lcd_frequency = 3
        {45, 49, 54, 58, 63, 67, 72, 76, 81, 85, 90},
        // lcd_frequency = 4
        {65, 70, 75, 80, 85, 90, 95, 100, 105, 110, 115}
    };

    int sysfs_value;

    if (lcd >= 0 && lcd <= 4 && brightness >= 0 && brightness <= 10) {
        sysfs_value = brightness_table[lcd][brightness];
    }

    int fd = open("/sys/class/backlight/backlight/brightness", O_WRONLY);
    if (fd >= 0) {
        dprintf(fd, "%d", sysfs_value);
        close(fd);
    }
}

int main (int argc, char *argv[]) {
    input_fd = open("/dev/input/event5", O_RDONLY); // MIYOO Player1
    //volume_fd = open("/dev/input/event0", O_RDONLY); // gpio-keys-polled

    if (input_fd < 0 || volume_fd < 0) {
        perror("Failed to open input devices");
        return EXIT_FAILURE;
    }
	
	int level = 0;
	cJSON* request_json = NULL;
    cJSON* itemBrightness;

    const char *settings_file = "/mnt/SDCARD/system.json";

	char *request_body = load_file(settings_file);
	request_json = cJSON_Parse(request_body);
	itemBrightness = cJSON_GetObjectItem(request_json, "brightness");
	int brightness = cJSON_GetNumberValue(itemBrightness);
	level = brightness;
	cJSON_Delete(request_json);
	free(request_body);

    //struct input_event ev1, ev2;
    fd_set rfds;
    //int max_fd = (input_fd > volume_fd) ? input_fd : volume_fd;

    int menu_down = 0;
    int select_down = 0;
    int start_down = 0;
    int l1_down = 0;
    int r1_down = 0;
    int triggered = 0;

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(input_fd, &rfds);
        //FD_SET(volume_fd, &rfds);

        if (select(input_fd + 1, &rfds, NULL, NULL, NULL) > 0) {
            if (FD_ISSET(input_fd, &rfds)) {
                if (read(input_fd, &ev1, sizeof(ev1)) == sizeof(ev1)) {
                    if (ev1.type == EV_KEY && ev1.code == BUTTON_MENU) {
                        menu_down = (ev1.value == PRESSED);
                    }
                    
                    if (ev1.type == EV_KEY) {
                       if (ev1.code == BUTTON_SELECT) {
                       select_down = (ev1.value == PRESSED);
                       } else if (ev1.code == BUTTON_START) {
                       start_down = (ev1.value == PRESSED);
                       } else if (ev1.code == BUTTON_L1) {
                       l1_down = (ev1.value == PRESSED);
                       } else if (ev1.code == BUTTON_R1) {
                       r1_down = (ev1.value == PRESSED);
                       }

                       if (select_down && start_down && l1_down && r1_down &&!triggered) {
                       if (system("pgrep retroarch > /dev/null") == 0) {
                           system("pkill -9 retroarch");
                           } else {
                           system("pkill -9 MainUI");
                       }
                       triggered = 1;
                       }

                       if (!select_down || !start_down || !l1_down || !r1_down) {
                       triggered = 0;
                       }
                    }

                    if (ev1.type == EV_ABS && ev1.code == ABS_HAT0Y) {
                        if (ev1.value == -1 && menu_down) { // DPAD UP
                            level = level + 1;
                            if (level > BRIMAX) level = BRIMAX;
                            modifyBrightness(level);
                        } else if (ev1.value == 1 && menu_down) { // DPAD DOWN
                            level = level -1;
                            if (level < BRIMIN) level = BRIMIN;
                            modifyBrightness(level);
                        }
                    }
                }
            }
        }
    }


    close(input_fd);
    //close(volume_fd);
    return EXIT_SUCCESS;
}
