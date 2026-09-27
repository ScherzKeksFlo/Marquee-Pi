using System.Diagnostics;

namespace ArcadePiTray;

internal sealed class SettingsForm : Form
{
    private sealed record Choice(string Id, string Label)
    {
        public override string ToString() => Label;
    }

    private static readonly Choice[] Choices =
    {
        new("none", "Keine Aktion"),
        new("marquee", "Marquee anzeigen"),
        new("box_art", "Box Art anzeigen"),
        new("logo", "LaunchBox-Logo anzeigen"),
        new("controls", "Steuerungsbelegung anzeigen"),
        new("default", "Standardanimation anzeigen"),
        new("retroarch_menu", "RetroArch-Menü öffnen")
    };

    private readonly AppSettings settings;
    private readonly PiClient client;
    private readonly TextBox url = new() { Dock = DockStyle.Fill };
    private readonly TextBox token = new() { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
    private readonly TextBox hotkey = new() { Width = 220 };
    private readonly CheckBox autostart = new() { Text = "Mit Windows starten", AutoSize = true };
    private readonly Dictionary<string, ComboBox> fields = new();

    public SettingsForm(AppSettings settings, PiClient client)
    {
        this.settings = settings;
        this.client = client;
        Text = "Arcade Pi Display – Einstellungen";
        Width = 660;
        Height = 440;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterScreen;
        MaximizeBox = false;
        MinimizeBox = false;

        url.Text = settings.PiUrl;
        token.Text = settings.Token;
        hotkey.Text = settings.RetroArchMenuHotkey;
        autostart.Checked = settings.StartWithWindows;

        var tabs = new TabControl { Dock = DockStyle.Fill };
        tabs.TabPages.Add(ConnectionTab());
        tabs.TabPages.Add(GesturesTab());
        tabs.TabPages.Add(GeneralTab());
        var buttons = new FlowLayoutPanel {
            Dock = DockStyle.Bottom, Height = 50, Padding = new Padding(10),
            FlowDirection = FlowDirection.RightToLeft
        };
        var save = new Button { Text = "Speichern", AutoSize = true };
        save.Click += (_, _) => Save();
        var cancel = new Button { Text = "Abbrechen", AutoSize = true, DialogResult = DialogResult.Cancel };
        buttons.Controls.Add(save);
        buttons.Controls.Add(cancel);
        Controls.Add(tabs);
        Controls.Add(buttons);
        AcceptButton = save;
        CancelButton = cancel;
    }

    private TabPage ConnectionTab()
    {
        var page = new TabPage("Verbindung");
        var layout = new TableLayoutPanel {
            Dock = DockStyle.Fill, Padding = new Padding(15),
            ColumnCount = 2, RowCount = 4
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 170));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.Controls.Add(new Label { Text = "Pi-Adresse", AutoSize = true }, 0, 0);
        layout.Controls.Add(url, 1, 0);
        layout.Controls.Add(new Label { Text = "Zugriffstoken", AutoSize = true }, 0, 1);
        layout.Controls.Add(token, 1, 1);
        var helpers = new FlowLayoutPanel { AutoSize = true, WrapContents = true };
        var show = new CheckBox { Text = "Token anzeigen", AutoSize = true };
        show.CheckedChanged += (_, _) => token.UseSystemPasswordChar = !show.Checked;
        var help = new Button { Text = "Hilfe: Token erstellen", AutoSize = true };
        help.Click += (_, _) => ShowTokenHelp();
        helpers.Controls.Add(show);
        helpers.Controls.Add(help);
        layout.Controls.Add(helpers, 1, 2);
        var hint = new Label {
            Text = "Adresse z. B. http://10.0.0.10:8765. Der Token muss mit der Pi-Konfiguration übereinstimmen.",
            AutoSize = true, MaximumSize = new Size(570, 0)
        };
        layout.Controls.Add(hint, 0, 3);
        layout.SetColumnSpan(hint, 2);
        page.Controls.Add(layout);
        return page;
    }

    private TabPage GesturesTab()
    {
        var page = new TabPage("Wischgesten");
        var layout = new TableLayoutPanel {
            Dock = DockStyle.Fill, Padding = new Padding(15),
            ColumnCount = 2, RowCount = 6
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 205));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        AddGesture(layout, 0, "swipe-down", "Oben nach unten");
        AddGesture(layout, 1, "swipe-up", "Unten nach oben");
        AddGesture(layout, 2, "swipe-right", "Links nach rechts");
        AddGesture(layout, 3, "swipe-left", "Rechts nach links");
        layout.Controls.Add(new Label {
            Text = "RetroArch-Tastenkombination", AutoSize = true, Anchor = AnchorStyles.Left
        }, 0, 4);
        layout.Controls.Add(hotkey, 1, 4);
        var hint = new Label {
            Text = "Beispiele: F1, Ctrl+F1, Shift+F1. Die Taste geht nur an ein aktives RetroArch-Fenster.",
            AutoSize = true, MaximumSize = new Size(570, 0)
        };
        layout.Controls.Add(hint, 0, 5);
        layout.SetColumnSpan(hint, 2);
        page.Controls.Add(layout);
        return page;
    }

    private void AddGesture(TableLayoutPanel layout, int row, string id, string label)
    {
        layout.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left }, 0, row);
        var choice = new ComboBox { Dock = DockStyle.Fill, DropDownStyle = ComboBoxStyle.DropDownList };
        choice.Items.AddRange(Choices.Cast<object>().ToArray());
        var selected = settings.GestureActions.GetValueOrDefault(id, "none");
        choice.SelectedItem = Choices.FirstOrDefault(item => item.Id == selected) ?? Choices[0];
        layout.Controls.Add(choice, 1, row);
        fields[id] = choice;
    }

    private TabPage GeneralTab()
    {
        var page = new TabPage("Allgemein");
        var layout = new FlowLayoutPanel {
            Dock = DockStyle.Fill, Padding = new Padding(18),
            FlowDirection = FlowDirection.TopDown, WrapContents = false
        };
        layout.Controls.Add(autostart);
        var media = new Button { Text = "Standardmedien verwalten…", AutoSize = true };
        media.Click += (_, _) => { using var form = new MediaForm(client); form.ShowDialog(this); };
        layout.Controls.Add(media);
        var open = new Button { Text = "INI-Datei öffnen", AutoSize = true };
        open.Click += (_, _) => Process.Start(new ProcessStartInfo(AppSettings.FilePath) { UseShellExecute = true });
        layout.Controls.Add(open);
        layout.Controls.Add(new Label {
            Text = AppSettings.FilePath, AutoSize = true, MaximumSize = new Size(570, 0)
        });
        page.Controls.Add(layout);
        return page;
    }

    private void Save()
    {
        var candidate = new AppSettings {
            PiUrl = url.Text.Trim(),
            Token = token.Text.Trim(),
            RetroArchMenuHotkey = hotkey.Text.Trim(),
            GestureActions = fields.ToDictionary(
                item => item.Key, item => ((Choice)item.Value.SelectedItem!).Id),
            StartWithWindows = autostart.Checked
        };
        if (!candidate.IsConfigured)
        {
            MessageBox.Show(this, "Bitte eine HTTP-Adresse und ein Token mit mindestens 24 Zeichen eingeben.");
            return;
        }
        if (!RetroArchHotkey.TryParse(candidate.RetroArchMenuHotkey, out _))
        {
            MessageBox.Show(this, "Ungültige Tastenkombination. Beispiel: Ctrl+Shift+F1.");
            return;
        }
        try
        {
            AutostartManager.SetEnabled(candidate.StartWithWindows);
            candidate.Save();
            settings.CopyFrom(candidate);
            DialogResult = DialogResult.OK;
            Close();
        }
        catch (Exception error)
        {
            MessageBox.Show(this, error.Message, "Einstellungen konnten nicht gespeichert werden");
        }
    }

    private void ShowTokenHelp()
    {
        const string instructions =
            "1. Per SSH am Raspberry Pi anmelden.\n\n" +
            "2. Einen zufälligen Token erzeugen:\n" +
            "   openssl rand -hex 32\n\n" +
            "3. Die 64 ausgegebenen Zeichen in /etc/arcade-pi-display/config.json " +
            "als Wert von \"token\" eintragen (z. B. mit sudo nano). " +
            "Den bisherigen Platzhalter ersetzen.\n\n" +
            "4. Den API-Dienst neu starten:\n" +
            "   sudo systemctl restart arcade-pi-display.service\n\n" +
            "5. Genau denselben Token hier eintragen und speichern. " +
            "Den Token nicht veröffentlichen oder ins Git-Repository aufnehmen.";
        using var help = new Form {
            Text = "Zugriffstoken erstellen", Width = 610, Height = 390,
            StartPosition = FormStartPosition.CenterParent,
            FormBorderStyle = FormBorderStyle.FixedDialog,
            MaximizeBox = false, MinimizeBox = false
        };
        var body = new TextBox {
            Text = instructions, Multiline = true, ReadOnly = true,
            ScrollBars = ScrollBars.Vertical, Dock = DockStyle.Fill
        };
        help.Controls.Add(body);
        help.ShowDialog(this);
    }
}
internal sealed class MediaForm : Form
{
    private readonly PiClient client;
    private readonly ListBox files = new() { Dock = DockStyle.Fill };
    private readonly Label active = new() { Dock = DockStyle.Top, Height = 26 };
    private static string ActiveFile => Path.Combine(AppSettings.DirectoryPath, "active-media.txt");

    public MediaForm(PiClient client)
    {
        this.client = client;
        Text = "Arcade Pi Display – Standardmedien";
        Width = 620;
        Height = 400;
        StartPosition = FormStartPosition.CenterScreen;
        Directory.CreateDirectory(AppSettings.MediaPath);
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 45 };
        AddButton(buttons, "Hinzufügen", AddMedia);
        AddButton(buttons, "Vorschau", Preview);
        AddButton(buttons, "Auf Pi aktivieren", async () => await ActivateAsync());
        AddButton(buttons, "Entfernen", Remove);
        Controls.Add(files);
        Controls.Add(active);
        Controls.Add(buttons);
        RefreshFiles();
    }

    private static void AddButton(FlowLayoutPanel panel, string label, Action action)
    {
        var button = new Button { Text = label, AutoSize = true };
        button.Click += (_, _) => action();
        panel.Controls.Add(button);
    }

    private void RefreshFiles()
    {
        files.Items.Clear();
        foreach (var path in Directory.GetFiles(AppSettings.MediaPath).OrderBy(Path.GetFileName))
            files.Items.Add(Path.GetFileName(path));
        active.Text = "Aktiv: " + (File.Exists(ActiveFile) ? File.ReadAllText(ActiveFile) : "kein eigenes Medium");
    }

    private string? SelectedPath() =>
        files.SelectedItem is string name ? Path.Combine(AppSettings.MediaPath, name) : null;

    private void AddMedia()
    {
        using var dialog = new OpenFileDialog {
            Filter = "Unterstützte Medien|*.jpg;*.jpeg;*.png;*.gif;*.webp;*.mp4",
            Title = "Standardmedium auswählen"
        };
        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        var source = new FileInfo(dialog.FileName);
        if (source.Length > 20 * 1024 * 1024)
        {
            MessageBox.Show(this, "Datei überschreitet das Upload-Limit von 20 MB.");
            return;
        }
        var name = Path.GetFileName(dialog.FileName);
        var target = Path.Combine(AppSettings.MediaPath, name);
        if (File.Exists(target))
        {
            name = Path.GetFileNameWithoutExtension(name) + "-" + Guid.NewGuid().ToString("N")[..8] + source.Extension;
            target = Path.Combine(AppSettings.MediaPath, name);
        }
        File.Copy(dialog.FileName, target);
        RefreshFiles();
        files.SelectedItem = name;
    }

    private void Preview()
    {
        var path = SelectedPath();
        if (path == null) return;
        Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
    }

    private async Task ActivateAsync()
    {
        var path = SelectedPath();
        if (path == null) return;
        try
        {
            await client.UploadDefaultAsync(path);
            File.WriteAllText(ActiveFile, Path.GetFileName(path));
            RefreshFiles();
            MessageBox.Show(this, "Das Medium ist auf dem Pi aktiv.");
        }
        catch (Exception error)
        {
            MessageBox.Show(this, error.Message, "Upload fehlgeschlagen");
        }
    }

    private void Remove()
    {
        var path = SelectedPath();
        if (path == null) return;
        if (File.Exists(ActiveFile) && Path.GetFileName(path) == File.ReadAllText(ActiveFile))
        {
            MessageBox.Show(this, "Bitte zuerst ein anderes Medium aktivieren.");
            return;
        }
        File.Delete(path);
        RefreshFiles();
    }
}
