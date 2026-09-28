# Medienverwaltung

## Aufgabe des Windows-Tools

Das Windows-Tool verwaltet eine gemeinsame Bibliothek eigener Medien. Es kann Dateien hinzufügen, eine Vorschau anzeigen und eine Datei als Standardmedium, Boot-Splash oder Shutdown-Medium auf den Pi übertragen. Eine Datei darf mehreren Rollen zugeordnet sein. Solange sie in mindestens einer Rolle aktiv ist, kann sie lokal erst nach Wahl eines Ersatzes gelöscht werden. Nach einem Upload meldet der Pi Formatfehler und Speicherprobleme sichtbar an Windows zurück.

Der Pi hält das zuletzt aktivierte Standardmedium lokal vor. Damit erscheint es bereits beim Pi-Start, bevor Windows erreichbar ist. Ein neues Medium wird zunächst vollständig übertragen und geprüft; erst danach ersetzt es das aktive Medium. Bei fehlgeschlagenem Upload bleibt das bisherige Standardmedium erhalten.

## Start ohne Windows

Die ausgewählte Datei und die Information, welche Datei aktiv ist, liegen dauerhaft auf dem Pi. Das Anzeigeprogramm startet beim Booten automatisch und lädt dieses Medium aus lokalem Speicher; es wartet dafür weder auf Netzwerk noch auf LaunchBox. Ein MP4 oder eine Animation beginnt in Endlosschleife, sobald die grafische Ausgabe bereit ist. Wenn die aktive Datei beschädigt oder nicht lesbar ist, erscheint ein mitgeliefertes lokales Ersatzbild.

Das statische Boot-Bild liegt ebenfalls dauerhaft auf dem Pi. Der Framebuffer zeigt es, bis der X11-Kiosk die Ausgabe übernimmt. Der Kernelstart wird mit den dokumentierten `cmdline.txt`-Optionen weitgehend ausgeblendet. Video und Animation starten erst mit Chromium; der frühe Splash akzeptiert deshalb nur PNG oder JPEG.

Das Shutdown-Medium wird vor `systemctl poweroff` im bereits laufenden Kiosk gezeigt. Ein H.264-MP4 spielt einmal ab. Die ermittelte Videodauer plus eine kurze Reserve bestimmt die Wartezeit, begrenzt auf 30 Sekunden. Bei einem direkten Shutdown am Pi sorgt ein eigener systemd-Dienst für denselben Ablauf.

## Formate der ersten Version

| Endung | Verwendung | Hinweise |
| --- | --- | --- |
| `.jpg`, `.jpeg` | Foto, statisches Logo, Boot-Splash | Keine Transparenz |
| `.png` | Statisches Logo, Boot-Splash | Transparenz möglich |
| `.gif` | Kurze Animation | Auflösung und Bildrate für Pi 3 B+ begrenzen |
| `.webp` | Statisches Logo | Animiertes WebP erst nach Test am Pi freigeben |
| `.mp4` | Video in Endlosschleife | H.264-Videostream; Audio wird ignoriert |

Eine Dateiendung allein genügt nicht: Das Tool prüft den tatsächlichen Medientyp, bei MP4 auch den Videocodec, und meldet nicht unterstützte Dateien vor der Aktivierung. Im Prototyp gilt ein festes Limit von 20 MB pro Datei. Grenzen für Auflösung und Animationsdauer werden nach dem Gerätetest festgelegt. Der Zielbildschirm hat 800 × 480 Pixel; Medien werden proportional eingepasst, ohne standardmäßig etwas abzuschneiden. Videos werden beim Wechsel zu einem Spiel gestoppt und beim Rücksprung zum Standardmedium neu gestartet.

APNG kann später ergänzt werden, falls animierte Transparenz gebraucht wird. SVG kann bei Bedarf beim Import in PNG umgewandelt werden. Für die erste Version sind zusätzliche Video-Container und H.265/VP9 nicht vorgesehen, da sie auf dem Pi 3 B+ keinen Vorteil für diesen Bildschirm bieten.

## Persistenz und Schutz der SD-Karte

Ein optionales schreibgeschütztes Overlay-Dateisystem verwirft gewöhnliche Änderungen beim Neustart. Für vom Windows-Tool hochgeladene Standardmedien braucht der Pi deshalb einen ausdrücklich persistenten Speicherort, beispielsweise eine separate beschreibbare Datenpartition. Der Installationsprozess muss diesen Ort einrichten und prüfen, bevor Uploads freigegeben werden. Medienwechsel erfolgen atomar: neue Datei schreiben, prüfen, dann Verweis auf das aktive Medium ändern.

## Offene Gerätetests

- Vorschau und Endlosschleife für GIF und H.264-MP4 auf dem tatsächlichen Pi 3 B+
- Verhalten bei Touch-Eingaben während einer Animation
- Upload und erneuter Pi-Start mit aktivem schreibgeschütztem Overlay
- Rückfall auf das bisherige Standardmedium nach abgebrochenem Upload
