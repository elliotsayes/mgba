import os
import shutil
import subprocess
import sys
import tempfile
from difflib import SequenceMatcher
from pathlib import Path

import mgba.image
import pytest


def save_screenshot_png(framebuffer):
    fd, screenshot_name = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    screenshot_path = Path(screenshot_name)
    try:
        # Prefer PIL output when available, as it is what existing video tests consume.
        if hasattr(framebuffer, "to_pil"):
            framebuffer.to_pil().convert("RGB").save(str(screenshot_path), format="PNG")
        else:
            with screenshot_path.open("wb") as screenshot:
                assert framebuffer.save_png(screenshot), "Failed to save screenshot"
    except Exception:
        if screenshot_path.exists():
            screenshot_path.unlink()
        raise
    return screenshot_path


def _luma_from_color(color):
    color_u32 = mgba.image.color_to_u32(color)
    red = color_u32 & 0xFF
    green = (color_u32 >> 8) & 0xFF
    blue = (color_u32 >> 16) & 0xFF
    return (red * 299 + green * 587 + blue * 114) // 1000


def _gray_to_color(gray):
    gray = max(0, min(255, int(gray)))
    color_u32 = gray | (gray << 8) | (gray << 16) | 0xFF000000
    return mgba.image.u32_to_color(color_u32)


def _build_scaled_variant(framebuffer, scale, mode, threshold=145, invert=False):
    variant = mgba.image.Image(
        framebuffer.width * scale,
        framebuffer.height * scale,
        alpha=framebuffer.alpha,
    )

    for y in range(framebuffer.height):
        src_base = y * framebuffer.stride
        dst_y_base = y * scale
        for x in range(framebuffer.width):
            color = framebuffer.buffer[src_base + x]

            if mode in ("gray", "binary"):
                luma = _luma_from_color(color)
                if mode == "binary":
                    luma = 255 if luma >= threshold else 0
                    if invert:
                        luma = 255 - luma
                color = _gray_to_color(luma)

            dst_x_base = x * scale
            for dy in range(scale):
                dst_base = (dst_y_base + dy) * variant.stride + dst_x_base
                for dx in range(scale):
                    variant.buffer[dst_base + dx] = color

    return variant


def _build_pil_ocr_variant(screenshot_path):
    try:
        from PIL import Image, ImageOps
    except Exception:
        return None

    with Image.open(screenshot_path) as image:
        gray = ImageOps.exif_transpose(image).convert("L")

    resampling = getattr(Image, "Resampling", Image)
    scaled = gray.resize((gray.width * 6, gray.height * 6), resample=resampling.NEAREST)
    high_contrast = ImageOps.autocontrast(scaled)
    # This setting reads the GBA menu text reliably in CI/local experiments.
    binary_inverted = ImageOps.invert(
        high_contrast.point(lambda px: 255 if px > 110 else 0).convert("L")
    )

    fd, path_str = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    path = Path(path_str)
    binary_inverted.save(str(path), format="PNG")
    return path


def ocr_text_from_png(screenshot_path, framebuffer):
    tesseract = shutil.which("tesseract")
    if tesseract is None:
        pytest.skip("tesseract is required for OCR checks")

    # Keep OCR fast by default: one strong PIL variant + original fallback.
    ocr_inputs = []
    temp_paths = []
    pil_variant = _build_pil_ocr_variant(screenshot_path)
    if pil_variant:
        temp_paths.append(pil_variant)
        ocr_inputs.append(("pil_bw110_inv_x6", pil_variant, (6,)))
    ocr_inputs.append(("original", screenshot_path, (6,)))

    # If Pillow is unavailable, fallback to framebuffer-derived variant.
    if not pil_variant or os.environ.get("MGBA_TEST_OCR_EXTRA"):
        try:
            variants = [
                (
                    "scaled_binary_inv_x4",
                    _build_scaled_variant(
                        framebuffer,
                        scale=4,
                        mode="binary",
                        threshold=110,
                        invert=True,
                    ),
                    (6,),
                ),
            ]
            if os.environ.get("MGBA_TEST_OCR_EXTRA"):
                variants.append(
                    (
                        "scaled_binary_x4",
                        _build_scaled_variant(
                            framebuffer, scale=4, mode="binary", threshold=110
                        ),
                        (6,),
                    )
                )

            for name, image, psms in variants:
                path = save_screenshot_png(image)
                temp_paths.append(path)
                ocr_inputs.append((name, path, psms))
        except Exception as error:
            print("Framebuffer OCR preprocessing skipped: {}".format(error))

    best_text = ""
    best_details = ("none", 0)
    try:
        for name, path, psms in ocr_inputs:
            for psm in psms:
                try:
                    result = subprocess.run(
                        [
                            tesseract,
                            str(path),
                            "stdout",
                            "--oem",
                            "1",
                            "--psm",
                            str(psm),
                            "--dpi",
                            "300",
                            "-l",
                            "eng",
                            "-c",
                            "preserve_interword_spaces=1",
                            "-c",
                            "tessedit_do_invert=0",
                        ],
                        check=True,
                        capture_output=True,
                        text=True,
                        timeout=4,
                    )
                except subprocess.CalledProcessError as error:
                    print(
                        "tesseract failed for variant={} psm={} rc={}".format(
                            name, psm, error.returncode
                        )
                    )
                    continue
                except subprocess.TimeoutExpired:
                    print("tesseract timed out for variant={} psm={}".format(name, psm))
                    continue
                text = " ".join(result.stdout.split())
                if len(text) > len(best_text):
                    best_text = text
                    best_details = (name, psm)
    finally:
        for path in temp_paths:
            if path.exists():
                path.unlink()

    print(
        "Best OCR variant={} psm={} length={}".format(
            best_details[0], best_details[1], len(best_text)
        )
    )
    return best_text


def _normalize_text(text):
    lowered = text.lower()
    return "".join(ch if ch.isalnum() or ch.isspace() else " " for ch in lowered)


def has_fuzzy_word(text, expected, threshold=0.72):
    words = [word for word in _normalize_text(text).split() if len(word) >= 3]
    for word in words:
        if SequenceMatcher(None, word, expected).ratio() >= threshold:
            return True
    return False


def assert_screen_keywords(screen_text):
    expected_keywords = ("wireless", "adapter", "command")
    matched = [word for word in expected_keywords if has_fuzzy_word(screen_text, word)]
    print("OCR keyword matches: {}".format(", ".join(matched) if matched else "<none>"))
    assert len(matched) >= 2, "OCR did not confidently match enough expected screen keywords"


def preview_requested():
    return bool(os.environ.get("MGBA_TEST_PREVIEW_SCREENSHOT"))


def preview_png(screenshot_path):
    if not preview_requested():
        return

    try:
        if sys.platform == "darwin":
            subprocess.Popen(["open", str(screenshot_path)])
            return
        if sys.platform.startswith("win"):
            os.startfile(str(screenshot_path))
            return
        subprocess.Popen(["xdg-open", str(screenshot_path)])
    except Exception as error:
        print("Screenshot preview failed: {}".format(error))


def capture_latest_frame(core):
    framebuffer = mgba.image.Image(*core.desired_video_dimensions())
    core.set_video_buffer(framebuffer)
    core.run_frame()
    return framebuffer
