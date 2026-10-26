"""Generate a private header without shell interpolation or logging credentials."""
import ipaddress
import json
import os
from pathlib import Path


def configuration(env):
    values = {key: env.get(key, "") for key in
              ("WIFI_SSID", "WIFI_PASSWORD", "SERVER_IP", "BUS_TOKEN")}
    for key in ("WIFI_SSID", "SERVER_IP", "BUS_TOKEN"):
        if not values[key]:
            raise ValueError(f"Set {key} in the environment")
    ipaddress.IPv4Address(values["SERVER_IP"])
    if not 16 <= len(values["BUS_TOKEN"]) <= 64 or not values["BUS_TOKEN"].isalnum() or not values["BUS_TOKEN"].isascii():
        raise ValueError("BUS_TOKEN must contain 16–64 ASCII letters/digits")
    if len(values["WIFI_SSID"].encode()) > 32:
        raise ValueError("WIFI_SSID must be at most 32 bytes")
    port = int(env.get("SERVER_PORT") or "8766")
    if not 1 <= port <= 65535:
        raise ValueError("SERVER_PORT must be 1–65535")
    lines = ["// Generated; do not commit.", "#pragma once"]
    lines += [f"#define {key} {json.dumps(value, ensure_ascii=True)}" for key, value in values.items()]
    lines += [f"#define SERVER_PORT {port}"]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    try:
        content = configuration(os.environ)
    except ValueError as error:
        raise SystemExit(str(error))
    path = Path(__file__).resolve().parents[1] / "firmware/bus/config.h"
    path.touch(mode=0o600, exist_ok=True)
    path.chmod(0o600)
    path.write_text(content)
