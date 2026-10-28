#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${WIFI_SSID:?Set WIFI_SSID}"
export WIFI_SSID WIFI_PASSWORD
umask 077
mkdir -p .build
# ENVIRON keeps credentials out of shell-generated code and command arguments.
awk 'function quote(s) { gsub(/\\/, "\\\\", s); gsub(/"/, "\\\"", s); gsub(/\r/, "\\r", s); gsub(/\n/, "\\n", s); gsub(/\t/, "\\t", s); return "\"" s "\"" }
BEGIN {
 if (length(ENVIRON["WIFI_SSID"]) > 32) { print "SSID exceeds 32 bytes" > "/dev/stderr"; exit 1 }
 print "#pragma once"
 print "#define WIFI_SSID " quote(ENVIRON["WIFI_SSID"])
 print "#define WIFI_PASSWORD " quote(ENVIRON["WIFI_PASSWORD"])
}' > .build/config.h
mv .build/config.h firmware/bus/config.h
{
  printf '#pragma once\nstatic const char INDEX_HTML[] = R"BUSHTML(\n'
  cat index.html
  printf '\n)BUSHTML";\nstatic const char CONTROLS_JS[] = R"BUSJS(\n'
  cat controls.js
  printf '\n)BUSJS";\n'
} > .build/web.h
mv .build/web.h firmware/bus/web.h
