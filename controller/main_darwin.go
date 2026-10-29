//go:build darwin && cgo

// Native macOS Xbox input and UDP forwarding. No third-party packages.
package main

/*
#cgo LDFLAGS: -framework IOKit -framework CoreFoundation
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    int connected;
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

static void inputValue(void *context, IOReturn result, void *sender, IOHIDValueRef value) {
    if (result != kIOReturnSuccess || !controller) return;
    IOHIDElementRef element = IOHIDValueGetElement(value);
    if (IOHIDElementGetDevice(element) != controller) return;
    uint32_t page = IOHIDElementGetUsagePage(element), usage = IOHIDElementGetUsage(element);
    CFIndex raw = IOHIDValueGetIntegerValue(value);
    if (page == 1 && usage == 0x30) state.steering = axisValue(element, raw);
    if (page == 2 && usage == 0xc5) state.leftTrigger = triggerValue(element, raw);
    if (page == 2 && usage == 0xc4) state.rightTrigger = triggerValue(element, raw);
}

static void deviceConnected(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
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

static ControllerState pollController(double seconds) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
    return state;
}

static void stopController(void) {
    IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    if (controller) { CFRelease(controller); controller = NULL; }
}
*/
import "C"

import (
	"flag"
	"fmt"
	"math"
	"net"
	"os"
	"os/signal"
	"runtime"
	"syscall"
	"time"
)

func init() { runtime.LockOSThread() } // HID callbacks and reads stay on this thread.

func axis(value, deadzone float64, limit int) int {
	value = math.Max(-1, math.Min(1, value))
	magnitude := math.Abs(value)
	if magnitude <= deadzone {
		return 0
	}
	return int(math.Round(math.Copysign((magnitude-deadzone)/(1-deadzone), value) * float64(limit)))
}

// The stronger trigger wins at its own pressure; do not subtract the weaker one.
func triggerSpeed(left, right float64) int {
	left = math.Max(0, math.Min(1, left))
	right = math.Max(0, math.Min(1, right))
	if left <= 0.02 {
		left = 0
	}
	if right <= 0.02 {
		right = 0
	}
	if right > left {
		return int(math.Round(right * 100))
	}
	if left > right {
		return -int(math.Round(left * 100))
	}
	return 0
}

func packet(angle, speed int) [3]byte { return [3]byte{1, byte(int8(angle)), byte(int8(speed))} }

func run() error {
	address := flag.String("addr", "", "board IP:5000; omitted = read-only dry run")
	deadzone := flag.Float64("deadzone", 0.12, "stick deadzone, 0 to less than 1")
	duration := flag.Duration("duration", 0, "exit after this duration; 0 runs until Ctrl-C")
	flag.Parse()
	if *deadzone < 0 || *deadzone >= 1 || math.IsNaN(*deadzone) {
		return fmt.Errorf("invalid deadzone")
	}
	if *duration < 0 {
		return fmt.Errorf("duration must be nonnegative")
	}

	var connection *net.UDPConn
	if *address != "" {
		remote, err := net.ResolveUDPAddr("udp", *address)
		if err != nil {
			return err
		}
		connection, err = net.DialUDP("udp", nil, remote)
		if err != nil {
			return err
		}
		defer connection.Close()
		fmt.Println("Sending to", remote, "at 50 Hz; center steering and release both triggers to begin")
	} else {
		fmt.Println("DRY RUN: no UDP packets will be sent")
	}

	if result := C.startController(); result != 0 {
		return fmt.Errorf("HID open failed: 0x%x; check macOS Input Monitoring permission", uint32(result))
	}
	defer C.stopController()
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, os.Interrupt, syscall.SIGTERM)
	defer signal.Stop(quit)
	start := time.Now()
	nextPoll := start.Add(20 * time.Millisecond)
	connected, ready := false, false
	lastAngle := 0
	var lastPrint time.Time
	send := func(angle, speed int) error {
		if connection == nil {
			return nil
		}
		frame := packet(angle, speed)
		if err := connection.SetWriteDeadline(time.Now().Add(50 * time.Millisecond)); err != nil {
			return err
		}
		_, err := connection.Write(frame[:])
		return err
	}
	defer func() {
		if ready {
			_ = send(lastAngle, 0)
		}
	}()

	for {
		select {
		case <-quit:
			return nil
		default:
		}
		if *duration > 0 && time.Since(start) >= *duration {
			return nil
		}
		wait := time.Until(nextPoll)
		if wait < 0 {
			wait = 0
		}
		state := C.pollController(C.double(wait.Seconds()))
		nextPoll = nextPoll.Add(20 * time.Millisecond)
		if time.Since(nextPoll) > 20*time.Millisecond {
			nextPoll = time.Now().Add(20 * time.Millisecond)
		}
		if state.connected == 0 {
			if connected {
				fmt.Println("Controller disconnected; motor stop sent, then transmission ends")
				if err := send(lastAngle, 0); err != nil {
					return err
				}
			}
			connected, ready = false, false
			continue
		}
		angle := axis(float64(state.steering), *deadzone, 90)
		speed := triggerSpeed(float64(state.leftTrigger), float64(state.rightTrigger))
		if !connected {
			fmt.Println("Connected:", C.GoString(&state.name[0]))
			fmt.Println("Left stick X = steering; right trigger = forward; left trigger = reverse. Stronger wins. Ctrl-C exits.")
			connected = true
		}
		if !ready && angle == 0 && state.leftTrigger <= 0.02 && state.rightTrigger <= 0.02 {
			ready = true
			fmt.Println("Steering centered and triggers released; ready")
		}
		if ready {
			if err := send(angle, speed); err != nil {
				return err
			}
			lastAngle = angle
		}
		if time.Since(lastPrint) >= 250*time.Millisecond {
			fmt.Printf("angle=%+4d speed=%+4d LT=%3.0f%% RT=%3.0f%% ready=%t\n", angle, speed, float64(state.leftTrigger)*100, float64(state.rightTrigger)*100, ready)
			lastPrint = time.Now()
		}
	}
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
