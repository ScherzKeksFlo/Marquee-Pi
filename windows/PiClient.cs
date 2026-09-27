using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;

namespace ArcadePiTray;

internal sealed class PiClient : IDisposable
{
    private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(8) };
    private AppSettings settings;

    public PiClient(AppSettings settings) => this.settings = settings;
    public void UpdateSettings(AppSettings value) => settings = value;

    private HttpRequestMessage Request(HttpMethod method, string path, HttpContent? content = null)
    {
        if (!settings.IsConfigured) throw new InvalidOperationException("Pi-Adresse und Token zuerst einrichten.");
        var url = new Uri(new Uri(settings.PiUrl.TrimEnd('/') + "/"), path.TrimStart('/'));
        var request = new HttpRequestMessage(method, url) { Content = content };
        request.Headers.Add("X-Arcade-Token", settings.Token);
        return request;
    }

    private async Task<string> SendAsync(HttpRequestMessage request)
    {
        using (request)
        using (var response = await http.SendAsync(request))
        {
            var body = await response.Content.ReadAsStringAsync();
            if (!response.IsSuccessStatusCode)
                throw new InvalidOperationException($"Pi meldet {(int)response.StatusCode}: {body}");
            return body;
        }
    }

    public void CommandSync(string command, TimeSpan timeout)
    {
        using var cancellation = new CancellationTokenSource(timeout);
        using var request = Request(HttpMethod.Post, "v1/" + command);
        using var response = http.Send(request, cancellation.Token);
        response.EnsureSuccessStatusCode();
    }

    public async Task<string> StatusAsync() =>
        await SendAsync(Request(HttpMethod.Get, "v1/status"));

    public async Task CommandAsync(string command) =>
        await SendAsync(Request(HttpMethod.Post, "v1/" + command));

    public async Task UploadDefaultAsync(string path)
    {
        var data = await File.ReadAllBytesAsync(path);
        using var content = new ByteArrayContent(data);
        content.Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
        var request = Request(HttpMethod.Post, "v1/default-media", content);
        request.Headers.Add("X-File-Name", "upload" + Path.GetExtension(path).ToLowerInvariant());
        await SendAsync(request);
    }

    private static object? Artwork(string? path)
    {
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) return null;
        var extension = Path.GetExtension(path).ToLowerInvariant();
        if (extension is not (".png" or ".jpg" or ".jpeg" or ".gif" or ".webp")) return null;
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length > 20 * 1024 * 1024) return null;
        return new { extension, base64 = Convert.ToBase64String(bytes) };
    }

    public async Task GameAsync(GameMessage game)
    {
        var payload = new {
            title = game.Title,
            marquee = Artwork(game.MarqueePath),
            controls = Artwork(game.ControlsPath)
        };
        using var content = new StringContent(JsonSerializer.Serialize(payload), Encoding.UTF8, "application/json");
        await SendAsync(Request(HttpMethod.Post, "v1/game", content));
    }

    public void Dispose() => http.Dispose();
}

internal sealed class GameMessage
{
    public string Action { get; set; } = "";
    public string Title { get; set; } = "";
    public string? MarqueePath { get; set; }
    public string? ControlsPath { get; set; }
}
