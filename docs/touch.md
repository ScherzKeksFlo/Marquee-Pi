# Touch control

A short tap switches between the marquee and the controls layout during a game, provided controls artwork is available. A long press opens the touch menu (see below). The four swipe directions and the long press are configured independently in the Windows notification area under **Settings… → Gestures**.

| Gesture | Finger movement |
| --- | --- |
| `swipe-down` | Top to bottom |
| `swipe-up` | Bottom to top |
| `swipe-right` | Left to right |
| `swipe-left` | Right to left |
| `long-press` | Hold one finger still for about 0.8 s |

## Available actions

- **No action:** Default assignment of the four swipes.
- **Open touch menu on Pi:** Opens the touch menu described below. This is the default assignment of the long press and can be given to any gesture.
- **Show marquee:** Game marquee, otherwise the banner chosen at game start.
- **Show box art:** Front of the box from LaunchBox.
- **Show LaunchBox logo:** Clear logo of the game from LaunchBox.
- **Show controls layout:** Existing arcade controls artwork.
- **Show default animation:** Default media stored on the Pi, even during a game.
- **Open RetroArch menu:** Sends the key combination configured in the Windows tool to the currently active RetroArch window.

If the selected game artwork is missing, the marquee appears instead; if that is also missing, the Pi shows the default media with the game title. The RetroArch action works only during an active game and only when RetroArch is in the foreground. The key combination can be entered as keyboard names, such as `F1`, `Ctrl+F1`, `Shift+F1` or `Ctrl+Shift+F1`; gamepad button combinations are not simulated.

The Windows app stores the assignment per user and transfers it to the Pi. The Pi also stores it so that image actions are available after a Pi restart. After a Windows or Pi restart, the app synchronizes the configuration again. RetroArch actions require the Windows app to be running.

## Touch menu

The touch menu is an overlay on the Pi display with these parts:

- **View:** Switch between Marquee, Box Art, Logo, Controls and Default. Views without artwork are greyed out, as is everything while no game runs.
- **Brightness:** Steps of 10 percent, never below 5 percent so the display cannot go black. The value survives a Pi restart.
- **Status:** Connection to the arcade PC, IP addresses of the Pi, running game and program version.
- **System …:** A separate page with **Restart** and **Shut down**, each behind a confirmation. It uses the same polkit-checked path as the API and requires `power_commands_enabled`.

The menu closes after 20 seconds without input. If no gesture is assigned to **Open touch menu on Pi**, brightness and restart can no longer be reached on the Pi; the Windows tool warns before saving such a configuration. The menu texts follow the language chosen in the Windows tool (**Settings… → General → Language**); English is used until Windows has reported a language.

## Detection

- At most one event per touch is generated, on release.
- The longer axis of movement determines the swipe direction; small lateral deviations are allowed.
- A movement above the tap threshold does not trigger a tap switch.
- A long press needs one finger that stays within the tap distance for 0.8 s; its release then produces no tap. It fires once per touch and only if a real action is assigned.
- Ambiguous, aborted and simultaneous multi-finger inputs result in no action.
- With a rotated display, the directions apply from the user's point of view; the rotation is set in the display profile and the touch coordinates are rotated with the picture.

## Displays without touch

If the display profile has no touch device (a *view-only display*), the Pi shows the marquee and ignores every touch and click: no gestures, no tap switch, no touch menu. Brightness and restart are then only available from the Windows app. The Windows gestures page says so and disables the assignments.

## Other screen sizes

The menu scales with the display: the short side of the screen is 480 CSS pixels by default (`scale` in the display profile), which also scales the gesture distances. On displays under 400 CSS pixels high the menu is denser, and on portrait displays the five views wrap onto two rows.

## Acceptance on the device

1. Assign each direction separately to a different image action and check the matching view.
2. Check a gesture without matching game artwork: the marquee or default media appears.
3. Configure the RetroArch menu key, start a RetroArch game and check the gesture.
4. Exit RetroArch and check the same gesture: no key is sent to Big Box.
5. Restart the Pi and the Windows app and check whether the settings are retained.
6. Assign **Open touch menu on Pi** to a swipe and long press to another action; check that the menu opens only on the swipe.
7. Switch the language in the Windows tool and check that the menu texts change without restarting the Pi.
