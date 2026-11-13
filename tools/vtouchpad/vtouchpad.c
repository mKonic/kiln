// vtouchpad: a virtual touchpad through /dev/uinput, for testing trackpad input on a rooted
// Android device without one (Android reads it as a real touchpad: pointer, two-finger scroll,
// pinch). Gestures come on stdin, one per line:
//
//   wait MS                     do nothing for MS
//   move DX DY MS               one finger moves the pointer by DX, DY (pad units)
//   click                       one finger taps and presses the pad's button
//   scroll DX DY MS             two fingers move together by DX, DY
//   pinch R0 R1 MS              two fingers, R0 to R1 apart from the pad's middle (horizontal)
//
// The pad is 1920 x 1080 units at 20 units/mm (96 x 54 mm). Build with the NDK
// (tools/vtouchpad/run builds, pushes and runs it as root).
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { W = 1920, H = 1080, RES = 20, STEP_MS = 8 };

static int fd;

static void emit(int type, int code, int value) {
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.code = code;
    e.value = value;
    if (write(fd, &e, sizeof e) != sizeof e) perror("write");
}
static void syn(void) { emit(EV_SYN, SYN_REPORT, 0); }
static void sleep_ms(int ms) {
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

static void abs_axis(int code, int min, int max, int res) {
    struct uinput_abs_setup a;
    memset(&a, 0, sizeof a);
    a.code = code;
    a.absinfo.minimum = min;
    a.absinfo.maximum = max;
    a.absinfo.resolution = res;
    if (ioctl(fd, UI_ABS_SETUP, &a) < 0) perror("UI_ABS_SETUP");
}

static int tracking = 1;
static const int tools[] = { 0, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP };

// n fingers at xs[i], ys[i]; down: first frame of a contact, up: lift them all.
static void contacts(int n, const int* xs, const int* ys, int down, int up) {
    for (int i = 0; i < n; i++) {
        emit(EV_ABS, ABS_MT_SLOT, i);
        if (up) {
            emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
            continue;
        }
        if (down) emit(EV_ABS, ABS_MT_TRACKING_ID, tracking++);
        emit(EV_ABS, ABS_MT_POSITION_X, xs[i]);
        emit(EV_ABS, ABS_MT_POSITION_Y, ys[i]);
        emit(EV_ABS, ABS_MT_PRESSURE, 60);
    }
    if (!up) {
        emit(EV_ABS, ABS_X, xs[0]);
        emit(EV_ABS, ABS_Y, ys[0]);
    }
    if (down || up) {
        emit(EV_KEY, BTN_TOUCH, up ? 0 : 1);
        emit(EV_KEY, tools[n], up ? 0 : 1);
    }
    syn();
}

// Moves n fingers from (x0[i], y0[i]) to (x1[i], y1[i]) over ms, then lifts them.
static void gesture(int n, const int* x0, const int* y0, const int* x1, const int* y1, int ms) {
    int xs[3], ys[3];
    const int steps = ms / STEP_MS > 1 ? ms / STEP_MS : 1;
    contacts(n, x0, y0, 1, 0);
    sleep_ms(STEP_MS);
    for (int s = 1; s <= steps; s++) {
        for (int i = 0; i < n; i++) {
            xs[i] = x0[i] + (x1[i] - x0[i]) * s / steps;
            ys[i] = y0[i] + (y1[i] - y0[i]) * s / steps;
        }
        contacts(n, xs, ys, 0, 0);
        sleep_ms(STEP_MS);
    }
    contacts(n, xs, ys, 0, 1);
}

int main(void) {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    const int keys[] = { BTN_LEFT, BTN_TOUCH, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP };
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_BUTTONPAD);
    abs_axis(ABS_X, 0, W - 1, RES);
    abs_axis(ABS_Y, 0, H - 1, RES);
    abs_axis(ABS_MT_SLOT, 0, 4, 0);
    abs_axis(ABS_MT_TRACKING_ID, 0, 65535, 0);
    abs_axis(ABS_MT_POSITION_X, 0, W - 1, RES);
    abs_axis(ABS_MT_POSITION_Y, 0, H - 1, RES);
    abs_axis(ABS_MT_PRESSURE, 0, 255, 0);

    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1d6b;
    setup.id.product = 0x0104;
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "kiln virtual touchpad");
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    sleep_ms(1500);   // the input reader opens the new device

    char line[256];
    const int cx = W / 2, cy = H / 2;
    while (fgets(line, sizeof line, stdin)) {
        int a = 0, b = 0, c = 0;
        if (sscanf(line, "wait %d", &a) == 1) sleep_ms(a);
        else if (sscanf(line, "move %d %d %d", &a, &b, &c) == 3) {
            int x0[1] = { cx - a / 2 }, y0[1] = { cy - b / 2 }, x1[1] = { cx + a / 2 }, y1[1] = { cy + b / 2 };
            gesture(1, x0, y0, x1, y1, c);
        } else if (strncmp(line, "click", 5) == 0) {
            int x[1] = { cx }, y[1] = { cy };
            contacts(1, x, y, 1, 0);
            emit(EV_KEY, BTN_LEFT, 1); syn();
            sleep_ms(60);
            emit(EV_KEY, BTN_LEFT, 0); syn();
            contacts(1, x, y, 0, 1);
        } else if (sscanf(line, "scroll %d %d %d", &a, &b, &c) == 3) {
            int x0[2] = { cx - 150 - a / 2, cx + 150 - a / 2 }, y0[2] = { cy - b / 2, cy - b / 2 };
            int x1[2] = { x0[0] + a, x0[1] + a }, y1[2] = { y0[0] + b, y0[1] + b };
            gesture(2, x0, y0, x1, y1, c);
        } else if (sscanf(line, "pinch %d %d %d", &a, &b, &c) == 3) {
            int x0[2] = { cx - a, cx + a }, y0[2] = { cy, cy }, x1[2] = { cx - b, cx + b }, y1[2] = { cy, cy };
            gesture(2, x0, y0, x1, y1, c);
        } else if (line[0] != '\n' && line[0] != '#') fprintf(stderr, "vtouchpad: unknown: %s", line);
        sleep_ms(120);
    }
    sleep_ms(300);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
