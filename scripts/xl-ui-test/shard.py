"""Shared standalone MPC model defaults and view assignment for all hardware test phases."""
from dataclasses import dataclass
from device import Key61, XL
from inventory import VIEW_IDS


@dataclass(frozen=True)
class Model:
    adapter: type
    host: str
    mount: str
    helper_dir: str
    helpers: tuple
    elf_class: int
    elf_machine: int

    @property
    def tui(self):
        return self.mount + "/VST2 Plugins/Safian - VST - SynthyBoy/Plugin Skins/TUI.json"


MODELS = {
    "xl": Model(XL, "root@192.168.0.126", "/nvme", "/tmp/synthyboy-xl-ui-test",
                ("xtouch", "drmgrab", "pgrab"), 2, 183),
    "key61": Model(Key61, "root@192.168.0.30", "/sdcard", "/tmp/synthyboy-key61-ui-test",
                   ("mtouch", "drmgrab", "sb-restore-accept-pgrab"), 1, 40),
}


def add_shard(parser):
    parser.add_argument("--device", choices=MODELS, default="xl")
    parser.add_argument("--view", action="append", choices=VIEW_IDS.values(), help="select an owning view")
    parser.add_argument("--exclude-view", action="append", choices=VIEW_IDS.values(), help="skip an owning view")


def selected(args, parser):
    default = [v for v in VIEW_IDS.values() if (v == "oscnoise") == (args.device == "key61")]
    chosen = [v for v in VIEW_IDS.values() if v in (args.view or default) and v not in (args.exclude_view or [])]
    if not chosen:
        parser.error("no views remain in the selected shard")
    return chosen
