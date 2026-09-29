using System;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Reflection;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
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
                LogoPath = logo
            };
            Send(message);
        }

        public void OnGameExited()
        {
            Send(new GameMessage { Action = "exit" });
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
        }
    }
}
