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
    def test_pulse_and_expiry(self):
        state = State()
        with patch("server.time.monotonic", return_value=100):
            state.command(120, -120)
            self.assertEqual(state.reply(b"7"), b"7 120 -120")
        with patch("server.time.monotonic", return_value=101):
            self.assertEqual(state.reply(b"8"), b"8 0 0")

    def test_boot_stopped_and_invalid_packets(self):
        state = State()
        self.assertEqual(state.reply(b"1"), b"1 0 0")
        for packet in (b"wrong 1", b"\xff", b"-1", b"4294967296", b"1 2", b""):
            self.assertIsNone(state.reply(packet))
        for value in (True, 256, -256, 1.2, "12"):
            with self.assertRaises(ValueError):
                state.command(value, 0)

    def test_configuration(self):
        env = dict(WIFI_SSID='network"name', WIFI_PASSWORD='p\\word', SERVER_IP="192.168.1.2")
        output = configure.configuration(env)
        self.assertIn('#define SERVER_PORT 8766', output)
        self.assertIn('network\\"name', output)
        with self.assertRaises(ValueError):
            configure.configuration({**env, "SERVER_IP": "invalid"})


if __name__ == "__main__":
    unittest.main()
