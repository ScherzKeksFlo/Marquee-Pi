using Microsoft.Win32;

namespace ArcadePiTray;

internal static class AutostartManager
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string RunName = "ArcadePiDisplay";
    private static string StartupLink => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.Startup), "ArcadePiDisplay.lnk");

    public static bool IsEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        return File.Exists(StartupLink) || key?.GetValue(RunName) is string;
    }

    public static void SetEnabled(bool enabled)
    {
        using var key = Registry.CurrentUser.CreateSubKey(RunKey);
        if (!enabled)
        {
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
}
