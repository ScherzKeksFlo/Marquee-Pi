#pragma once
#include <cwchar>
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
    X(TabDashboard, L"Dashboard", L"Übersicht") \
    X(TabConnection, L"Connection", L"Verbindung") \
    X(TabMedia, L"Media", L"Medien") \
    X(TabGestures, L"Gestures", L"Gesten") \
    X(TabRetroArch, L"RetroArch", L"RetroArch") \
    X(TabGeneral, L"General", L"Allgemein") \
    X(TabLogs, L"Logs", L"Protokoll") \
    X(Save, L"Save", L"Speichern") \
    X(Cancel, L"Cancel", L"Abbrechen") \
    X(PiAddress, L"Pi address", L"Pi-Adresse") \
    X(AccessToken, L"Access token", L"Zugriffstoken") \
    X(ShowToken, L"Show token", L"Token anzeigen") \
    X(HelpToken, L"Help: create token", L"Hilfe: Token erstellen") \
    X(TokenHelpTitle, L"Create access token", L"Zugriffstoken erstellen") \
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
    /* status */ \
    X(StatusPiConnected, L"Pi connected", L"Pi verbunden") \
    X(StatusNotConfigured, L"Not configured", L"Nicht eingerichtet") \
    X(StatusNotConfiguredShort, L"Enter Pi address and token", L"Pi-Adresse und Token eingeben") \
    X(StatusRetrying, L"Retrying automatically", L"Versucht es automatisch erneut") \
    X(StatusTipNotConfigured, L"Pi: not configured", L"Pi: nicht eingerichtet") \
    X(StatusSubConnected, L"%ls · %d ms · Pi API v1", L"%ls · %d ms · Pi-API v1") \
    X(StatusSubNotConfigured, L"Enter the Pi address and the access token to connect.", \
      L"Pi-Adresse und Zugriffstoken eingeben, um zu verbinden.") \
    X(StatusSubUnreachable, \
      L"Last contact %ls · retrying automatically. Check the cable and that the Pi is powered.", \
      L"Letzter Kontakt %ls · neuer Versuch automatisch. Kabel und Stromversorgung des Pi prüfen.") \
    X(StatusSubNeverContacted, \
      L"No contact yet · retrying automatically. Check the cable and that the Pi is powered.", \
      L"Noch kein Kontakt · neuer Versuch automatisch. Kabel und Stromversorgung des Pi prüfen.") \
    X(SidebarFooter, L"v%ls · INSERT COIN", L"v%ls · INSERT COIN") \
    X(FootSaved, L"Settings are stored in settings.ini", L"Einstellungen liegen in settings.ini") \
    X(FootOffline, L"Changes are saved locally and sent when the Pi is reachable.", \
      L"Änderungen werden lokal gespeichert und gesendet, sobald der Pi erreichbar ist.") \
    X(DialogOk, L"OK", L"OK") \
    X(ConfirmRestartButton, L"Restart", L"Neu starten") \
    X(ConfirmShutdownButton, L"Shut down", L"Herunterfahren") \
    /* toasts */ \
    X(ToastSettingsSaved, L"Settings saved.", L"Einstellungen gespeichert.") \
    X(ToastPiUnreachable, L"Pi unreachable", L"Pi nicht erreichbar") \
    X(ToastSetupFirst, L"Set up the connection first.", L"Zuerst die Verbindung einrichten.") \
    X(ToastDefaultShown, L"Default logo shown on the Pi.", L"Standardlogo wird am Pi angezeigt.") \
    X(ToastReloaded, L"Display reloaded.", L"Anzeige neu geladen.") \
    X(ToastRestartSent, L"Restart command sent to the Pi.", L"Neustart-Befehl an den Pi gesendet.") \
    X(ToastShutdownSent, L"Shutdown command sent. The Pi shows the shutdown media.", \
      L"Herunterfahren-Befehl gesendet. Der Pi zeigt das Shutdown-Medium.") \
    X(ToastIniReloaded, L"Settings reloaded from settings.ini.", L"Einstellungen aus settings.ini neu geladen.") \
    X(ToastLanguageApplied, L"Language applied and sent to the Pi.", L"Sprache übernommen und an den Pi gesendet.") \
    X(ToastLanguageOffline, L"Language applied. The Pi will update when reachable.", \
      L"Sprache übernommen. Der Pi folgt, sobald er erreichbar ist.") \
    X(ToastImported, L"Imported %ls. Assign a role to send it to the Pi.", \
      L"%ls importiert. Eine Rolle zuweisen, um es an den Pi zu senden.") \
    X(ToastLogCopied, L"Log copied to clipboard.", L"Protokoll in die Zwischenablage kopiert.") \
    /* dashboard */ \
    X(DashSubtitle, L"Raspberry Pi 3 B+ · 800 × 480 touch", L"Raspberry Pi 3 B+ · 800 × 480 Touch") \
    X(FirstRunTitle, L"Connect your Pi to get started", L"Pi verbinden und loslegen") \
    X(FirstRunBody, \
      L"Please enter an HTTP Pi address and a token with at least 24 characters. The token must match the one in " \
      L"/etc/marquee-pi/config.json on the Pi.", \
      L"Bitte HTTP-Pi-Adresse und ein Token mit mindestens 24 Zeichen eingeben. Das Token muss mit dem in " \
      L"/etc/marquee-pi/config.json auf dem Pi übereinstimmen.") \
    X(FirstRunButton, L"Set up connection", L"Verbindung einrichten") \
    X(PanelNowShowing, L"Now showing", L"Aktuelle Anzeige") \
    X(PanelConnection, L"Connection", L"Verbindung") \
    X(PanelCurrentGame, L"Current game", L"Aktuelles Spiel") \
    X(PanelActiveRoles, L"Active roles on Pi", L"Aktive Rollen am Pi") \
    X(PanelQuickActions, L"Quick actions", L"Schnellaktionen") \
    X(PanelRecentEvents, L"Recent events", L"Letzte Ereignisse") \
    X(TagLive, L"LIVE", L"LIVE") \
    X(TagOffline, L"OFFLINE", L"OFFLINE") \
    X(NoSignal, L"No signal", L"Kein Signal") \
    X(CaptionMarquee, L"Marquee for %ls · %d × %d · tap on the Pi switches to the controls view", \
      L"Marquee für %ls · %d × %d · Tippen am Pi wechselt zur Steuerungsansicht") \
    X(CaptionMarqueeViewOnly, L"Marquee for %ls · %d × %d", L"Marquee für %ls · %d × %d") \
    X(DashSubtitleFormat, L"%ls · %d × %d · %ls", L"%ls · %d × %d · %ls") \
    X(DashSubtitleNoSize, L"%ls · %ls", L"%ls · %ls") \
    X(DisplayTouch, L"touch", L"Touch") \
    X(DisplayViewOnly, L"view only", L"nur Anzeige") \
    X(GesturesNoTouch, \
      L"This display has no touch, so gestures are not available here. The marquee is controlled from Windows.", \
      L"Dieses Display hat kein Touch, daher gibt es hier keine Gesten. Das Marquee wird von Windows aus gesteuert.") \
    X(CaptionDefaultMedia, L"Default media is showing · %ls", L"Standardmedium wird angezeigt · %ls") \
    X(CaptionOffline, L"Showing whatever the Pi last displayed. It keeps the default media locally.", \
      L"Zeigt, was der Pi zuletzt angezeigt hat. Das Standardmedium liegt lokal auf dem Pi.") \
    X(CaptionConnectFirst, L"Connect the Pi to see what it shows.", L"Pi verbinden, um die Anzeige zu sehen.") \
    X(LabelAddress, L"Address", L"Adresse") \
    X(LabelLatency, L"Latency", L"Latenz") \
    X(LabelPluginPipe, L"Plugin pipe", L"Plugin-Pipe") \
    X(PipeListening, L"MarqueePiGameEvents · listening", L"MarqueePiGameEvents · wartet") \
    X(PipeStopped, L"MarqueePiGameEvents · not running", L"MarqueePiGameEvents · nicht aktiv") \
    X(NoGameRunning, L"No game running", L"Kein Spiel aktiv") \
    X(WaitingForGame, L"Waiting for game events from LaunchBox", L"Wartet auf Spielereignisse von LaunchBox") \
    X(GameStartedVia, L"Started %ls via Big Box", L"Gestartet um %ls über Big Box") \
    X(GameQueued, L"Events are queued until the Pi is back", L"Ereignisse werden gesendet, sobald der Pi wieder da ist") \
    X(LinkAllLogs, L"All logs", L"Alle Protokolle") \
    X(RoleChipDefault, L"Default", L"Standard") \
    X(RoleChipShutdown, L"Shutdown", L"Shutdown") \
    /* connection */ \
    X(TestConnection, L"Test connection", L"Verbindung testen") \
    X(Testing, L"Testing…", L"Teste…") \
    X(HideToken, L"Hide token", L"Token verbergen") \
    X(TokenCount, L"%d characters", L"%d Zeichen") \
    X(TokenTooShort, L"%d characters · at least 24 required", L"%d Zeichen · mindestens 24 erforderlich") \
    X(TokenNone, L"No token entered", L"Kein Token eingegeben") \
    X(ConnOk, L"Pi connected · %d ms · token accepted", L"Pi verbunden · %d ms · Token akzeptiert") \
    X(ConnUnreachable, L"Pi unreachable: %ls. Check the cable and that the Pi is powered.", \
      L"Pi nicht erreichbar: %ls. Kabel und Stromversorgung des Pi prüfen.") \
    X(ConnTokenRejected, L"The Pi is reachable, but it rejected the token.", \
      L"Der Pi ist erreichbar, hat das Token aber abgelehnt.") \
    X(SecurityTitle, L"Trusted network only", L"Nur in vertrauenswürdigen Netzen") \
    X(SecurityBody, \
      L"The connection uses HTTP and the token is sent unencrypted. Use a direct cable or a trusted local network, " \
      L"and restrict allowed_client_ips on the Pi to this PC.", \
      L"Die Verbindung nutzt HTTP, das Token wird unverschlüsselt gesendet. Direktes Kabel oder vertrauenswürdiges " \
      L"lokales Netz verwenden und allowed_client_ips am Pi auf diesen PC beschränken.") \
    X(TokenStep1, L"Log in to the Raspberry Pi via SSH.", L"Per SSH am Raspberry Pi anmelden.") \
    X(TokenStep2, L"Generate a random token:", L"Zufälligen Token erzeugen:") \
    X(TokenStep3, \
      L"Enter the 64 characters in /etc/marquee-pi/config.json as the value of \"token\", replacing the placeholder.", \
      L"Die 64 Zeichen in /etc/marquee-pi/config.json als Wert von \"token\" eintragen und den Platzhalter ersetzen.") \
    X(TokenStep4, L"Restart the API service:", L"API-Dienst neu starten:") \
    X(TokenStep5, L"Enter exactly the same token here and save. Do not publish it.", \
      L"Genau denselben Token hier eintragen und speichern. Nicht veröffentlichen.") \
    /* media view */ \
    X(MediaGrid, L"Grid", L"Raster") \
    X(MediaListView, L"List", L"Liste") \
    X(MediaSubline, \
      L"JPG, PNG, GIF, WebP and H.264 MP4 up to 20 MB. Media is scaled to fit %d × %d without cropping.", \
      L"JPG, PNG, GIF, WebP und H.264-MP4 bis 20 MB. Medien werden ohne Zuschnitt auf %d × %d eingepasst.") \
    X(MediaSublineCover, \
      L"JPG, PNG, GIF, WebP and H.264 MP4 up to 20 MB. Media fills the %d × %d display; the edges may be cropped.", \
      L"JPG, PNG, GIF, WebP und H.264-MP4 bis 20 MB. Medien füllen das Display mit %d × %d; die Ränder können abgeschnitten werden.") \
    X(UploadingTo, L"Uploading to Pi · %d%%", L"Upload zum Pi · %d %%") \
    X(UploadErrorBody, L"The Pi rejected %ls: %ls The previous %ls stays active.", \
      L"Der Pi hat %ls abgelehnt: %ls Das bisherige %ls bleibt aktiv.") \
    X(Retry, L"Retry", L"Erneut versuchen") \
    X(ColName, L"Name", L"Name") \
    X(ColType, L"Type", L"Typ") \
    X(ColSize, L"Size", L"Größe") \
    X(ColRoles, L"Roles", L"Rollen") \
    X(MediaHintStatic, L"A file can have several roles. Boot splash takes effect from the next Pi start.", \
      L"Eine Datei kann mehrere Rollen haben. Der Boot-Splash gilt ab dem nächsten Pi-Start.") \
    X(MediaHintVideo, L"Boot splash accepts static PNG or JPEG only. Shutdown MP4 plays once, max 30 s.", \
      L"Der Boot-Splash akzeptiert nur statische PNG- oder JPEG-Dateien. Ein Shutdown-MP4 läuft einmal, max. 30 s.") \
    X(NoMedia, L"No media yet. Add a file to get started.", L"Noch keine Medien. Eine Datei hinzufügen.") \
    /* general view */ \
    X(LanguageCurrently, L"Currently %ls", L"Aktuell: %ls") \
    X(LanguageSub, L"Tray menu, dialogs and Pi touch menu", L"Tray-Menü, Dialoge und Pi-Touchmenü") \
    X(TouchMenuPreview, L"Pi touch menu preview", L"Vorschau des Pi-Touchmenüs") \
    X(TouchView, L"View", L"Ansicht") \
    X(TouchBrightness, L"Brightness", L"Helligkeit") \
    X(TouchStatus, L"Status", L"Status") \
    X(TouchRestart, L"Restart", L"Neustart") \
    X(AutostartHint, L"Keeps the tray icon running so game events reach the Pi.", \
      L"Hält das Tray-Symbol aktiv, damit Spielereignisse den Pi erreichen.") \
    X(SettingsFile, L"Settings file", L"Einstellungsdatei") \
    /* logs */ \
    X(LogFilterAll, L"All", L"Alle") \
    X(LogFilterInfo, L"Info", L"Info") \
    X(LogFilterWarning, L"Warning", L"Warnung") \
    X(LogFilterError, L"Error", L"Fehler") \
    X(LogCopy, L"Copy", L"Kopieren") \
    X(LogOpenShutdown, L"Open shutdown.log", L"shutdown.log öffnen") \
    X(LogEmpty, L"No events yet.", L"Noch keine Ereignisse.") \
    X(LogGameStarted, L"Game started: %ls", L"Spiel gestartet: %ls") \
    X(LogGameEnded, L"Game ended, default media restored", L"Spiel beendet, Standardmedium wiederhergestellt") \
    X(LogConnected, L"Pi connected: %ls", L"Pi verbunden: %ls") \
    X(LogUnreachable, L"Pi unreachable: %ls", L"Pi nicht erreichbar: %ls") \
    X(LogNotConfigured, L"Not configured: Pi address and token missing", \
      L"Nicht eingerichtet: Pi-Adresse und Token fehlen") \
    X(LogGameSent, L"Game sent to Pi (%d KB) in %d ms", L"Spiel an den Pi gesendet (%d KB) in %d ms") \
    X(LogArtworkSkipped, L"Artwork not sent: %ls", L"Artwork nicht gesendet: %ls") \
    X(LogPiWarning, L"Pi warning: %ls", L"Warnung vom Pi: %ls") \
    X(LogSettingsSaved, L"Settings saved", L"Einstellungen gespeichert") \
    X(LogSettingsReloaded, L"Settings reloaded from settings.ini", L"Einstellungen aus settings.ini neu geladen") \
    X(LogMediaStored, L"%ls stored on the Pi (%ls)", L"%ls auf dem Pi gespeichert (%ls)") \
    X(LogMediaFailed, L"Upload of %ls failed: %ls", L"Upload von %ls fehlgeschlagen: %ls") \
    X(LogMediaImported, L"Imported %ls", L"%ls importiert") \
    X(LogMediaRemoved, L"Removed %ls", L"%ls entfernt") \
    X(LogCommandSent, L"%ls: command sent to the Pi", L"%ls: Befehl an den Pi gesendet") \
    X(LogCommandFailed, L"%ls failed: %ls", L"%ls fehlgeschlagen: %ls") \
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
    X(LanguageHint, L"Also sets the language of the touch menu on the Pi display. Applies immediately.", \
      L"Legt auch die Sprache des Touchmenüs am Pi-Display fest. Wirkt sofort.")

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
// Text in an explicit language, e.g. for previews of what the Pi will show.
const wchar_t* trLang(Str id, bool german);
// Consecutive entries of a group, e.g. trAt(Str::TabConnection, 2).
inline const wchar_t* trAt(Str first, int offset) { return tr(Str(int(first) + offset)); }
// printf-style formatting of a string table entry; pass wide strings as const wchar_t* (%ls).
template <class... Args>
std::wstring fmt(Str id, Args... args) {
    wchar_t buffer[1024];
    _snwprintf(buffer, 1024, tr(id), args...);
    buffer[1023] = 0;
    return buffer;
}
