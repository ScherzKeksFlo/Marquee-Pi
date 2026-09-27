using System.Diagnostics;

namespace ArcadePiTray;

internal sealed class SettingsForm : Form
{
    private readonly TextBox url = new() { Width = 400 };
    private readonly TextBox token = new() { Width = 400, UseSystemPasswordChar = true };

    public SettingsForm(AppSettings settings)
    {
        Text = "Arcade Pi Display – Verbindung";
        Width = 540;
        Height = 190;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterScreen;
        MaximizeBox = false;
        MinimizeBox = false;

        url.Text = settings.PiUrl;
        token.Text = settings.Token;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(12), ColumnCount = 2, RowCount = 3 };
        layout.Controls.Add(new Label { Text = "Pi-Adresse (http://IP:8765)", AutoSize = true }, 0, 0);
        layout.Controls.Add(url, 1, 0);
        layout.Controls.Add(new Label { Text = "Zugriffstoken", AutoSize = true }, 0, 1);
        layout.Controls.Add(token, 1, 1);
        var save = new Button { Text = "Speichern", AutoSize = true };
        save.Click += (_, _) => {
            settings.PiUrl = url.Text.Trim();
            settings.Token = token.Text.Trim();
            if (!settings.IsConfigured)
            {
                MessageBox.Show("Bitte eine HTTP-Adresse und ein Token mit mindestens 24 Zeichen eingeben.");
                return;
            }
            settings.Save();
            DialogResult = DialogResult.OK;
            Close();
        };
        layout.Controls.Add(save, 1, 2);
        Controls.Add(layout);
        AcceptButton = save;
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
