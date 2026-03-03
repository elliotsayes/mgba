# LinkRawWireless_demo draws text using BG0 tile IDs that map directly to a small
# fixed character set. Decoding tilemap text is deterministic and fast.
_GBA_TILE_CHARMAP = {
    0: " ",
    1: "!",
    8: "(",
    9: ")",
    10: "*",
    11: "+",
    12: ",",
    13: "-",
    14: ".",
    15: "/",
    26: ":",
    27: ";",
    28: "<",
    29: "=",
    30: ">",
    31: "?",
    63: "_",
}
for _digit in range(10):
    _GBA_TILE_CHARMAP[16 + _digit] = str(_digit)
for _offset in range(26):
    _GBA_TILE_CHARMAP[33 + _offset] = chr(ord("A") + _offset)
for _offset in range(26):
    _GBA_TILE_CHARMAP[65 + _offset] = chr(ord("a") + _offset)


def _decode_gba_tile_char(tile_id):
    return _GBA_TILE_CHARMAP.get(tile_id, "?")


def extract_gba_bg0_text_lines(core, rows=20, columns=32):
    dispcnt = core.memory.io.u16[0x00]
    mode = dispcnt & 0x7
    bg0_enabled = bool(dispcnt & 0x0100)
    if mode != 0 or not bg0_enabled:
        raise AssertionError(
            "Expected Mode 0 with BG0 enabled, got DISPCNT={:04X}".format(dispcnt)
        )

    bg0cnt = core.memory.io.u16[0x08]
    screen_base = ((bg0cnt >> 8) & 0x1F) * 0x800
    vram_u16 = core.memory.vram.u16

    lines = []
    for y in range(rows):
        chars = []
        for x in range(columns):
            tile_entry = vram_u16[screen_base + ((y * 32 + x) * 2)]
            tile_id = tile_entry & 0x3FF
            chars.append(_decode_gba_tile_char(tile_id))
        line = "".join(chars).rstrip()
        if line.strip():
            lines.append((y, line))
    return lines
