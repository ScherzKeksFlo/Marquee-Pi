using System.Diagnostics;
using System.Runtime.InteropServices;

namespace ArcadePiTray;

internal static class RetroArchHotkey
{
    private const uint KeyUp = 0x0002;

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll")]
    private static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extraInfo);

    public static bool TryParse(string? text, out byte[] keys)
    {
        keys = Array.Empty<byte>();
        if (string.IsNullOrWhiteSpace(text)) return false;
        var parts = text.Split('+', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length is < 1 or > 5) return false;
        var modifiers = new List<byte>();
        var seen = new HashSet<byte>();
        foreach (var part in parts[..^1])
        {
            byte modifier = part.ToLowerInvariant() switch
            {
                "ctrl" or "control" or "strg" => (byte)Keys.ControlKey,
                "shift" or "umschalt" => (byte)Keys.ShiftKey,
                "alt" => (byte)Keys.Menu,
                "win" or "windows" => (byte)Keys.LWin,
                _ => 0
            };
            if (modifier == 0 || !seen.Add(modifier)) return false;
            modifiers.Add(modifier);
        }
        var name = parts[^1];
        if (name.Length == 1 && char.IsDigit(name[0])) name = "D" + name;
        if (!Enum.TryParse<Keys>(name, true, out var key) ||
            !Enum.IsDefined(key) || (int)key is < 1 or > 255 ||
            key is Keys.ControlKey or Keys.ShiftKey or Keys.Menu or Keys.LWin or Keys.RWin)
            return false;
        modifiers.Add((byte)key);
        keys = modifiers.ToArray();
        return true;
    }

    public static bool TrySend(string? hotkey)
    {
        if (!TryParse(hotkey, out var keys)) return false;
        var window = GetForegroundWindow();
        if (window == IntPtr.Zero) return false;
        GetWindowThreadProcessId(window, out var processId);
        if (processId == 0) return false;
        try
        {
            using var process = Process.GetProcessById((int)processId);
            if (!process.ProcessName.Equals("retroarch", StringComparison.OrdinalIgnoreCase))
                return false;
        }
        catch (ArgumentException) { return false; }
        catch (InvalidOperationException) { return false; }
        foreach (var key in keys) keybd_event(key, 0, 0, UIntPtr.Zero);
        for (var index = keys.Length - 1; index >= 0; index--)
            keybd_event(keys[index], 0, KeyUp, UIntPtr.Zero);
        return true;
    }
}