using System.Text.Json;

namespace ArcadePiTray;

internal sealed class AppSettings
{
    public string PiUrl { get; set; } = "";
    public string Token { get; set; } = "";

    public static string DirectoryPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ArcadePiDisplay");
    public static string FilePath => Path.Combine(DirectoryPath, "settings.json");
    public static string MediaPath => Path.Combine(DirectoryPath, "media");

    public static AppSettings Load()
    {
        try
        {
            return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(FilePath)) ?? new();
        }
        catch (IOException) { return new(); }
        catch (JsonException) { return new(); }
    }

    public void Save()
    {
        Directory.CreateDirectory(DirectoryPath);
        File.WriteAllText(FilePath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }

    public bool IsConfigured => Uri.TryCreate(PiUrl, UriKind.Absolute, out var uri)
        && uri.Scheme == Uri.UriSchemeHttp && (Token?.Length ?? 0) >= 24;
}
