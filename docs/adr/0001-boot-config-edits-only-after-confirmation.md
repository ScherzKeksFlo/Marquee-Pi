# Display setup may edit the boot configuration, but only after confirmation

Until now the Debian package never touched `/boot/firmware` (`packaging/deb/README.md` says so); the DSI settings were manual steps in `pi/INSTALL.md`. With variable displays some screens need a `video=` entry in `cmdline.txt` (or an overlay in `config.txt`) and most users get those edits wrong. `marquee-pi-configure-display` may therefore change the boot configuration, but only after the user confirmed it, with a backup and a way back (`--revert`); the user can instead have the exact changes written to a text file and apply them by hand.

Non-interactive runs (installation without a terminal, upgrades) and HDMI in automatic mode never change the boot configuration. A wrong `video=` entry can leave the display blank, which is why the confirmation, the backup and the text-file alternative exist.

## Considered options

- Keep the boot configuration manual: rejected, the variety of displays makes the manual steps too error-prone.
- Change it silently in `postinst`: rejected, a bad edit cannot be undone without a card reader.

## Consequences

The statement "the boot configuration is deliberately not changed by the package" now reads "not changed unless the user confirms it in `marquee-pi-configure-display`". Stage 1 (HDMI and DSI) only produces `cmdline.txt` changes; editing `config.txt` is allowed by this decision but no stage 1 display needs it yet.
