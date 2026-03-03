from pathlib import Path

import mgba.core
import mgba.gba as gba
import mgba.image
import pytest
from mgba._pylib import lib

from . import demo_rom


ROM_PATH = Path(__file__).resolve().parents[5] / "fixtures" / "LinkRawWireless_demo.gba"
SIODATA32_LO_REG = 0x90
SIODATA32_HI_REG = 0x91


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

    framebuffer = demo_rom.capture_latest_frame(core)
    screenshot_path = demo_rom.save_screenshot_png(framebuffer)
    print(f"Screenshot saved to {screenshot_path}")
    keep_screenshot = demo_rom.preview_requested()
    try:
        demo_rom.preview_png(screenshot_path)
        screen_text = demo_rom.ocr_text_from_png(screenshot_path, framebuffer=framebuffer)
        if not screen_text:
            keep_screenshot = True
    finally:
        if screenshot_path.exists() and not keep_screenshot:
            screenshot_path.unlink()

    print("OCR text:\n{}".format(screen_text))
    if keep_screenshot:
        print("Screenshot kept at {}".format(screenshot_path))
    assert screen_text, "OCR returned no text from screenshot: {}".format(screenshot_path)
    demo_rom.assert_screen_keywords(screen_text)
    assert not demo_rom.has_fuzzy_word(screen_text, "failure")

    # print("pressing A button")
    # core.set_keys(gba.GBA.KEY_A)
    # for i in range(100):
    #     core.run_frame()

    # print("running for 10,000 frames")
    # for i in range(10_000):
    #     core.run_frame()

    # print("press A button again")
    # core.set_keys(gba.GBA.KEY_A)
    # for i in range(100):
    #     core.run_frame()

    # print("running for 10,000 frames")
    # for i in range(10_000):
    #     core.run_frame()

    print("detaching RFU")
    core.detach_sio()
    rfu.disconnect()
