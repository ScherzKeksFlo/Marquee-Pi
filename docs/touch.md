# Touchbedienung

## Erkannte Eingaben

| Ereignis | Bewegung auf dem angezeigten Bild | Aktion im ersten Stand |
| --- | --- | --- |
| `tap` | Kurze Berührung ohne nennenswerte Bewegung | Während eines Spiels Marquee/Control-Panel umschalten |
| `swipe-down` | Oben nach unten | Noch nicht zugewiesen |
| `swipe-up` | Unten nach oben | Noch nicht zugewiesen |
| `swipe-right` | Links nach rechts | Noch nicht zugewiesen |
| `swipe-left` | Rechts nach links | Noch nicht zugewiesen |

Die Richtung beschreibt die Fingerbewegung, nicht die Position, an der sie beginnt. Ein Wischen kann überall auf der aktiven Displayfläche starten. Die Aktionen für die vier Wischrichtungen werden später festgelegt und sollen dann konfigurierbar sein. Bis dahin ändern diese Gesten weder die Anzeige noch den Pi-Zustand.

## Erkennung

- Pro Berührung wird höchstens ein Ereignis erzeugt, und zwar erst beim Loslassen.
- Die längere Bewegungsachse bestimmt die Wischrichtung. Eine kleine seitliche Abweichung bleibt erlaubt.
- Mindeststrecke, maximale Dauer und zulässige Abweichung sind konfigurierbar. Ausgangswerte werden am echten 800 × 480 Touchdisplay erprobt.
- Eine Bewegung oberhalb der Tippgrenze darf den Tippwechsel nicht auslösen.
- Nicht eindeutige oder abgebrochene Eingaben bleiben ohne Aktion.
- Mehrere gleichzeitige Finger werden in der ersten Version ignoriert.
- Bei gedrehtem Display gelten die Richtungen aus Sicht des Benutzers. Dazu werden Touch-Koordinaten vor der Klassifikation an die Displayausrichtung angepasst.
- Spielstart, Spielende und Verbindungswechsel setzen einen laufenden, noch nicht abgeschlossenen Touch-Vorgang zurück.

## Abnahme am Gerät

1. Jede der vier Wischrichtungen wird mehrfach richtig erkannt.
2. Langsame und leicht diagonale Wischbewegungen werden ohne Fehlaktion getestet.
3. Ein Tipp wechselt die Ansicht nur bei aktivem Spiel mit Control-Panel-Grafik.
4. Wischen löst keinen Tippwechsel aus und nicht zugewiesene Gesten bleiben sichtbar folgenlos.
5. Der Test wird in der tatsächlichen Einbaulage des Displays wiederholt.
