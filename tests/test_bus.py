import importlib.util
from pathlib import Path
import time
import unittest
from unittest.mock import patch

from server import State

spec = importlib.util.spec_from_file_location("configure", Path(__file__).resolve().parents[1] / "tools/configure.py")
configure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(configure)


class ProtocolTests(unittest.TestCase):
    def test_command_lease_and_expiry(self):
        state = State()
        with patch("server.time.monotonic", return_value=100):
            state.command(120, -120)
            self.assertEqual(state.reply(b"7"), b"7 120 -120")
        with patch("server.time.monotonic", return_value=101):
            self.assertEqual(state.reply(b"8"), b"8 0 0")

    def test_hold_refreshes_until_release_or_browser_loss(self):
        state = State()
        for tick in range(30):
            with patch("server.time.monotonic", return_value=100 + tick / 10):
                state.command(255, 0)
                self.assertEqual(state.reply(b"1"), b"1 255 0")
        with patch("server.time.monotonic", return_value=103):
            state.command(0, 0)
            self.assertEqual(state.reply(b"2"), b"2 0 0")
            state.command(0, -255)
        with patch("server.time.monotonic", return_value=104):
            self.assertEqual(state.reply(b"3"), b"3 0 0")

    def test_boot_stopped_and_invalid_packets(self):
        state = State()
        self.assertEqual(state.reply(b"1"), b"1 0 0")
        for packet in (b"wrong 1", b"\xff", b"-1", b"4294967296", b"1 2", b""):
            self.assertIsNone(state.reply(packet))
        for value in (True, 256, -256, 1.2, "12"):
            with self.assertRaises(ValueError):
                state.command(value, 0)

    def test_connection_status(self):
        state = State()
        with patch("server.time.monotonic", return_value=0.1):
            self.assertFalse(state.status()["connected"])
            self.assertIsNone(state.status()["last_seen_seconds"])
        with patch("server.time.monotonic", return_value=100):
            state.reply(b"bad", ("192.168.1.9", 8767))
            self.assertIsNone(state.status()["board_ip"])
            state.reply(b"1", ("192.168.1.9", 8767))
            self.assertTrue(state.status()["connected"])
            self.assertEqual(state.status()["board_ip"], "192.168.1.9")
        with patch("server.time.monotonic", return_value=102):
            status = state.status()
            self.assertFalse(status["connected"])
            self.assertEqual(status["last_seen_seconds"], 2)
            self.assertEqual(status["driver_status"], "not_monitored")

    def test_configuration(self):
        env = dict(WIFI_SSID='network"name', WIFI_PASSWORD='p\\word', SERVER_IP="192.168.1.2")
        output = configure.configuration(env)
        self.assertIn('#define SERVER_PORT 8766', output)
        self.assertIn('network\\"name', output)
        with self.assertRaises(ValueError):
            configure.configuration({**env, "SERVER_IP": "invalid"})


if __name__ == "__main__":
    unittest.main()
