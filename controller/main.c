// Native macOS Xbox controller -> three-byte UDP commands.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

static volatile sig_atomic_t steeringCalibrationRequested;
static volatile sig_atomic_t steeringResetRequested;

typedef struct {
    int connected;
    int calibrateDown;
    int resetDown;
    double steering;
    double leftTrigger;
    double rightTrigger;
    char name[128];
} ControllerState;

static IOHIDManagerRef manager;
static IOHIDDeviceRef controller;
static ControllerState state;

static double axisValue(IOHIDElementRef element, CFIndex value) {
    CFIndex low = IOHIDElementGetLogicalMin(element);
    CFIndex high = IOHIDElementGetLogicalMax(element);
    if (high <= low) return 0;
    return 2.0 * (value - low) / (high - low) - 1.0;
}

static double triggerValue(IOHIDElementRef element, CFIndex value) {
    CFIndex low = IOHIDElementGetLogicalMin(element);
    CFIndex high = IOHIDElementGetLogicalMax(element);
    if (high <= low) return 0;
    return (double)(value - low) / (high - low);
}

static void calibrationButton(int pressed) {
    if (pressed && !state.calibrateDown) steeringCalibrationRequested = 1;
    state.calibrateDown = pressed != 0;
}

static void resetButton(int pressed) {
    if (pressed && !state.resetDown) steeringResetRequested = 1;
    state.resetDown = pressed != 0;
}

static void inputValue(void *context, IOReturn result, void *sender, IOHIDValueRef value) {
    (void)context; (void)sender;
    if (result != kIOReturnSuccess || !controller) return;
    IOHIDElementRef element = IOHIDValueGetElement(value);
    if (IOHIDElementGetDevice(element) != controller) return;
    uint32_t page = IOHIDElementGetUsagePage(element), usage = IOHIDElementGetUsage(element);
    CFIndex raw = IOHIDValueGetIntegerValue(value);
    // Xbox BLE Y (top face button, equivalent position to Triangle).
    if (page == 9 && usage == 5) calibrationButton(raw != 0);
    if (page == 9 && usage == 2) resetButton(raw != 0);
    if (page == 1 && usage == 0x30) state.steering = axisValue(element, raw);
    if (page == 2 && usage == 0xc5) state.leftTrigger = triggerValue(element, raw);
    if (page == 2 && usage == 0xc4) state.rightTrigger = triggerValue(element, raw);
}

static void deviceConnected(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
    (void)context; (void)sender;
    if (result != kIOReturnSuccess || controller) return;
    CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, NULL, kIOHIDOptionsTypeNone);
    IOHIDElementRef steering = NULL, left = NULL, right = NULL;
    if (!elements) return;
    for (CFIndex i = 0; i < CFArrayGetCount(elements); ++i) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
        if (IOHIDElementGetType(e) > kIOHIDElementTypeInput_ScanCodes) continue;
        if (IOHIDElementGetUsagePage(e) == 1 && IOHIDElementGetUsage(e) == 0x30) steering = e;
        if (IOHIDElementGetUsagePage(e) == 2 && IOHIDElementGetUsage(e) == 0xc5) left = e;
        if (IOHIDElementGetUsagePage(e) == 2 && IOHIDElementGetUsage(e) == 0xc4) right = e;
    }
    if (steering && left && right) {
        controller = (IOHIDDeviceRef)CFRetain(device);
        memset(&state, 0, sizeof(state));
        state.connected = 1;
        CFStringRef name = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
        if (name && CFGetTypeID(name) == CFStringGetTypeID())
            CFStringGetCString(name, state.name, sizeof(state.name), kCFStringEncodingUTF8);
        IOHIDValueRef value;
        if (IOHIDDeviceGetValue(device, steering, &value) == kIOReturnSuccess)
            state.steering = axisValue(steering, IOHIDValueGetIntegerValue(value));
        if (IOHIDDeviceGetValue(device, left, &value) == kIOReturnSuccess)
            state.leftTrigger = triggerValue(left, IOHIDValueGetIntegerValue(value));
        if (IOHIDDeviceGetValue(device, right, &value) == kIOReturnSuccess)
            state.rightTrigger = triggerValue(right, IOHIDValueGetIntegerValue(value));
    }
    CFRelease(elements);
}

static void deviceDisconnected(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
    (void)context; (void)result; (void)sender;
    if (device == controller) {
        CFRelease(controller); controller = NULL;
        memset(&state, 0, sizeof(state));
    }
}

static int startController(void) {
    manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!manager) return -1;
    int vendor = 0x045e; // Microsoft Xbox BLE controller.
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberIntType, &vendor);
    const void *keys[] = {CFSTR(kIOHIDVendorIDKey)};
    const void *values[] = {number};
    CFDictionaryRef match = CFDictionaryCreate(NULL, keys, values, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match); CFRelease(number);
    IOHIDManagerRegisterDeviceMatchingCallback(manager, deviceConnected, NULL);
    IOHIDManagerRegisterDeviceRemovalCallback(manager, deviceDisconnected, NULL);
    IOHIDManagerRegisterInputValueCallback(manager, inputValue, NULL);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOReturn result = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (result != kIOReturnSuccess) return (int)result;
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (devices) {
        CFIndex count = CFSetGetCount(devices);
        const void **list = calloc(count ? count : 1, sizeof(void *));
        if (list) {
            CFSetGetValues(devices, list);
            for (CFIndex i = 0; i < count; ++i)
                deviceConnected(NULL, kIOReturnSuccess, NULL, (IOHIDDeviceRef)list[i]);
            free(list);
        }
        CFRelease(devices);
    }
    return 0;
}

static void stopController(void) {
    if (!manager) return;
    IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager); manager = NULL;
    if (controller) { CFRelease(controller); controller = NULL; }
}

#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <errno.h>

static volatile sig_atomic_t quitting;
static void stopSignal(int signal) { (void)signal; quitting = 1; }
static void calibrateSteeringSignal(int signal) { (void)signal; steeringCalibrationRequested = 1; }

static double monotonicSeconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

static int steeringAngle(double value, double deadzone) {
    value = fmax(-1, fmin(1, value));
    if (fabs(value) <= deadzone) return 0;
    return (int)round(copysign((fabs(value) - deadzone) / (1 - deadzone), value) * 90);
}

static int triggerSpeed(double left, double right) {
    // Both directions share the measured minimum output needed to start moving.
    const double released = 0.02;
    const int minimum = 15;
    left = fmax(0, fmin(1, left)); right = fmax(0, fmin(1, right));
    if (left <= released) left = 0;
    if (right <= released) right = 0;
    if (left == right) return 0;
    double pressure = fmax(left, right);
    double travel = (pressure - released) / (1 - released);
    // Quadratic response gives more trigger travel to low-speed control.
    int speed = minimum + (int)round(travel * travel * (100 - minimum));
    return right > left ? speed : -speed;
}

static int trimmedAngle(int stick, int trim, bool centering) {
    // Travel limits are offsets from the saved center, within servo limits.
    int center = trim < -90 ? -90 : trim > 90 ? 90 : trim;
    if (centering) return center;
    int minimum = center - 71 < -90 ? -90 : center - 71;
    int maximum = center + 35 > 90 ? 90 : center + 35;
    stick = stick < -90 ? -90 : stick > 90 ? 90 : stick;
    int travel = stick < 0 ? center - minimum : maximum - center;
    double position = stick / 90.0;
    return center + (int)round(position * fabs(position) * travel);
}

static const char *trimPath(void) {
    static char path[1024];
    const char *home = getenv("HOME");
    if (!home || snprintf(path, sizeof(path), "%s/Library/Application Support/Bus/steering-trim", home) >= (int)sizeof(path)) return NULL;
    return path;
}

static int loadTrim(void) {
    const char *path = trimPath();
    FILE *file = path ? fopen(path, "r") : NULL;
    int trim = 0;
    if (file) { if (fscanf(file, "%d", &trim) != 1 || trim < -90 || trim > 90) trim = 0; fclose(file); }
    return trim;
}

static void saveTrim(int trim) {
    const char *path = trimPath();
    FILE *file = path ? fopen(path, "w") : NULL;
    if (file) { fprintf(file, "%d\n", trim); fclose(file); }
    else fputs("Could not save servo center; calibration is in memory only\n", stderr);
}

static void commandBytes(uint8_t bytes[3], int angle, int speed) {
    // Current motor mounting requires the opposite electrical direction.
    bytes[0] = 1; bytes[1] = (uint8_t)(int8_t)angle; bytes[2] = (uint8_t)(int8_t)(-speed);
}

static int sendCommand(int socket, int angle, int speed) {
    if (socket < 0) return 0; // Dry run.
    uint8_t bytes[3]; commandBytes(bytes, angle, speed);
    return send(socket, bytes, sizeof(bytes), 0) == sizeof(bytes) ? 0 : -1;
}

static int openUDP(const char *address) {
    const char *colon = strrchr(address, ':');
    char host[256];
    if (!colon || colon == address || colon - address >= (int)sizeof(host) || !colon[1]) {
        fprintf(stderr, "Address must be HOST:PORT\n"); return -1;
    }
    memcpy(host, address, colon - address); host[colon - address] = 0;
    struct addrinfo hints = {0}, *addresses = NULL;
    hints.ai_socktype = SOCK_DGRAM;
    int error = getaddrinfo(host, colon + 1, &hints, &addresses);
    if (error) { fprintf(stderr, "%s\n", gai_strerror(error)); return -1; }
    int fd = -1;
    for (struct addrinfo *a = addresses; a; a = a->ai_next) {
        fd = socket(a->ai_family, SOCK_DGRAM, a->ai_protocol);
        if (fd < 0) continue;
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 50000};
        if (!setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) &&
            !connect(fd, a->ai_addr, a->ai_addrlen)) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) perror("UDP setup");
    return fd;
}

static bool parseNumber(const char *text, bool duration, double *out) {
    char *end;
    errno = 0; double value = strtod(text, &end);
    if (end == text || errno || !isfinite(value)) return false;
    if (duration && !strcmp(end, "ms")) value /= 1000;
    else if (*end && !(duration && !strcmp(end, "s"))) return false;
    *out = value; return true;
}

int main(int argc, char **argv) {
    const char *address = NULL;
    double deadzone = 0.12, duration = 0;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];
        if (*option != '-') { fprintf(stderr, "Expected an option: %s\n", option); return 1; }
        while (*option == '-') ++option;
        if (!strcmp(option, "help") || !strcmp(option, "h")) {
            puts("controller [--addr HOST:5000] [--deadzone 0.12] [--duration 10s] [--quiet]\nNo address = dry run, no UDP packets.");
            return 0;
        }
        if (!strcmp(option, "quiet")) { quiet = true; continue; }
        if (++i >= argc) { fprintf(stderr, "Missing value for %s\n", option); return 1; }
        if (!strcmp(option, "addr")) address = argv[i];
        else if (!strcmp(option, "deadzone") && parseNumber(argv[i], false, &deadzone)) {}
        else if (!strcmp(option, "duration") && parseNumber(argv[i], true, &duration)) {}
        else { fprintf(stderr, "Invalid option/value: %s %s\n", option, argv[i]); return 1; }
    }
    if (deadzone < 0 || deadzone >= 1 || duration < 0) {
        fputs("Invalid deadzone or duration\n", stderr); return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    int fd = address ? openUDP(address) : -1;
    if (address && fd < 0) return 1;
    if (address) printf("Sending to %s at 50 Hz; center steering and release triggers to begin\n", address);
    else puts("DRY RUN: no UDP packets will be sent");
    int error = startController();
    if (error) {
        fprintf(stderr, "HID open failed: 0x%x; check macOS Input Monitoring permission\n", (unsigned)error);
        stopController(); if (fd >= 0) close(fd); return 1;
    }
    struct sigaction action = {0}; action.sa_handler = stopSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
    action.sa_handler = calibrateSteeringSignal; sigaction(SIGUSR2, &action, NULL);
    double start = monotonicSeconds(), next = start + 0.02, lastPrint = 0;
    bool connected = false, ready = false;
    int lastAngle = 0, status = 0, steeringTrim = loadTrim();
    bool steeringCentering = false;
    while (!quitting && (!duration || monotonicSeconds() - start < duration)) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, fmax(0, next - monotonicSeconds()), false);
        double now = monotonicSeconds();
        next += 0.02;
        if (now - next > 0.02) next = now + 0.02;
        if (!state.connected) {
            if (connected) {
                puts("Controller disconnected; stopping transmission");
                if (sendCommand(fd, lastAngle, 0)) { perror("UDP stop"); status = 1; break; }
            }
            connected = ready = false;
            continue;
        }
        double left = state.leftTrigger, right = state.rightTrigger;
        int stickAngle = steeringAngle(state.steering, deadzone);
        if (steeringCalibrationRequested) {
            steeringCalibrationRequested = 0;
            steeringTrim = ready ? lastAngle : trimmedAngle(stickAngle, steeringTrim, false); steeringCentering = true;
            if (left <= 0.02 && right <= 0.02) ready = true;
            saveTrim(steeringTrim);
            printf("Servo center calibrated: %+d degrees; release steering stick\n", steeringTrim);
        }
        if (steeringResetRequested) {
            steeringResetRequested = 0;
            steeringTrim = 0;
            steeringCentering = true;
            saveTrim(steeringTrim);
            puts("Servo center reset to 0 degrees; release steering stick");
        }
        if (steeringCentering && stickAngle == 0) steeringCentering = false;
        int angle = trimmedAngle(stickAngle, steeringTrim, steeringCentering);
        int speed = triggerSpeed(left, right);
        if (!connected) {
            printf("Connected: %s\n", state.name);
            puts("Left stick X = steering; RT = forward; LT = reverse. Stronger wins. Y calibrates servo center; B resets it to 0. Ctrl-C exits.");
            connected = true;
        }
        if (!ready && stickAngle == 0 && left <= 0.02 && right <= 0.02) {
            ready = true; puts("Steering centered and triggers released; ready");
        }
        if (ready) {
            if (sendCommand(fd, angle, speed)) { perror("UDP send"); status = 1; break; }
            lastAngle = angle;
        }
        if (!quiet && now - lastPrint >= 0.25) {
            printf("angle=%+4d speed=%+4d LT=%3.0f%% RT=%3.0f%% ready=%d\n",
                angle, speed, state.leftTrigger * 100, state.rightTrigger * 100, ready);
            lastPrint = now;
        }
    }
    if (ready) (void)sendCommand(fd, lastAngle, 0);
    stopController(); if (fd >= 0) close(fd);
    return status;
}
