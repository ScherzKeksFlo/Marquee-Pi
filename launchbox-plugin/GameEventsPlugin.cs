using System;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Reflection;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text.RegularExpressions;
using System.Threading;
using Unbroken.LaunchBox.Plugins;
using Unbroken.LaunchBox.Plugins.Data;

namespace MarqueePiLaunchBox
{
    public sealed class GameEventsPlugin : IGameLaunchingPlugin
    {
        private const string PipeName = "MarqueePiGameEvents";

        public void OnBeforeGameLaunching(IGame? game, IAdditionalApplication? app, IEmulator? emulator)
        {
        }

        public void OnAfterGameLaunched(IGame? game, IAdditionalApplication? app, IEmulator? emulator)
        {
            if (game == null) return;
            var marquee = FirstExisting(
                game.MarqueeImagePath,
                FirstImage(game, ImageTypes.ArcadeMarquee),
                FirstImage(game, ImageTypes.Banner),
                game.ClearLogoImagePath);
            var controls = FirstExisting(
                FirstImage(game, ImageTypes.ArcadeControlsInformation),
                FirstImage(game, ImageTypes.ArcadeControlPanel));
            var boxArt = FirstExisting(game.FrontImagePath, FirstImage(game, ImageTypes.BoxFront));
            var logo = FirstExisting(game.ClearLogoImagePath, FirstImage(game, ImageTypes.ClearLogo));
            var message = new GameMessage
            {
                Action = "game",
                Title = game.Title ?? "Game",
                MarqueePath = marquee,
                ControlsPath = controls,
                BoxArtPath = boxArt,
                LogoPath = logo,
                // Only emulated games have a ROM and a core; native Windows games report neither.
                Core = emulator == null ? null : CoreName(game, emulator),
                Rom = emulator == null ? null : RomName(game)
            };
            Send(message);
        }

        public void OnGameExited()
        {
            Send(new GameMessage { Action = "exit" });
        }

        // File name of the ROM, e.g. "sf2ce.zip".
        private static string? RomName(IGame game)
        {
            try
            {
                var name = Path.GetFileName(game.ApplicationPath ?? "");
                return string.IsNullOrWhiteSpace(name) ? null : name;
            }
            catch { return null; }
        }

        // The libretro core from the launch command line (-L "cores\fbneo_libretro.dll"): the name from
        // RetroArch's info file ("FinalBurn Neo") when it can be found, else the file name ("fbneo").
        private static string? CoreName(IGame game, IEmulator emulator)
        {
            try
            {
                var match = Regex.Match(game.GetEffectiveCommandLine() ?? "",
                    "(?:^|\\s)(?:-L|--libretro)(?:=|\\s+)(?:\"([^\"]+)\"|(\\S+))");
                if (!match.Success) return null;
                var library = match.Groups[1].Success ? match.Groups[1].Value : match.Groups[2].Value;
                var stem = Path.GetFileNameWithoutExtension(library);
                if (string.IsNullOrEmpty(stem)) return null;
                var executable = Resolve(emulator.ApplicationPath);
                if (executable != null)
                {
                    var info = Path.Combine(Path.GetDirectoryName(executable) ?? "", "info", stem + ".info");
                    if (File.Exists(info))
                    {
                        foreach (var line in File.ReadLines(info))
                        {
                            var name = Regex.Match(line, "^\\s*corename\\s*=\\s*\"(.+)\"\\s*$");
                            if (name.Success) return name.Groups[1].Value;
                        }
                    }
                }
                const string suffix = "_libretro";
                return stem.EndsWith(suffix, StringComparison.OrdinalIgnoreCase) ? stem.Substring(0, stem.Length - suffix.Length) : stem;
            }
            catch { return null; }
        }

        private static string? FirstImage(IGame game, string imageType)
        {
            try
            {
                var images = game.GetAllImagesWithDetails(imageType);
                return images?.FirstOrDefault()?.FilePath;
            }
            catch { return null; }
        }

        private static string? FirstExisting(params string?[] candidates)
        {
            foreach (var candidate in candidates)
            {
                var path = Resolve(candidate);
                if (path != null) return path;
            }
            return null;
        }

        private static string? Resolve(string? candidate)
        {
            if (string.IsNullOrWhiteSpace(candidate)) return null;
            try
            {
                if (Path.IsPathRooted(candidate))
                    return File.Exists(candidate) ? candidate : null;

                var directory = new FileInfo(Assembly.GetExecutingAssembly().Location).Directory;
                while (directory != null)
                {
                    var path = Path.GetFullPath(Path.Combine(directory.FullName, candidate));
                    if (File.Exists(path)) return path;
                    directory = directory.Parent;
                }
            }
            catch { }
            return null;
        }

        private static void Send(GameMessage message)
        {
            try
            {
                string json;
                var serializer = new DataContractJsonSerializer(typeof(GameMessage));
                using (var memory = new MemoryStream())
                {
                    serializer.WriteObject(memory, message);
                    memory.Position = 0;
                    using (var reader = new StreamReader(memory))
                        json = reader.ReadToEnd();
                }
                for (var attempt = 0; attempt < 3; attempt++)
                {
                    try
                    {
                        using (var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.Out))
                        {
                            pipe.Connect(200);
                            using (var writer = new StreamWriter(pipe) { AutoFlush = true })
                                writer.WriteLine(json);
                        }
                        return;
                    }
                    catch when (attempt < 2)
                    {
                        Thread.Sleep(75);
                    }
                }
            }
            catch
            {
                // Big Box must keep launching games even when the companion app is offline.
            }
        }

        [DataContract]
        private sealed class GameMessage
        {
            [DataMember] public string Action { get; set; } = "";
            [DataMember] public string Title { get; set; } = "";
            [DataMember] public string? MarqueePath { get; set; }
            [DataMember] public string? ControlsPath { get; set; }
            [DataMember] public string? BoxArtPath { get; set; }
            [DataMember] public string? LogoPath { get; set; }
            [DataMember] public string? Core { get; set; }
            [DataMember] public string? Rom { get; set; }
        }
    }
}
