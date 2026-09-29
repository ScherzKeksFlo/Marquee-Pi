#pragma once
#include <string>

// UI texts of the Windows app in English and German. Each entry is
// X(identifier, English, German); the same list builds the enum and the table.
// Entries of one group (tabs, gestures, actions) must stay consecutive because the
// settings window indexes them as base + i.
#define MARQUEE_STRINGS(X) \
    /* tray */ \
    X(StatusChecking, L"Pi: checking connection", L"Pi: Verbindung wird geprüft") \
    X(TipConnected, L"Marquee-Pi - Pi connected", L"Marquee-Pi - Pi verbunden") \
    X(TipDisconnected, L"Marquee-Pi - Pi unreachable", L"Marquee-Pi - Pi nicht erreichbar") \
    X(StatusConnected, L"Pi connected - ", L"Pi verbunden - ") \
    X(StatusUnreachable, L"Pi unreachable", L"Pi nicht erreichbar") \
    X(DefaultMediaTitle, L"Default media", L"Standardmedium") \
    X(MenuMedia, L"Manage media…", L"Medien verwalten…") \
    X(MenuDefault, L"Show default logo", L"Standardlogo anzeigen") \
    X(MenuReload, L"Reload display", L"Anzeige neu laden") \
    X(MenuReboot, L"Restart Pi", L"Pi neu starten") \
    X(MenuShutdown, L"Shut down Pi", L"Pi herunterfahren") \
    X(MenuSettings, L"Settings…", L"Einstellungen…") \
    X(MenuOpenIni, L"Open INI file", L"INI-Datei öffnen") \
    X(MenuReloadIni, L"Reload settings", L"Einstellungen neu laden") \
    X(MenuExit, L"Exit", L"Programm beenden") \
    X(ConfirmReboot, L"Really restart the Pi?", L"Pi wirklich neu starten?") \
    X(ConfirmShutdown, L"Really shut down the Pi? It only starts again after a power cycle.", \
      L"Pi wirklich herunterfahren? Er startet erst nach einem neuen Stromzyklus.") \
    X(TitleReloadIni, L"Reload settings", L"Einstellungen neu laden") \
    X(StartFailed, L"Marquee-Pi could not be started.", L"Marquee-Pi konnte nicht gestartet werden.") \
    X(TitleStartupError, L"Marquee-Pi startup error", L"Marquee-Pi Startfehler") \
    /* settings window */ \
    X(WindowTitle, L"Marquee-Pi - Settings", L"Marquee-Pi - Einstellungen") \
    X(TitleSettings, L"Settings", L"Einstellungen") \
    X(SettingsOpenFailed, L"The settings window could not be opened.", \
      L"Das Einstellungsfenster konnte nicht geöffnet werden.") \
    X(TabConnection, L"Connection", L"Verbindung") \
    X(TabGestures, L"Gestures", L"Gesten") \
    X(TabMedia, L"Media", L"Medien") \
    X(TabRetroArch, L"RetroArch", L"RetroArch") \
    X(TabGeneral, L"General", L"Allgemein") \
    X(Save, L"Save", L"Speichern") \
    X(Cancel, L"Cancel", L"Abbrechen") \
    X(PiAddress, L"Pi address", L"Pi-Adresse") \
    X(AccessToken, L"Access token", L"Zugriffstoken") \
    X(ShowToken, L"Show token", L"Token anzeigen") \
    X(HelpToken, L"Help: create token", L"Hilfe: Token erstellen") \
    X(TokenHelpTitle, L"Create access token", L"Zugriffstoken erstellen") \
    X(TokenHelp, \
      L"1. Log in to the Raspberry Pi via SSH.\n\n" \
      L"2. Generate a random token: openssl rand -hex 32\n\n" \
      L"3. Enter the 64 characters in /etc/arcade-pi-display/config.json as the value of " \
      L"\"token\", replacing the placeholder.\n\n" \
      L"4. sudo systemctl restart arcade-pi-display.service\n\n" \
      L"5. Enter exactly the same token here and save. Do not publish it.", \
      L"1. Per SSH am Raspberry Pi anmelden.\n\n" \
      L"2. Zufälligen Token erzeugen: openssl rand -hex 32\n\n" \
      L"3. Die 64 Zeichen in /etc/arcade-pi-display/config.json als Wert von " \
      L"\"token\" eintragen und den Platzhalter ersetzen.\n\n" \
      L"4. sudo systemctl restart arcade-pi-display.service\n\n" \
      L"5. Genau denselben Token hier eintragen und speichern. Nicht veröffentlichen.") \
    /* gestures */ \
    X(GesturesIntro, \
      L"Choose what happens on the Pi display for each gesture. The touch menu (view, brightness, " \
      L"status, restart) opens on long press by default.", \
      L"Legt fest, was am Pi-Display bei welcher Geste passiert. Das Touchmenü (Ansicht, " \
      L"Helligkeit, Status, Neustart) öffnet sich standardmäßig durch langen Druck.") \
    X(GestureLongPress, L"Long press", L"Langer Druck") \
    X(GestureDown, L"Top to bottom", L"Oben nach unten") \
    X(GestureUp, L"Bottom to top", L"Unten nach oben") \
    X(GestureRight, L"Left to right", L"Links nach rechts") \
    X(GestureLeft, L"Right to left", L"Rechts nach links") \
    X(ActionNone, L"No action", L"Keine Aktion") \
    X(ActionMarquee, L"Show marquee", L"Marquee anzeigen") \
    X(ActionBoxArt, L"Show box art", L"Box Art anzeigen") \
    X(ActionLogo, L"Show LaunchBox logo", L"LaunchBox-Logo anzeigen") \
    X(ActionControls, L"Show controls layout", L"Steuerungsbelegung anzeigen") \
    X(ActionDefault, L"Show default animation", L"Standardanimation anzeigen") \
    X(ActionRetroArchMenu, L"Open RetroArch menu", L"RetroArch-Menü öffnen") \
    X(ActionTouchMenu, L"Open touch menu on Pi", L"Touchmenü am Pi öffnen") \
    X(GesturesWarning, \
      L"Without an assigned touch menu, brightness and restart can no longer be reached on the Pi.", \
      L"Ohne zugeordnetes Touchmenü lassen sich Helligkeit und Neustart am Pi nicht mehr aufrufen.") \
    X(TouchMenuUnassigned, \
      L"No gesture opens the touch menu. Brightness, status, restart and shutdown can then no longer " \
      L"be reached on the Pi.\n\nSave anyway?", \
      L"Dem Touchmenü ist keine Geste zugeordnet. Helligkeit, Status sowie Neustart " \
      L"und Herunterfahren lassen sich am Pi dann nicht mehr aufrufen.\n\nTrotzdem speichern?") \
    /* media */ \
    X(MediaAdd, L"Add", L"Hinzufügen") \
    X(MediaPreview, L"Preview", L"Vorschau") \
    X(MediaRemove, L"Remove", L"Entfernen") \
    X(MediaUseDefault, L"Use as default", L"Als Standard") \
    X(MediaUseBoot, L"Use as boot splash", L"Als Boot-Splash") \
    X(MediaUseShutdown, L"Use as shutdown media", L"Als Shutdown-Medium") \
    X(MediaNoPreview, L"no preview", L"kein Vorschaubild") \
    X(MediaVideo, L"Video", L"Video") \
    X(MediaImage, L"Image", L"Bild") \
    X(RoleDefault, L"Default media", L"Standardmedium") \
    X(RoleBoot, L"Boot splash", L"Boot-Splash") \
    X(RoleShutdown, L"Shutdown media", L"Shutdown-Medium") \
    X(FilterSupported, L"Supported media", L"Unterstützte Medien") \
    X(FilterAll, L"All files", L"Alle Dateien") \
    X(DefaultActive, L"The default media is now active on the Pi.", L"Das Standardmedium ist auf dem Pi aktiv.") \
    X(BootSaved, L"The boot splash is stored on the Pi and takes effect at the next start.", \
      L"Der Boot-Splash ist auf dem Pi gespeichert und gilt ab dem nächsten Start.") \
    X(ShutdownSaved, L"The shutdown media is stored on the Pi.", L"Das Shutdown-Medium ist auf dem Pi gespeichert.") \
    X(UploadFailed, L"Upload failed", L"Upload fehlgeschlagen") \
    X(ReplaceRolesFirst, L"Please replace the media in all roles it is used for first.", \
      L"Bitte das Medium zuerst in allen verwendeten Rollen ersetzen.") \
    X(RemoveFailed, L"The file could not be removed.", L"Datei konnte nicht entfernt werden.") \
    /* RetroArch */ \
    X(Hotkey, L"Hotkey", L"Tastenkombination") \
    X(HotkeyExamples, L"Examples: F1, Ctrl+F1, Shift+F1 (only in the active RetroArch window).", \
      L"Beispiele: F1, Ctrl+F1, Shift+F1 (nur im aktiven RetroArch-Fenster).") \
    X(ControlMethod, L"Control method", L"Steuerungsart") \
    X(ModeKeyboard, L"Send keyboard shortcut", L"Tastaturkürzel senden") \
    X(ModeNetwork, L"Local RetroArch network command", L"Lokaler RetroArch-Netzwerkbefehl") \
    X(NetworkPort, L"Network port", L"Netzwerk-Port") \
    X(NetworkHint, L"For network mode: enable RetroArch > Settings > Network > Network commands.", \
      L"Für Netzwerkmodus: RetroArch > Einstellungen > Netzwerk > Netzwerkbefehle aktivieren.") \
    X(InvalidPort, L"Invalid RetroArch network port.", L"Ungültiger RetroArch-Netzwerk-Port.") \
    X(PortRange, L"The RetroArch network port must be between 1 and 65535.", \
      L"RetroArch-Netzwerk-Port muss zwischen 1 und 65535 liegen.") \
    X(InvalidHotkey, L"Invalid hotkey. Example: Ctrl+Shift+F1.", \
      L"Ungültige Tastenkombination. Beispiel: Ctrl+Shift+F1.") \
    X(NotConfigured, L"Please enter an HTTP Pi address and a token with at least 24 characters.", \
      L"Bitte HTTP-Pi-Adresse und Token mit mindestens 24 Zeichen eingeben.") \
    /* general */ \
    X(Autostart, L"Start with Windows", L"Mit Windows starten") \
    X(OpenIni, L"Open INI file", L"INI-Datei öffnen") \
    X(IniHint, L"All settings in a text editor; afterwards choose \"Reload settings\" in the tray menu.", \
      L"Alle Einstellungen im Texteditor; danach im Tray \"Einstellungen neu laden\".") \
    X(Language, L"Language", L"Sprache") \
    X(LanguageAuto, L"Automatic (Windows language)", L"Automatisch (Windows-Sprache)") \
    X(LanguageEnglish, L"English", L"English") \
    X(LanguageGerman, L"Deutsch", L"Deutsch") \
    X(LanguageHint, L"Also sets the language of the touch menu on the Pi display.", \
      L"Legt auch die Sprache des Touchmenüs am Pi-Display fest.")

enum class Str : int {
#define MARQUEE_STRING_ID(id, en, de) id,
    MARQUEE_STRINGS(MARQUEE_STRING_ID)
#undef MARQUEE_STRING_ID
    Count
};

// "auto" follows the Windows display language; "en" and "de" are explicit.
// Returns the language actually used: "en" or "de".
std::string resolveLanguage(const std::string& setting);
// Selects the table used by tr(); anything other than "de" selects English.
void setUiLanguage(const std::string& resolved);
const wchar_t* tr(Str id);
// Consecutive entries of a group, e.g. trAt(Str::TabConnection, 2).
inline const wchar_t* trAt(Str first, int offset) { return tr(Str(int(first) + offset)); }
