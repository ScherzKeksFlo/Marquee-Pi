# Touchbedienung

Ein kurzer Tipp schaltet während eines Spiels zwischen Marquee und Steuerungsbelegung um, sofern eine Steuerungsgrafik vorhanden ist. Die vier Wischrichtungen werden im Windows-Infobereich unter **Wischgesten einrichten…** unabhängig konfiguriert.

| Geste | Fingerbewegung |
| --- | --- |
| `swipe-down` | Oben nach unten |
| `swipe-up` | Unten nach oben |
| `swipe-right` | Links nach rechts |
| `swipe-left` | Rechts nach links |

## Verfügbare Aktionen

- **Keine Aktion:** Standardbelegung aller vier Gesten.
- **Marquee anzeigen:** Spiel-Marquee, sonst das im Spielstart gewählte Banner.
- **Box Art anzeigen:** Vorderseite der Box aus LaunchBox.
- **LaunchBox-Logo anzeigen:** Clear Logo des Spiels aus LaunchBox.
- **Steuerungsbelegung anzeigen:** vorhandene Arcade-Steuerungsgrafik.
- **Standardanimation anzeigen:** auf dem Pi gespeichertes Standardmedium, auch während eines Spiels.
- **RetroArch-Menü öffnen:** sendet die im Windows-Tool eingestellte Tastenkombination an das gerade aktive RetroArch-Fenster.

Fehlt die gewählte Spielgrafik, erscheint stattdessen das Marquee; fehlt auch dieses, zeigt der Pi das Standardmedium mit Spieltitel. Die RetroArch-Aktion wirkt nur während eines aktiven Spiels und nur wenn RetroArch im Vordergrund ist. Die Tastenkombination lässt sich als Tastaturnamen eingeben, etwa `F1`, `Ctrl+F1`, `Shift+F1` oder `Ctrl+Shift+F1`; Gamepad-Tastenkombinationen werden nicht simuliert.

Die Windows-App speichert die Zuordnung pro Benutzer und überträgt sie an den Pi. Der Pi speichert sie ebenfalls, damit Bildaktionen nach einem Pi-Neustart bereitstehen. Nach einem Windows- oder Pi-Neustart synchronisiert die App die Konfiguration erneut. RetroArch-Aktionen benötigen die laufende Windows-App.

## Erkennung

- Pro Berührung wird höchstens ein Ereignis beim Loslassen erzeugt.
- Die längere Bewegungsachse bestimmt die Wischrichtung; kleine seitliche Abweichungen sind erlaubt.
- Eine Bewegung oberhalb der Tippgrenze löst keinen Tippwechsel aus.
- Nicht eindeutige, abgebrochene und gleichzeitige Mehrfinger-Eingaben bleiben ohne Aktion.
- Bei gedrehtem Display gelten die Richtungen aus Sicht des Benutzers; die Displayausrichtung wird am Pi eingestellt.

## Abnahme am Gerät

1. Jede Richtung separat einer anderen Bildaktion zuweisen und die passende Ansicht prüfen.
2. Geste ohne passende Spielgrafik prüfen: Marquee bzw. Standardmedium erscheint.
3. RetroArch-Menütaste konfigurieren, ein RetroArch-Spiel starten und Geste prüfen.
4. RetroArch verlassen und dieselbe Geste prüfen: Es wird keine Taste an Big Box gesendet.
5. Pi und Windows-App neu starten und prüfen, ob die Einstellungen erhalten bleiben.
