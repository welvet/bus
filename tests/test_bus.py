import importlib.util
from pathlib import Path
import time
import unittest
from unittest.mock import patch

from server import State

spec = importlib.util.spec_from_file_location("configure", Path(__file__).resolve().parents[1] / "tools/configure.py")
configure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(configure)
TOKEN = "a" * 32


class ProtocolTests(unittest.TestCase):
    def test_pulse_and_expiry(self):
        state = State()
        with patch("server.time.monotonic", return_value=100):
            state.command(120, -120)
            self.assertEqual(state.reply(f"{TOKEN} 7".encode(), TOKEN), f"{TOKEN} 7 120 -120".encode())
        with patch("server.time.monotonic", return_value=101):
            self.assertEqual(state.reply(f"{TOKEN} 8".encode(), TOKEN), f"{TOKEN} 8 0 0".encode())

    def test_boot_stopped_and_invalid_packets(self):
        state = State()
        self.assertEqual(state.reply(f"{TOKEN} 1".encode(), TOKEN), f"{TOKEN} 1 0 0".encode())
        for packet in (b"wrong 1", b"\xff", f"{TOKEN} -1".encode(), f"{TOKEN} 4294967296".encode()):
            self.assertIsNone(state.reply(packet, TOKEN))
        for value in (True, 256, -256, 1.2, "12"):
            with self.assertRaises(ValueError):
                state.command(value, 0)

    def test_configuration(self):
        env = dict(WIFI_SSID='network"name', WIFI_PASSWORD='p\\word', SERVER_IP="192.168.1.2", BUS_TOKEN=TOKEN)
        output = configure.configuration(env)
        self.assertIn('#define SERVER_PORT 8766', output)
        self.assertIn('network\\"name', output)
        with self.assertRaises(ValueError):
            configure.configuration({**env, "SERVER_IP": "invalid"})
        with self.assertRaises(ValueError):
            configure.configuration({**env, "BUS_TOKEN": "short"})


if __name__ == "__main__":
    unittest.main()
