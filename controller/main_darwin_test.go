//go:build darwin && cgo

package main

import (
	"math"
	"testing"
)

func TestAxis(t *testing.T) {
	for _, sample := range []struct {
		input       float64
		limit, want int
	}{
		{0, 90, 0}, {0.1, 90, 0}, {-0.1, 100, 0}, {0.12, 100, 0},
		{1, 90, 90}, {-1, 90, -90}, {1, 100, 100}, {-1, 100, -100},
		{0.56, 100, 50}, {-0.56, 100, -50}, {2, 100, 100}, {-2, 100, -100},
	} {
		if got := axis(sample.input, 0.12, sample.limit); got != sample.want {
			t.Errorf("axis(%v,%d)=%d, want %d", sample.input, sample.limit, got, sample.want)
		}
	}
	for n := 0; n <= 1000; n++ {
		value := float64(n) / 1000
		if axis(-value, 0.12, 100) != -axis(value, 0.12, 100) {
			t.Fatal("asymmetric mapping")
		}
		if math.Abs(float64(axis(value, 0.12, 90))) > 90 {
			t.Fatal("angle exceeds protocol range")
		}
	}
}

func TestPacket(t *testing.T) {
	for _, test := range []struct {
		angle, speed int
		want         [3]byte
	}{
		{0, 0, [3]byte{1, 0, 0}}, {-90, -100, [3]byte{1, 166, 156}}, {90, 100, [3]byte{1, 90, 100}},
	} {
		if got := packet(test.angle, test.speed); got != test.want {
			t.Errorf("packet=%v want %v", got, test.want)
		}
	}
}

func TestTriggerSpeed(t *testing.T) {
	for _, test := range []struct {
		left, right float64
		want        int
	}{
		{0, 0, 0}, {0, 1, 100}, {1, 0, -100}, {0.3, 0.8, 80}, {0.8, 0.3, -80},
		{0.5, 0.5, 0}, {1, 1, 0}, {0.01, 0.02, 0}, {0, 0.03, 3},
		{-1, 2, 100}, {2, -1, -100},
	} {
		if got := triggerSpeed(test.left, test.right); got != test.want {
			t.Errorf("triggerSpeed(%v,%v)=%d want %d", test.left, test.right, got, test.want)
		}
	}
}
