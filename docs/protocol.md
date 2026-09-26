# Netzwerkprotokoll (Entwurf)

Das Windows-Programm kommuniziert über die direkte Netzwerkverbindung mit dem Pi. Die erste Implementierung soll eine kleine HTTP-API mit Versionspräfix `/v1` und einem lokal konfigurierten gemeinsamen Zugriffstoken verwenden. Der Pi akzeptiert nur die konfigurierte Windows-Adresse. Zugangstoken und echte IP-Adressen stehen ausschließlich in lokalen Konfigurationsdateien, die Git ignoriert.

## Geplante Operationen

| Operation | Zweck |
| --- | --- |
| `GET /v1/status` | Version, Zustand, Spielkennung und Bereitschaft lesen |
| `POST /v1/game` | Spielkennung und verfügbare Bildtypen setzen |
| `POST /v1/default` | Auf Standardanzeige zurücksetzen |
| `POST /v1/reload` | Anzeige und Konfiguration neu laden |
| `POST /v1/reboot` | Pi neu starten |
| `POST /v1/shutdown` | Pi herunterfahren |

Spielgrafiken müssen vom Windows-Rechner zum Pi übertragen werden; Windows-Dateipfade allein sind auf dem Pi nicht lesbar. Das endgültige Medienformat, Größenlimit, Caching und Fehlerformat werden zusammen mit der Implementierung festgelegt. Befehle zum Neustart oder Herunterfahren müssen eine eindeutige Bestätigung liefern, bevor die Verbindung endet.

## Wiederverbindung

Windows sendet nach einem Verbindungsaufbau den aktuellen Anzeigezustand erneut. Der Pi zeigt ohne Verbindung sein lokales Standardmedium. Der Verlust einer Verbindung löst keinen Pi-Shutdown aus, damit Windows-Neustarts möglich bleiben.
