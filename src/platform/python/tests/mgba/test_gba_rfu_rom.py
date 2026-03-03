from pathlib import Path

import mgba.core
import mgba.gba as gba
import mgba.image
import pytest
from mgba._pylib import lib

from . import demo_rom


ROM_PATH = Path(__file__).resolve().parents[5] / "fixtures" / "LinkRawWireless_demo.gba"
EXPECTED_TILEMAP_TEXT = """state = AUTHENTICATED     p0
LinkRawWireless_demo
  (v8.0.3)
START: reset wireless adapter
RIGHT: restore from multiboot
A: send command
B: toggle log level
UP/DOWN: scroll up/down
L/R: scroll page up/down
UP+L/DOWN+R: scroll to top/botto
SELECT: clear
---
! setting log level to NORMAL
> resetting adapter...
< success :)"""


@pytest.fixture(autouse=True)
def configure_log_filter():
    lib.mLoggerPythonUseStdLogger(lib.mLOG_ALL)
    sup_levels = lib.mLOG_WARN | lib.mLOG_ERROR | lib.mLOG_FATAL | lib.mLOG_GAME_ERROR
    lib.mLoggerPythonStdFilterSet(b"gba.dma", sup_levels)
    lib.mLoggerPythonStdFilterSet(b"gba.bios", sup_levels)
    hi_levels = sup_levels | lib.mLOG_INFO | lib.mLOG_DEBUG
    lib.mLoggerPythonStdFilterSet(b"gba.sio", hi_levels)
    yield
    lib.mLoggerPythonClearStdLogger()


def test_rfu_with_raw_wireless_demo_receives_data():
    assert ROM_PATH.is_file(), "Missing fixture ROM: {}".format(ROM_PATH)

    core = mgba.core.load_path(str(ROM_PATH))
    assert core is not None
    assert isinstance(core, gba.GBA)

    framebuffer = mgba.image.Image(*core.desired_video_dimensions())
    core.set_video_buffer(framebuffer)

    print("Resetting core")
    core.reset()

    print("Creating RFU")
    rfu = gba.RFUDriver()
    print("Connecting RFU")
    assert rfu.connect()

    print("Attaching RFU")
    core.attach_sio(rfu)

    print("running for 10,000 frames")
    for i in range(10_000):
        # print(f"f{i}")
        core.run_frame()

    print("pressing Start")
    core.set_keys(gba.GBA.KEY_START)
    for i in range(100):
        core.run_frame()
    core.clear_keys(gba.GBA.KEY_START)
    for i in range(100):
        core.run_frame()

    print("running for 10,000 frames")
    for i in range(10_000):
        core.run_frame()

    tilemap_lines = demo_rom.extract_gba_bg0_text_lines(core)
    screen_text = "\n".join(line for _, line in tilemap_lines)
    print("Tilemap text:\n{}".format(screen_text))
    assert screen_text == EXPECTED_TILEMAP_TEXT
    assert int(rfu._native.startCount) >= 5
    assert int(rfu._native.finishCount) >= 5
    assert int(rfu._native.comState) == lib.GBASIO_RFU_COM_WAIT_CMD

    print("detaching RFU")
    core.detach_sio()
    rfu.disconnect()
