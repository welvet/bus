#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
label=local.bus.controller
domain="gui/$(id -u)"
plist="$HOME/Library/LaunchAgents/$label.plist"
app_dir="$HOME/Library/Application Support/Bus"
log_dir="$HOME/Library/Logs/Bus"
xml_escape() { sed 's/\&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g'; }
case "${1:-status}" in
  install)
    : "${BOARD_IP:?Set BOARD_IP to the Arduino IPv4 address}"
    case "$BOARD_IP" in *[!0-9.]*|'') echo 'BOARD_IP must be an IPv4 address' >&2; exit 1;; esac
    mkdir -p "$app_dir" "$log_dir" "$(dirname "$plist")"
    cp .build/controller "$app_dir/controller.new"
    chmod 755 "$app_dir/controller.new"
    mv "$app_dir/controller.new" "$app_dir/controller"
    app_xml=$(printf '%s' "$app_dir/controller" | xml_escape)
    log_xml=$(printf '%s' "$log_dir" | xml_escape)
    cat > "$plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>$label</string>
<key>ProgramArguments</key><array>
<string>$app_xml</string><string>--addr</string><string>$BOARD_IP:5000</string><string>--quiet</string>
</array>
<key>RunAtLoad</key><true/>
<key>KeepAlive</key><true/>
<key>ThrottleInterval</key><integer>5</integer>
<key>ProcessType</key><string>Interactive</string>
<key>StandardOutPath</key><string>$log_xml/controller.log</string>
<key>StandardErrorPath</key><string>$log_xml/controller-error.log</string>
</dict></plist>
PLIST
    plutil -lint "$plist"
    if launchctl print "$domain/$label" >/dev/null 2>&1; then
      launchctl bootout "$domain/$label"
    fi
    launchctl bootstrap "$domain" "$plist"
    ;;
  start)
    if launchctl print "$domain/$label" >/dev/null 2>&1; then
      launchctl kickstart "$domain/$label"
    else
      launchctl bootstrap "$domain" "$plist"
    fi
    ;;
  calibrate) launchctl kill SIGUSR2 "$domain/$label" ;;
  stop) launchctl bootout "$domain/$label" ;;
  status) launchctl print "$domain/$label" ;;
  *) echo 'Usage: controller-service.sh install|start|stop|status|calibrate' >&2; exit 1 ;;
esac
