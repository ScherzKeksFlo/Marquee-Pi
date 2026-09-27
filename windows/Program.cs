using System.Drawing;
using System.IO.Pipes;
using Microsoft.Win32;
using System.Text.Json;

namespace ArcadePiTray;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        var settings = AppSettings.Load();
        using var client = new PiClient(settings);
        if (args.Length == 1 && args[0] == "--pi-shutdown")
        {
            try { client.CommandAsync("shutdown").GetAwaiter().GetResult(); }
            catch { Environment.ExitCode = 1; }
            return;
        }
        Application.Run(new TrayContext(settings, client));
    }
}

internal sealed class TrayContext : ApplicationContext
{
    private const string PipeName = "ArcadePiDisplayGameEvents";
    private readonly AppSettings settings;
    private readonly PiClient client;
    private readonly NotifyIcon icon;
    private readonly ShutdownWindow dispatcher;
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 5000 };
    private readonly CancellationTokenSource stop = new();
    private readonly SemaphoreSlim gameGate = new(1, 1);
    private readonly ToolStripMenuItem statusItem;
    private GameMessage? currentGame;
    private bool needsSync;
    private bool gestureConfigDirty = true;
    private string piInstance = "";
    private long gestureCursor;
    private bool checking;
    private int ticks;

    public TrayContext(AppSettings settings, PiClient client)
    {
        this.settings = settings;
        this.client = client;
        dispatcher = new ShutdownWindow(new ShutdownCoordinator(client));
        _ = dispatcher.Handle;

        var menu = new ContextMenuStrip();
        statusItem = new ToolStripMenuItem("Pi: Verbindung wird geprüft") { Enabled = false };
        menu.Items.Add(statusItem);
        menu.Items.Add(new ToolStripSeparator());
        Add(menu, "Standardmedien verwalten…", () => new MediaForm(client).ShowDialog());
        Add(menu, "Wischgesten einrichten…", () => {
            using var form = new GestureSettingsForm(settings);
            if (form.ShowDialog() == DialogResult.OK) gestureConfigDirty = true;
        });
        Add(menu, "Standardlogo anzeigen", async () => {
            currentGame = null;
            needsSync = false;
            await CommandAsync("default");
        });
        Add(menu, "Anzeige neu laden", async () => await CommandAsync("reload"));
        menu.Items.Add(new ToolStripSeparator());
        Add(menu, "Pi neu starten", async () => await ConfirmCommandAsync("reboot", "Pi wirklich neu starten?"));
        Add(menu, "Pi herunterfahren", async () => await ConfirmCommandAsync("shutdown",
            "Pi wirklich herunterfahren? Er startet erst nach einem neuen Stromzyklus."));
        menu.Items.Add(new ToolStripSeparator());
        Add(menu, "Verbindung einrichten…", () => {
            using var form = new SettingsForm(settings);
            if (form.ShowDialog() == DialogResult.OK) {
                client.UpdateSettings(settings);
                needsSync = currentGame != null;
                gestureConfigDirty = true;
            }
        });
        var autostart = new ToolStripMenuItem("Mit Windows starten") {
            Checked = IsAutostartEnabled(),
            CheckOnClick = true
        };
        autostart.Click += (_, _) => {
            try { SetAutostart(autostart.Checked); }
            catch (Exception error) {
                autostart.Checked = !autostart.Checked;
                MessageBox.Show(error.Message, "Autostart");
            }
        };
        menu.Items.Add(autostart);
        Add(menu, "Programm beenden", ExitThread);

        icon = new NotifyIcon {
            Icon = SystemIcons.Application,
            Text = "Arcade Pi Display",
            ContextMenuStrip = menu,
            Visible = true
        };
        icon.DoubleClick += (_, _) => new MediaForm(client).ShowDialog();

        timer.Tick += async (_, _) => await CheckAsync();
        timer.Start();
        _ = PipeLoopAsync(stop.Token);
        _ = GestureLoopAsync(stop.Token);
        _ = CheckAsync();
    }

    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string RunName = "ArcadePiDisplay";
    private static string StartupLink => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.Startup), "ArcadePiDisplay.lnk");

    private static bool IsAutostartEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        return File.Exists(StartupLink) || key?.GetValue(RunName) is string;
    }

    private static void SetAutostart(bool enabled)
    {
        using var key = Registry.CurrentUser.CreateSubKey(RunKey);
        if (!enabled) {
            key.DeleteValue(RunName, false);
            if (File.Exists(StartupLink)) File.Delete(StartupLink);
            return;
        }
        var path = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(path) ||
            !Path.GetFileName(path).Equals("ArcadePiTray.exe", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Autostart erst mit der veröffentlichten ArcadePiTray.exe aktivieren.");
        key.SetValue(RunName, "\"" + path + "\"");
        if (File.Exists(StartupLink)) File.Delete(StartupLink);
    }

    private static void Add(ContextMenuStrip menu, string text, Action action)
    {
        var item = new ToolStripMenuItem(text);
        item.Click += (_, _) => action();
        menu.Items.Add(item);
    }

    private async Task CommandAsync(string command)
    {
        try { await client.CommandAsync(command); }
        catch (Exception error) { MessageBox.Show(error.Message, "Arcade Pi Display"); }
    }

    private async Task ConfirmCommandAsync(string command, string prompt)
    {
        if (MessageBox.Show(prompt, "Arcade Pi Display", MessageBoxButtons.YesNo) == DialogResult.Yes)
            await CommandAsync(command);
    }

    private async Task CheckAsync()
    {
        if (checking) return;
        checking = true;
        try
        {
            if (currentGame != null && ++ticks % 3 == 0)
                await client.CommandAsync("heartbeat");
            var json = await client.StatusAsync();
            if (gestureConfigDirty)
            {
                await client.SetGestureConfigAsync(settings.GestureActions);
                gestureConfigDirty = false;
            }
            using var document = JsonDocument.Parse(json);
            var title = document.RootElement.GetProperty("game_title");
            var shown = title.ValueKind == JsonValueKind.String ? title.GetString() : "Standardmedium";
            statusItem.Text = "Pi verbunden – " + shown;
            icon.Text = "Arcade Pi Display – Pi verbunden";
            if (needsSync && currentGame != null)
            {
                await gameGate.WaitAsync();
                try
                {
                    if (needsSync && currentGame != null)
                    {
                        await client.GameAsync(currentGame);
                        needsSync = false;
                    }
                }
                finally { gameGate.Release(); }
            }
        }
        catch
        {
            statusItem.Text = "Pi nicht erreichbar";
            icon.Text = "Arcade Pi Display – Pi nicht erreichbar";
            if (currentGame != null) needsSync = true;
            gestureConfigDirty = true;
        }
        finally { checking = false; }
    }

    private async Task GestureLoopAsync(CancellationToken cancellation)
    {
        while (!cancellation.IsCancellationRequested)
        {
            try
            {
                var batch = await client.GestureEventsAsync(gestureCursor);
                if (batch.InstanceId != piInstance)
                {
                    piInstance = batch.InstanceId;
                    gestureCursor = 0;
                    gestureConfigDirty = true;
                    if (currentGame != null) needsSync = true;
                }
                else
                {
                    foreach (var id in batch.EventIds)
                    {
                        if (id <= gestureCursor) continue;
                        gestureCursor = id;
                        if (currentGame != null)
                            RetroArchHotkey.TrySend(settings.RetroArchMenuHotkey);
                    }
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception) { }
            try { await Task.Delay(750, cancellation); }
            catch (OperationCanceledException) { break; }
        }
    }

    private async Task PipeLoopAsync(CancellationToken cancellation)
    {
        while (!cancellation.IsCancellationRequested)
        {
            try
            {
                using var pipe = new NamedPipeServerStream(
                    PipeName, PipeDirection.In, 1, PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous);
                await pipe.WaitForConnectionAsync(cancellation);
                using var reader = new StreamReader(pipe);
                var line = await reader.ReadLineAsync(cancellation);
                if (line == null || line.Length > 100_000) continue;
                var message = JsonSerializer.Deserialize<GameMessage>(
                    line, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
                if (message != null && dispatcher.IsHandleCreated)
                    dispatcher.BeginInvoke(new Action(() => _ = HandleGameAsync(message)));
            }
            catch (OperationCanceledException) { break; }
            catch (Exception error) { Console.Error.WriteLine(error); }
        }
    }

    private async Task HandleGameAsync(GameMessage message)
    {
        await gameGate.WaitAsync();
        try
        {
            if (message.Action == "game")
            {
                currentGame = message;
                needsSync = true;
                await client.GameAsync(message);
                needsSync = false;
            }
            else if (message.Action == "exit")
            {
                currentGame = null;
                needsSync = false;
                await client.CommandAsync("default");
            }
        }
        catch (Exception error)
        {
            statusItem.Text = "Pi: " + error.Message;
        }
        finally { gameGate.Release(); }
    }

    protected override void ExitThreadCore()
    {
        timer.Stop();
        stop.Cancel();
        icon.Visible = false;
        icon.Dispose();
        dispatcher.Dispose();
        client.Dispose();
        stop.Dispose();
        base.ExitThreadCore();
    }
}
