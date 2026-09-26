# Standardmedium und Medienverwaltung

## Aufgabe des Windows-Tools

Das Windows-Tool verwaltet eine Bibliothek eigener Standardmedien. Es kann Dateien hinzufügen, eine Vorschau anzeigen, das aktive Standardmedium auswählen, dieses auf den Pi übertragen und nicht mehr benötigte Einträge löschen. Das aktive Medium darf erst nach Wahl eines Ersatzes gelöscht werden. Nach einem Upload meldet der Pi Formatfehler und Speicherprobleme sichtbar an Windows zurück.

Der Pi hält das zuletzt aktivierte Standardmedium lokal vor. Damit erscheint es bereits beim Pi-Start, bevor Windows erreichbar ist. Ein neues Medium wird zunächst vollständig übertragen und geprüft; erst danach ersetzt es das aktive Medium. Bei fehlgeschlagenem Upload bleibt das bisherige Standardmedium erhalten.

## Formate der ersten Version

| Endung | Verwendung | Hinweise |
| --- | --- | --- |
| `.jpg`, `.jpeg` | Foto oder statisches Logo | Keine Transparenz |
| `.png` | Statisches Logo | Transparenz möglich |
| `.gif` | Kurze Animation | Auflösung und Bildrate für Pi 3 B+ begrenzen |
| `.webp` | Statisches Logo | Animiertes WebP erst nach Test am Pi freigeben |
| `.mp4` | Video in Endlosschleife | H.264-Videostream; Audio wird ignoriert |

Eine Dateiendung allein genügt nicht: Das Tool prüft den tatsächlichen Medientyp, bei MP4 auch den Videocodec, und meldet nicht unterstützte Dateien vor der Aktivierung. Dateigröße, Auflösung und Animationsdauer erhalten dokumentierte und konfigurierbare Grenzen. Der Zielbildschirm hat 800 × 480 Pixel; Medien werden proportional eingepasst, ohne standardmäßig etwas abzuschneiden. Videos werden beim Wechsel zu einem Spiel gestoppt und beim Rücksprung zum Standardmedium neu gestartet.

APNG kann später ergänzt werden, falls animierte Transparenz gebraucht wird. SVG kann bei Bedarf beim Import in PNG umgewandelt werden. Für die erste Version sind zusätzliche Video-Container und H.265/VP9 nicht vorgesehen, da sie auf dem Pi 3 B+ keinen Vorteil für diesen Bildschirm bieten.

## Persistenz und Schutz der SD-Karte

Ein optionales schreibgeschütztes Overlay-Dateisystem verwirft gewöhnliche Änderungen beim Neustart. Für vom Windows-Tool hochgeladene Standardmedien braucht der Pi deshalb einen ausdrücklich persistenten Speicherort, beispielsweise eine separate beschreibbare Datenpartition. Der Installationsprozess muss diesen Ort einrichten und prüfen, bevor Uploads freigegeben werden. Medienwechsel erfolgen atomar: neue Datei schreiben, prüfen, dann Verweis auf das aktive Medium ändern.

## Offene Gerätetests

- Vorschau und Endlosschleife für GIF und H.264-MP4 auf dem tatsächlichen Pi 3 B+
- Verhalten bei Touch-Eingaben während einer Animation
- Upload und erneuter Pi-Start mit aktivem schreibgeschütztem Overlay
- Rückfall auf das bisherige Standardmedium nach abgebrochenem Upload
