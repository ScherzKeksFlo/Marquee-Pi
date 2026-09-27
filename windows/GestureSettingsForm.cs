namespace ArcadePiTray;

internal sealed class GestureSettingsForm : Form
{
    private sealed record Choice(string Id, string Label)
    {
        public override string ToString() => Label;
    }

    private static readonly Choice[] Choices = new Choice[]
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
    private readonly Dictionary<string, ComboBox> fields = new();
    private readonly TextBox hotkey = new() { Width = 190 };

    public GestureSettingsForm(AppSettings settings)
    {
        this.settings = settings;
        Text = "Arcade Pi Display – Wischgesten";
        Width = 590;
        Height = 350;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterScreen;
        MaximizeBox = false;
        MinimizeBox = false;

        var layout = new TableLayoutPanel {
            Dock = DockStyle.Fill, Padding = new Padding(16),
            ColumnCount = 2, RowCount = 7
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 190));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        AddField(layout, 0, "swipe-down", "Oben nach unten");
        AddField(layout, 1, "swipe-up", "Unten nach oben");
        AddField(layout, 2, "swipe-right", "Links nach rechts");
        AddField(layout, 3, "swipe-left", "Rechts nach links");

        layout.Controls.Add(new Label {
            Text = "RetroArch-Tastenkombination", AutoSize = true,
            Anchor = AnchorStyles.Left
        }, 0, 4);
        hotkey.Text = settings.RetroArchMenuHotkey ?? "F1";
        layout.Controls.Add(hotkey, 1, 4);
        layout.Controls.Add(new Label {
            Text = "Beispiele: F1, Ctrl+F1, Shift+F1. Die Taste geht nur an ein aktives RetroArch-Fenster.",
            AutoSize = true, MaximumSize = new Size(510, 0)
        }, 0, 5);
        layout.SetColumnSpan(layout.GetControlFromPosition(0, 5)!, 2);

        var save = new Button { Text = "Speichern", AutoSize = true };
        save.Click += (_, _) => Save();
        layout.Controls.Add(save, 1, 6);
        Controls.Add(layout);
        AcceptButton = save;
    }

    private void AddField(TableLayoutPanel layout, int row, string kind, string label)
    {
        layout.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left }, 0, row);
        var choice = new ComboBox { Width = 315, DropDownStyle = ComboBoxStyle.DropDownList };
        choice.Items.AddRange(Choices.Cast<object>().ToArray());
        var selected = settings.GestureActions?.GetValueOrDefault(kind) ?? "none";
        choice.SelectedItem = Choices.FirstOrDefault(item => item.Id == selected) ?? Choices[0];
        layout.Controls.Add(choice, 1, row);
        fields[kind] = choice;
    }

    private void Save()
    {
        if (!RetroArchHotkey.TryParse(hotkey.Text, out _))
        {
            MessageBox.Show(this, "Ungültige Tastenkombination. Beispiel: Ctrl+Shift+F1.");
            return;
        }
        settings.GestureActions = fields.ToDictionary(
            item => item.Key, item => ((Choice)item.Value.SelectedItem!).Id);
        settings.RetroArchMenuHotkey = hotkey.Text.Trim();
        settings.Save();
        DialogResult = DialogResult.OK;
        Close();
    }
}