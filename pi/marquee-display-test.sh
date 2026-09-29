#!/bin/sh
set -eu

ACTION="${1:-status}"
KIOSK_USER="${MARQUEE_PI_USER:-pi}"
DISPLAY_NAME="${MARQUEE_PI_DISPLAY:-DSI-1}"
XAUTHORITY_FILE="/home/$KIOSK_USER/.Xauthority"

has_dsi() {
    find /sys/class/drm -maxdepth 1 -name 'card*-DSI-*' -print -quit 2>/dev/null | grep -q .
}

xrun() {
    runuser -u "$KIOSK_USER" -- env DISPLAY=:0 XAUTHORITY="$XAUTHORITY_FILE" "$@"
}

status() {
    echo "Marquee-Pi display status"
    echo "Kernel: $(uname -r)"
    printf "DSI: "
    if has_dsi; then echo "detected"; else echo "missing"; fi
    printf "Firmware power: "
    vcgencmd display_power 2>/dev/null || echo "unavailable"
    printf "Services: "
    systemctl is-active marquee-pi-api marquee-pi-kiosk 2>/dev/null | paste -sd ' ' -
    echo "DRM connectors:"
    find /sys/class/drm -maxdepth 1 -name 'card*-*' -printf '  %f' -exec sh -c 'test -r "$1/status" && printf " (%s)" "$(cat "$1/status")"; echo' sh {} \;
    echo "Input devices:"
    for event in /sys/class/input/event*/device/name; do
        test -r "$event" && printf "  %s\n" "$(cat "$event")"
    done
    if xrun xrandr --current >/dev/null 2>&1; then
        echo "X11 outputs:"
        xrun xrandr --current | sed -n '/ connected/p'
    else
        echo "X11: unavailable"
    fi
}

require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "This action requires root." >&2
        exit 1
    fi
}

blink() {
    require_root
    if ! has_dsi; then
        echo "DSI is missing; a backlight test is not possible." >&2
        exit 2
    fi
    echo "Display off for three seconds..."
    vcgencmd display_power 0 >/dev/null
    sleep 3
    vcgencmd display_power 1 >/dev/null
    echo "Display on. The backlight should have visibly changed."
}

reset_display() {
    require_root
    if ! has_dsi; then
        echo "DSI is missing. Rebooting the Pi once while display power remains on..."
        systemctl reboot
        exit 0
    fi
    vcgencmd display_power 0 >/dev/null 2>&1 || true
    sleep 2
    vcgencmd display_power 1 >/dev/null 2>&1 || true
    if xrun xrandr --query 2>/dev/null | grep -q "^$DISPLAY_NAME connected"; then
        xrun xrandr --output "$DISPLAY_NAME" --off || true
        sleep 1
        xrun xrandr --output "$DISPLAY_NAME" --auto --primary || true
    fi
    systemctl restart marquee-pi-kiosk
    echo "Display output and kiosk restarted."
    status
}

case "$ACTION" in
    status) status ;;
    blink) blink ;;
    reset) reset_display ;;
    *) echo "Usage: $0 [status|blink|reset]" >&2; exit 64 ;;
esac
