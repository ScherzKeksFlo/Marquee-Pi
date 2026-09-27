using System.Diagnostics.Eventing.Reader;
using System.Xml.Linq;

namespace ArcadePiTray;

internal sealed class ShutdownCoordinator
{
    private readonly PiClient client;
    private readonly DateTime startedAt = DateTime.Now;
    private bool handled;

    public ShutdownCoordinator(PiClient client) => this.client = client;

    public void OnSessionEnding()
    {
        if (handled) return;
        handled = true;
        try
        {
            var shutdownType = RecentShutdownType();
            if (!IsPowerOff(shutdownType))
            {
                Log("Pi bleibt eingeschaltet; Windows-Typ: " + (shutdownType ?? "unbekannt"));
                return;
            }
            client.CommandSync("shutdown", TimeSpan.FromSeconds(3));
            Log("Pi-Shutdown-Befehl gesendet; Windows-Typ: " + shutdownType);
        }
        catch (Exception error)
        {
            Log("Pi-Shutdown fehlgeschlagen: " + error.Message);
        }
    }

    private string? RecentShutdownType()
    {
        const string query = "*[System[(EventID=1074)]]";
        using var reader = new EventLogReader(new EventLogQuery("System", PathType.LogName, query)
        {
            ReverseDirection = true
        });
        for (var i = 0; i < 10; i++)
        {
            using var record = reader.ReadEvent();
            if (record == null) break;
            if (record.TimeCreated == null || record.TimeCreated < startedAt ||
                DateTime.Now - record.TimeCreated.Value > TimeSpan.FromMinutes(2))
                continue;
            if (!string.Equals(record.ProviderName, "User32", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(record.ProviderName, "Microsoft-Windows-User32", StringComparison.OrdinalIgnoreCase))
                continue;
            var data = XDocument.Parse(record.ToXml()).Descendants()
                .FirstOrDefault(element => element.Name.LocalName == "Data" &&
                    string.Equals((string?)element.Attribute("Name"), "param5", StringComparison.OrdinalIgnoreCase));
            return data?.Value.Trim();
        }
        return null;
    }

    private static bool IsPowerOff(string? value) => value?.Trim().ToLowerInvariant() is
        "shutdown" or "power off" or "herunterfahren" or "ausschalten";

    private static void Log(string message)
    {
        try
        {
            var directory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ArcadePiDisplay");
            Directory.CreateDirectory(directory);
            File.AppendAllText(Path.Combine(directory, "shutdown.log"),
                $"{DateTime.Now:O} {message}{Environment.NewLine}");
        }
        catch { }
    }
}

internal sealed class ShutdownWindow : Form
{
    private const int WmEndSession = 0x0016;
    private const long EndSessionCloseApp = 0x00000001;
    private const long EndSessionLogoff = 0x80000000;
    private readonly ShutdownCoordinator coordinator;

    public ShutdownWindow(ShutdownCoordinator coordinator)
    {
        this.coordinator = coordinator;
        ShowInTaskbar = false;
    }

    protected override void WndProc(ref Message message)
    {
        if (message.Msg == WmEndSession && message.WParam != IntPtr.Zero &&
            (message.LParam.ToInt64() & (EndSessionCloseApp | EndSessionLogoff)) == 0)
            coordinator.OnSessionEnding();
        base.WndProc(ref message);
    }
}
