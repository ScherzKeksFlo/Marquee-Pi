using System.Text;
using System.Text.Json;

namespace ArcadePiTray;

internal sealed class AppSettings
{
    public string PiUrl { get; set; } = "";
    public string Token { get; set; } = "";
    public string RetroArchMenuHotkey { get; set; } = "F1";
    public Dictionary<string, string> GestureActions { get; set; } = new();
    public bool StartWithWindows { get; set; }

    public static string DirectoryPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ArcadePiDisplay");
    public static string FilePath => Path.Combine(DirectoryPath, "settings.ini");
    private static string LegacyFilePath => Path.Combine(DirectoryPath, "settings.json");
    public static string MediaPath => Path.Combine(DirectoryPath, "media");

    public static AppSettings Load()
    {
        if (File.Exists(FilePath)) return ReadIni(File.ReadAllLines(FilePath));
        AppSettings settings;
        try
        {
            settings = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(LegacyFilePath)) ?? new();
        }
        catch (IOException) { settings = new(); }
        catch (JsonException) { settings = new(); }
        settings.StartWithWindows = AutostartManager.IsEnabled();
        settings.Save();
        return settings;
    }

    private static AppSettings ReadIni(IEnumerable<string> lines)
    {
        var settings = new AppSettings();
        var section = "";
        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith(';') || line.StartsWith('#')) continue;
            if (line.StartsWith('[') && line.EndsWith(']'))
            {
                section = line[1..^1].Trim().ToLowerInvariant();
                continue;
            }
            var separator = line.IndexOf('=');
            if (separator < 1) continue;
            var key = line[..separator].Trim().ToLowerInvariant();
            var value = line[(separator + 1)..].Trim();
            switch (section, key)
            {
                case ("connection", "piurl"): settings.PiUrl = value; break;
                case ("connection", "token"): settings.Token = value; break;
                case ("gestures", "retroarchmenuhotkey"): settings.RetroArchMenuHotkey = value; break;
                case ("gestures", "swipedown"): settings.GestureActions["swipe-down"] = value; break;
                case ("gestures", "swipeup"): settings.GestureActions["swipe-up"] = value; break;
                case ("gestures", "swiperight"): settings.GestureActions["swipe-right"] = value; break;
                case ("gestures", "swipeleft"): settings.GestureActions["swipe-left"] = value; break;
                case ("general", "startwithwindows"):
                    if (bool.TryParse(value, out var start)) settings.StartWithWindows = start;
                    break;
            }
        }
        return settings;
    }

    public void Save()
    {
        Directory.CreateDirectory(DirectoryPath);
        static string SingleLine(string? value) => (value ?? "").Replace("\r", "").Replace("\n", "");
        var data = new StringBuilder()
            .AppendLine("; Arcade Pi Display – nach externer Bearbeitung im Tray 'Einstellungen neu laden' wählen.")
            .AppendLine("[Connection]")
            .AppendLine("PiUrl=" + SingleLine(PiUrl))
            .AppendLine("Token=" + SingleLine(Token))
            .AppendLine()
            .AppendLine("[Gestures]")
            .AppendLine("SwipeDown=" + SingleLine(GestureActions.GetValueOrDefault("swipe-down", "none")))
            .AppendLine("SwipeUp=" + SingleLine(GestureActions.GetValueOrDefault("swipe-up", "none")))
            .AppendLine("SwipeRight=" + SingleLine(GestureActions.GetValueOrDefault("swipe-right", "none")))
            .AppendLine("SwipeLeft=" + SingleLine(GestureActions.GetValueOrDefault("swipe-left", "none")))
            .AppendLine("RetroArchMenuHotkey=" + SingleLine(RetroArchMenuHotkey))
            .AppendLine()
            .AppendLine("[General]")
            .AppendLine("StartWithWindows=" + StartWithWindows.ToString().ToLowerInvariant())
            .ToString();
        var temporary = FilePath + ".tmp";
        File.WriteAllText(temporary, data, new UTF8Encoding(false));
        File.Move(temporary, FilePath, true);
    }

    public void CopyFrom(AppSettings other)
    {
        PiUrl = other.PiUrl;
        Token = other.Token;
        RetroArchMenuHotkey = other.RetroArchMenuHotkey;
        GestureActions = new Dictionary<string, string>(other.GestureActions);
        StartWithWindows = other.StartWithWindows;
    }

    public bool IsConfigured => Uri.TryCreate(PiUrl, UriKind.Absolute, out var uri)
        && uri.Scheme == Uri.UriSchemeHttp && (Token?.Length ?? 0) >= 24;
}
