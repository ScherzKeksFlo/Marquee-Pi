import io
import json
import os
import struct
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import configure_display as cd
from display_profile import parse_profile


# ---- fixture builders: a fake root with /sys/class/drm, /proc/bus/input/devices, ... -------------------

def make_edid(name="DELL U2412M", width=1920, height=1200, cm=(52, 32), maker="DEL"):
    data = bytearray(128)
    data[:8] = b"\x00\xff\xff\xff\xff\xff\xff\x00"
    code = ((ord(maker[0]) - 64) << 10) | ((ord(maker[1]) - 64) << 5) | (ord(maker[2]) - 64)
    data[8], data[9] = code >> 8, code & 0xFF
    data[21], data[22] = cm
    timing = bytearray(18)
    timing[0], timing[1] = 0x01, 0x1D  # a non-zero pixel clock marks a detailed timing
    timing[2], timing[4] = width & 0xFF, (width >> 8) << 4
    timing[5], timing[7] = height & 0xFF, (height >> 8) << 4
    data[54:72] = timing
    if name:
        block = bytearray(18)
        block[3] = 0xFC
        text = name.encode("ascii")[:13]
        block[5:5 + len(text)] = text
        if len(text) < 13:
            block[5 + len(text)] = 0x0A
            for i in range(5 + len(text) + 1, 18):
                block[i] = 0x20
        data[72:90] = block
    return bytes(data)


def bitmap(bits, word_bits=64):
    value = 0
    for bit in bits:
        value |= 1 << bit
    words = []
    while value:
        words.append(value & ((1 << word_bits) - 1))
        value >>= word_bits
    return " ".join("%x" % word for word in reversed(words)) or "0"


def input_block(name, bus="0003", handlers="mouse0 event0", props=(), absolute=(), rel=False, word_bits=64):
    lines = ['I: Bus=%s Vendor=0001 Product=0001 Version=0100' % bus, 'N: Name="%s"' % name, "P: Phys=", "U: Uniq=",
             "H: Handlers=%s" % handlers, "B: PROP=%s" % bitmap(props, word_bits), "B: EV=%s" % ("b" if absolute else "17")]
    if absolute:
        lines.append("B: ABS=%s" % bitmap(absolute, word_bits))
    if rel:
        lines.append("B: REL=903")
    return "\n".join(lines) + "\n"


MT = (cd.ABS_MT_POSITION_X, cd.ABS_MT_POSITION_Y, 0x2F, 0x39)


def make_root(directory, connectors, inputs="", model="Raspberry Pi 3 Model B Plus Rev 1.3", cmdline=None,
              backlights=()):
    root = Path(directory) / "root"
    drm = root / "sys/class/drm"
    drm.mkdir(parents=True)
    (drm / "version").write_text("x")  # not a connector: must be ignored
    for name, info in connectors.items():
        folder = drm / name
        folder.mkdir()
        (folder / "status").write_text(info.get("status", "connected") + "\n")
        (folder / "modes").write_text("".join(mode + "\n" for mode in info.get("modes", [])))
        if info.get("edid"):
            (folder / "edid").write_bytes(info["edid"])
    (root / "proc/bus/input").mkdir(parents=True)
    (root / "proc/bus/input/devices").write_text(inputs)
    (root / "proc/device-tree").mkdir(parents=True)
    (root / "proc/device-tree/model").write_bytes(model.encode() + b"\x00")
    if cmdline is not None:
        (root / "boot/firmware").mkdir(parents=True)
        (root / "boot/firmware/cmdline.txt").write_text(cmdline)
    for name in backlights:
        folder = root / "sys/class/backlight" / name
        folder.mkdir(parents=True)
        (folder / "brightness").write_text("100")
        (folder / "max_brightness").write_text("255")
    (root / "etc/marquee-pi").mkdir(parents=True)
    return root


DSI_PI = {
    "card0-Composite-1": {"status": "connected", "modes": ["720x480", "720x576"]},
    "card0-DSI-1": {"status": "connected", "modes": ["800x480"]},
    "card0-HDMI-A-1": {"status": "disconnected"},
}
FT5X06 = input_block("generic ft5x06 (79)", "0018", "mouse0 event0", (cd.INPUT_PROP_DIRECT.bit_length() - 1,), MT)
HDMI_MONITOR = {"card1-HDMI-A-1": {"status": "connected", "modes": ["1920x1080", "1280x720", "1920x1080"],
                                   "edid": make_edid("DELL U2412M", 1920, 1080)},
                "card1-HDMI-A-2": {"status": "disconnected"}}
CMDLINE = "console=serial0,115200 console=tty1 root=PARTUUID=abcd-02 rootfstype=ext4 fsck.repair=yes rootwait\n"
TOKEN_CONFIG = {"bind": "0.0.0.0", "port": 8765, "token": "t" * 40, "data_dir": "DATA"}


class ScriptedPrompter(cd.Prompter):
    def __init__(self, answers, timeout_answer="y"):
        self.answers = list(answers)
        self.timeout_answer = timeout_answer
        self.said = []

    def say(self, text=""):
        self.said.append(text)

    def ask(self, question, default=None, choices=None):
        answer = self.answers.pop(0)
        if answer == "":
            answer = default if default is not None else ""
        if choices is not None and answer not in choices:
            raise AssertionError("unexpected answer %r for %r (choices %r)" % (answer, question, choices))
        return answer

    def ask_timeout(self, question, seconds):
        return self.timeout_answer


def context(root, answers=(), **kwargs):
    config = root / "etc/marquee-pi/config.json"
    if not config.exists():
        data = dict(TOKEN_CONFIG, data_dir=str(root / "var/lib/marquee-pi"))
        config.write_text(json.dumps(data), encoding="utf-8")
        (root / "var/lib/marquee-pi").mkdir(parents=True, exist_ok=True)
    return cd.Context(cd.System(str(root)), config, ScriptedPrompter(answers), restart=False, **kwargs)


def saved_display(root):
    return json.loads((root / "etc/marquee-pi/config.json").read_text(encoding="utf-8")).get("display")


class EdidTests(unittest.TestCase):
    def test_name_preferred_mode_and_size(self):
        info = cd.parse_edid(make_edid("DELL U2412M", 1920, 1200, (52, 32)))
        self.assertEqual(info, {"name": "DELL U2412M", "preferred": "1920x1200", "size_cm": (52, 32)})

    def test_unnamed_monitor_falls_back_to_the_manufacturer_and_unknown_size_is_none(self):
        info = cd.parse_edid(make_edid(None, 1280, 720, (0, 0), "SAM"))
        self.assertEqual((info["name"], info["size_cm"], info["preferred"]), ("SAM", None, "1280x720"))

    def test_garbage_is_rejected(self):
        self.assertIsNone(cd.parse_edid(b"\x00" * 10))
        self.assertIsNone(cd.parse_edid(b"x" * 128))


class DetectionTests(unittest.TestCase):
    def test_connectors_get_the_x_output_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, dict(DSI_PI, **HDMI_MONITOR))
            found = {c.output: c for c in cd.detect_connectors(cd.System(str(root)))}
        self.assertEqual(sorted(found), ["Composite-1", "DSI-1", "HDMI-1", "HDMI-2"])
        self.assertTrue(found["DSI-1"].connected and found["DSI-1"].is_dsi)
        self.assertFalse(found["HDMI-2"].connected)
        self.assertEqual(found["HDMI-1"].modes, ["1920x1080", "1280x720"])  # duplicates removed, order kept
        self.assertEqual((found["HDMI-1"].edid_name, found["HDMI-1"].preferred), ("DELL U2412M", "1920x1080"))
        self.assertEqual(found["DSI-1"].modes, ["800x480"])

    def test_touchscreens_are_told_from_mice_touchpads_and_keyboards(self):
        inputs = "\n".join([
            FT5X06,
            input_block("ILITEK ILITEK-TP", "0003", "mouse1 event1", (1,), MT),
            input_block("Logitech USB Receiver", "0003", "mouse2 event2", (), (), rel=True),
            input_block("Some Touchpad", "0003", "mouse3 event3", (0, 2), (0, 1, 0x35, 0x36)),  # pointer, not direct
            input_block("Keyboard", "0003", "sysrq kbd event4", (), ()),
            input_block("Single touch panel", "0003", "mouse5 event5", (1,), (0, 1)),
        ])
        for word_bits in (32, 64):
            with self.subTest(word_bits=word_bits), tempfile.TemporaryDirectory() as directory:
                text = inputs if word_bits == 64 else "\n".join([
                    input_block("generic ft5x06 (79)", "0018", "mouse0 event0", (1,), MT, word_bits=32),
                    input_block("Some Touchpad", "0003", "mouse3 event3", (0, 2), (0, 1, 0x35, 0x36), word_bits=32)])
                root = make_root(directory, {}, text)
                devices = {d.name: d for d in cd.detect_touch(cd.System(str(root)), word_bits)}
            self.assertTrue(devices["generic ft5x06 (79)"].is_touchscreen)
            self.assertEqual(devices["generic ft5x06 (79)"].bus, "I2C")
            self.assertEqual(devices["generic ft5x06 (79)"].event, "event0")
            self.assertFalse(devices["Some Touchpad"].is_touchscreen)
            if word_bits == 64:
                self.assertTrue(devices["ILITEK ILITEK-TP"].is_touchscreen)
                self.assertEqual(devices["ILITEK ILITEK-TP"].bus, "USB")
                self.assertTrue(devices["Single touch panel"].is_touchscreen)
                self.assertFalse(devices["Logitech USB Receiver"].is_touchscreen)
                self.assertFalse(devices["Keyboard"].is_touchscreen)

    def test_model_backlight_and_default_choice(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, dict(DSI_PI, **HDMI_MONITOR), backlights=["rpi_backlight"])
            system = cd.System(str(root))
            self.assertEqual(cd.detect_model(system), "Raspberry Pi 3 Model B Plus Rev 1.3")
            self.assertEqual(cd.detect_backlights(system), ["rpi_backlight"])
            self.assertEqual(cd.choose_default(cd.detect_connectors(system)).output, "DSI-1")  # DSI wins
            only_hdmi = make_root(Path(directory) / "two", HDMI_MONITOR)
            self.assertEqual(cd.choose_default(cd.detect_connectors(cd.System(str(only_hdmi)))).output, "HDMI-1")
            none = make_root(Path(directory) / "three", {"card1-HDMI-A-1": {"status": "disconnected"}})
            self.assertIsNone(cd.choose_default(cd.detect_connectors(cd.System(str(none)))))

    def test_missing_system_files_mean_nothing_found(self):
        with tempfile.TemporaryDirectory() as directory:
            system = cd.System(directory)
            self.assertEqual(cd.detect_connectors(system), [])
            self.assertEqual(cd.detect_touch(system), [])
            self.assertEqual(cd.detect_backlights(system), [])
            self.assertIsNone(cd.detect_model(system))


class BootConfigTests(unittest.TestCase):
    def connectors(self, directory, layout):
        root = make_root(directory, layout, cmdline=CMDLINE)
        return root, cd.detect_connectors(cd.System(str(root)))

    def test_dsi_needs_a_video_entry_and_the_composite_port_is_switched_off(self):
        with tempfile.TemporaryDirectory() as directory:
            root, connectors = self.connectors(directory, DSI_PI)
            dsi = next(c for c in connectors if c.output == "DSI-1")
            plan = cd.plan_boot_changes(dsi, "auto", CMDLINE, connectors)
            self.assertEqual(plan.add, ["video=DSI-1:800x480@60", "video=Composite-1:d"])
            self.assertEqual(len(plan.why), 2)

    def test_nothing_to_do_when_the_entries_exist_or_for_hdmi(self):
        with tempfile.TemporaryDirectory() as directory:
            root, connectors = self.connectors(directory, dict(DSI_PI, **HDMI_MONITOR))
            dsi = next(c for c in connectors if c.output == "DSI-1")
            done = CMDLINE.strip() + " video=Composite-1:d video=DSI-1:800x480@60\n"
            self.assertTrue(cd.plan_boot_changes(dsi, "auto", done, connectors).empty())
            hdmi = next(c for c in connectors if c.output == "HDMI-1")
            self.assertTrue(cd.plan_boot_changes(hdmi, "auto", CMDLINE, connectors).empty())

    def test_apply_keeps_one_line_makes_a_backup_and_revert_restores(self):
        with tempfile.TemporaryDirectory() as directory:
            root, connectors = self.connectors(directory, DSI_PI)
            system = cd.System(str(root))
            data = root / "data"
            data.mkdir()
            dsi = next(c for c in connectors if c.is_dsi)
            plan = cd.plan_boot_changes(dsi, "auto", CMDLINE, connectors)
            cd.apply_boot_plan(system, plan, data)
            path = root / "boot/firmware/cmdline.txt"
            written = path.read_text()
            self.assertEqual(written.count("\n"), 1)
            self.assertTrue(written.startswith(CMDLINE.strip()) and written.strip().endswith("video=Composite-1:d"))
            self.assertEqual((root / "boot/firmware/cmdline.txt.marquee-pi-before-display").read_text(), CMDLINE)
            cd.apply_boot_plan(system, plan, data)  # idempotent: nothing added twice
            self.assertEqual(path.read_text(), written)
            said = []
            self.assertEqual(cd.revert_boot_changes(system, data, said.append), 0)
            self.assertEqual(path.read_text().split(), CMDLINE.split())
            self.assertFalse((data / cd.BOOT_RECORD).exists())
            cd.revert_boot_changes(system, data, said.append)
            self.assertIn("No boot configuration changes", said[-1])

    def test_a_cmdline_without_root_is_left_alone(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, DSI_PI, cmdline="console=tty1\n")
            plan = cd.BootPlan(cd.CMDLINE, ["video=DSI-1:800x480@60"], [])
            with self.assertRaises(SystemExit):
                cd.apply_boot_plan(cd.System(str(root)), plan, root)
            self.assertEqual((root / "boot/firmware/cmdline.txt").read_text(), "console=tty1\n")

    def test_instructions_file(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = cd.build_profile(None, None, None)
            target = Path(directory) / "changes.txt"
            cd.write_instructions(cd.BootPlan(cd.CMDLINE, ["video=DSI-1:800x480@60"], ["because"]), target, profile)
            text = target.read_text()
            self.assertIn("video=DSI-1:800x480@60", text)
            self.assertIn("ONE line", text)
            cd.write_instructions(cd.BootPlan(cd.CMDLINE, [], []), target, profile)
            self.assertIn("No change to the boot configuration is needed", target.read_text())


class AutoTests(unittest.TestCase):
    def test_a_profile_is_derived_from_the_connected_screen_without_boot_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, DSI_PI, FT5X06, cmdline=CMDLINE, backlights=["rpi_backlight"])
            ctx = context(root)
            self.assertEqual(cd.run_auto(ctx, False, lambda *_: None), 0)
            self.assertEqual(saved_display(root), {
                "output": "DSI-1", "mode": "auto", "rotation": 0, "fit": "contain", "scale": "auto",
                "touch": {"device": "generic ft5x06 (79)"}, "backlight": "rpi_backlight"})
            self.assertEqual((root / "boot/firmware/cmdline.txt").read_text(), CMDLINE)
            config = json.loads((root / "etc/marquee-pi/config.json").read_text())
            self.assertEqual(config["token"], "t" * 40)  # the rest of the configuration is untouched
            self.assertIn("ReadWritePaths=-", cd.dropin_path(ctx.system).read_text())

    def test_an_existing_profile_is_kept_unless_forced(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, dict(DSI_PI, **HDMI_MONITOR), FT5X06)
            ctx = context(root)
            config = root / "etc/marquee-pi/config.json"
            data = json.loads(config.read_text())
            data["display"] = {"output": "HDMI-1", "rotation": 90, "touch": None}
            config.write_text(json.dumps(data))
            cd.run_auto(ctx, False, lambda *_: None)
            self.assertEqual(saved_display(root)["output"], "HDMI-1")
            cd.run_auto(ctx, True, lambda *_: None)
            self.assertEqual(saved_display(root)["output"], "DSI-1")

    def test_no_screen_and_no_touch_gives_a_view_only_profile_with_automatic_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, {"card1-HDMI-A-1": {"status": "disconnected"}})
            cd.run_auto(context(root), False, lambda *_: None)
            display = saved_display(root)
            self.assertEqual((display["output"], display["touch"]), (None, None))

    def test_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, DSI_PI, FT5X06)
            cd.run_auto(context(root, dry_run=True), False, lambda *_: None)
            self.assertIsNone(saved_display(root))

    def test_the_old_backlight_dropin_is_removed_when_the_profile_has_none(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, dict(HDMI_MONITOR), backlights=["x"])
            system = cd.System(str(root))
            with_light, _ = parse_profile({"backlight": "x"})
            self.assertTrue(cd.write_backlight_dropin(system, with_light))
            self.assertFalse(cd.write_backlight_dropin(system, with_light))  # unchanged
            without, _ = parse_profile({})
            self.assertTrue(cd.write_backlight_dropin(system, without))
            self.assertFalse(cd.dropin_path(system).exists())


class InteractiveTests(unittest.TestCase):
    def test_hdmi_monitor_with_rotation_fit_and_the_tapped_touch_device(self):
        with tempfile.TemporaryDirectory() as directory:
            inputs = "\n".join([FT5X06, input_block("ILITEK ILITEK-TP", "0003", "mouse1 event1", (1,), MT)])
            root = make_root(directory, dict(DSI_PI, **HDMI_MONITOR), inputs, cmdline=CMDLINE)
            # 1 = DSI? the list is: Composite? no, only connected ones: Composite-1, DSI-1, HDMI-1 -> HDMI-1 is 3
            ctx = context(root, ["3", "1280x720", "90", "cover", "y"])
            tapped = cd.detect_touch(ctx.system)[1]
            ctx.identify = lambda devices: tapped
            self.assertEqual(cd.run_interactive(ctx), 0)
            display = saved_display(root)
            self.assertEqual((display["output"], display["mode"], display["rotation"], display["fit"]),
                             ("HDMI-1", "1280x720", 90, "cover"))
            self.assertEqual(display["touch"], {"device": "ILITEK ILITEK-TP"})
            self.assertEqual((root / "boot/firmware/cmdline.txt").read_text(), CMDLINE)  # HDMI: no boot change

    def test_no_touch_device_makes_a_view_only_display(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, HDMI_MONITOR, cmdline=CMDLINE)
            ctx = context(root, ["1", "auto", "0", "contain", "y"])
            self.assertEqual(cd.run_interactive(ctx), 0)
            display = saved_display(root)
            self.assertIsNone(display["touch"])
            self.assertTrue(any("view-only" in line for line in ctx.prompter.said))

    def test_single_touch_device_can_be_declined(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, HDMI_MONITOR, FT5X06, cmdline=CMDLINE)
            ctx = context(root, ["1", "auto", "0", "contain", "n", "y"])
            cd.run_interactive(ctx)
            self.assertIsNone(saved_display(root)["touch"])

    def test_dsi_boot_change_applied_after_confirmation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, DSI_PI, FT5X06, cmdline=CMDLINE, backlights=["rpi_backlight"])
            ctx = context(root, ["2", "0", "contain", "y", "1", "y"])  # DSI-1 is the 2nd screen; apply; save
            self.assertEqual(cd.run_interactive(ctx), 0)
            self.assertIn("video=DSI-1:800x480@60", (root / "boot/firmware/cmdline.txt").read_text())
            self.assertTrue((root / "var/lib/marquee-pi" / cd.BOOT_RECORD).exists())
            self.assertEqual(saved_display(root)["backlight"], "rpi_backlight")

    def test_dsi_boot_change_written_to_a_file_instead(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, DSI_PI, FT5X06, cmdline=CMDLINE)
            target = Path(directory) / "by-hand.txt"
            ctx = context(root, ["2", "0", "contain", "y", "2", "y"], instructions=target)
            cd.run_interactive(ctx)
            self.assertIn("video=DSI-1:800x480@60", target.read_text())
            self.assertEqual((root / "boot/firmware/cmdline.txt").read_text(), CMDLINE)  # left alone
            self.assertIsNotNone(saved_display(root))

    def test_declining_the_final_question_changes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, HDMI_MONITOR, cmdline=CMDLINE)
            ctx = context(root, ["1", "auto", "0", "contain", "n"])
            self.assertEqual(cd.run_interactive(ctx), 0)
            self.assertIsNone(saved_display(root))

    def test_no_connected_screen_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, {"card1-HDMI-A-1": {"status": "disconnected"}})
            ctx = context(root)
            self.assertEqual(cd.run_interactive(ctx), 1)
            self.assertIsNone(saved_display(root))


class TestPictureTests(unittest.TestCase):
    """The confirmation step with systemd faked: the new profile is kept only when confirmed."""

    def make(self, directory, answer):
        root = make_root(directory, HDMI_MONITOR, cmdline=CMDLINE)
        ctx = context(root)
        ctx.prompter = ScriptedPrompter([], timeout_answer=answer)
        ctx.systemd = lambda: True
        commands = []
        ctx.run = lambda arguments: (commands.append(arguments) or (0, ""))
        old = {"token": "t" * 40, "display": {"output": "DSI-1", "touch": None}}
        (root / "etc/marquee-pi/config.json").write_text(json.dumps(dict(old, data_dir=str(root / "var/lib/marquee-pi"))))
        return root, ctx, commands

    def test_confirmed_profile_stays_and_the_flag_is_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            root, ctx, commands = self.make(directory, "y")
            profile = cd.build_profile(cd.detect_connectors(ctx.system)[0], None, None)
            self.assertTrue(cd.test_and_confirm(ctx, cd.load_config(ctx.config_path), profile, lambda *_: None))
            self.assertEqual(saved_display(root)["output"], "HDMI-1")
            self.assertFalse((root / "var/lib/marquee-pi" / cd.TEST_FLAG).exists())
            self.assertIn(["systemctl", "restart", "marquee-pi-api", "marquee-pi-kiosk"], commands)

    def test_no_answer_and_no_both_restore_the_old_profile(self):
        for answer in (None, "n"):
            with self.subTest(answer=answer), tempfile.TemporaryDirectory() as directory:
                root, ctx, commands = self.make(directory, answer)
                profile = cd.build_profile(cd.detect_connectors(ctx.system)[0], None, None)
                said = []
                self.assertFalse(cd.test_and_confirm(ctx, cd.load_config(ctx.config_path), profile, said.append))
                self.assertEqual(saved_display(root), {"output": "DSI-1", "touch": None})
                self.assertFalse((root / "var/lib/marquee-pi" / cd.TEST_FLAG).exists())
                self.assertEqual(commands.count(["systemctl", "restart", "marquee-pi-api", "marquee-pi-kiosk"]), 2)


@unittest.skipUnless(os.name == "posix", "select() on files needs a POSIX system")
class IdentifyTests(unittest.TestCase):
    def test_the_device_that_reports_the_touch_is_returned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "dev/input").mkdir(parents=True)
            size = struct.calcsize("llHHi")
            quiet = struct.pack("llHHi", 0, 0, 0, 0, 0)  # an EV_SYN report only
            touch = struct.pack("llHHi", 0, 0, cd.EV_KEY, cd.BTN_TOUCH, 1)
            (root / "dev/input/event0").write_bytes(quiet)
            (root / "dev/input/event1").write_bytes(touch)
            self.assertEqual(size, len(touch))
            devices = [cd.TouchDevice("first", "event0", "USB", True), cd.TouchDevice("second", "event1", "USB", True)]
            found = cd.identify_touch(cd.System(str(root)), devices, seconds=1)
            self.assertEqual(found.name, "second")
            self.assertIsNone(cd.identify_touch(cd.System(str(root)), [devices[0]], seconds=1))  # only a sync report


class CommandLineTests(unittest.TestCase):
    def run_main(self, *arguments):
        out = io.StringIO()
        with redirect_stdout(out):
            code = cd.main(list(arguments))
        return code, out.getvalue()

    def test_json_report_and_dry_run_auto(self):
        with tempfile.TemporaryDirectory() as directory:
            root = make_root(directory, dict(DSI_PI, **HDMI_MONITOR), FT5X06, cmdline=CMDLINE)
            code, text = self.run_main("--json", "--root", str(root))
            self.assertEqual(code, 0)
            report = json.loads(text)
            self.assertEqual(report["model"], "Raspberry Pi 3 Model B Plus Rev 1.3")
            self.assertEqual(sorted(c["output"] for c in report["connectors"]), ["Composite-1", "DSI-1", "HDMI-1", "HDMI-2"])
            self.assertEqual(report["touch"][0]["name"], "generic ft5x06 (79)")
            config = root / "etc/marquee-pi/config.json"
            config.write_text(json.dumps(TOKEN_CONFIG))
            code, text = self.run_main("--auto", "--dry-run", "--root", str(root), "--config", str(config))
            self.assertEqual(code, 0)
            self.assertIn("screen:   DSI-1", text)
            self.assertIn("dry run", text)
            self.assertNotIn("display", json.loads(config.read_text()))


if __name__ == "__main__":
    unittest.main()
