# Netzwerkprotokoll v1

Der Pi stellt eine HTTP-API auf dem konfigurierten Port bereit. Windows sendet bei jedem `/v1`-Aufruf den Header `X-Arcade-Token`. Ein optionales `allowed_client_ips` begrenzt zusätzlich die Windows-Adressen; eine leere Liste bedeutet keine IP-Filterung. Die Vollbildseite unter `/ui/` ist nur von `127.0.0.1` bzw. `::1` erreichbar. Die direkte Verbindung sollte nicht ins öffentliche Netz weitergeleitet werden.

## Operationen

| Aufruf | Zweck |
| --- | --- |
| `GET /v1/status` | Programmversion, Spiel und aktives Standardmedium lesen |
| `POST /v1/game` | Spiel und Artwork setzen |
| `POST /v1/heartbeat` | Laufendes Spiel während der Windows-Verbindung bestätigen |
| `POST /v1/default` | Spiel beenden und Standardmedium zeigen |
| `POST /v1/default-media` | Standardmedium hochladen und nach Prüfung aktivieren |
| `POST /v1/reload` | Aktives Medium erneut laden und Browseranzeige aktualisieren |
| `POST /v1/reboot` | Pi-Neustart anfordern |
| `POST /v1/shutdown` | Pi-Shutdown anfordern |

`POST /v1/game` verwendet JSON. `title` ist erforderlich. `marquee` und `controls` sind optional und enthalten jeweils `extension` und `base64`. Dateipfade werden nicht übertragen, weil Windows-Pfade auf dem Pi nicht verfügbar sind. Bilder liegen für die laufende Sitzung im RAM. Wenn 60 Sekunden lang kein Heartbeat kommt, kehrt der Pi zum lokalen Standardmedium zurück. Der Timeout ist konfigurierbar.

`POST /v1/default-media` verwendet die Rohbytes der Datei. `X-File-Name` liefert die Endung. Die API prüft Typ und Größenlimit (derzeit 20 MiB); für MP4 muss `ffprobe` einen H.264-Videostream nachweisen. Die Datei wird erst nach vollständiger Prüfung dauerhaft aktiviert. Ein fehlgeschlagener Upload lässt das bisherige Medium aktiv.

Erfolgreiche Änderungen liefern JSON mit `ok: true`. Fehler liefern einen HTTP-Status und ein JSON-`error`. Neustart und Shutdown bestätigen den angenommenen Befehl, bevor die Pi-Verbindung endet.

## Wiederverbindung

Das Windows-Tool merkt sich das laufende Spiel und sendet es nach einer wiederhergestellten Pi-Verbindung erneut. Der Pi zeigt ohne Verbindung sein lokal gespeichertes Standardmedium. Ein Netzwerkverlust löst keinen Pi-Shutdown aus, damit Windows neu gestartet werden kann.
