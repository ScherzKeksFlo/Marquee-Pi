#!/bin/sh
# Start marquee-pi-configure-display (installed to /usr/sbin by the package).
exec python3 /opt/marquee-pi/configure_display.py "$@"
