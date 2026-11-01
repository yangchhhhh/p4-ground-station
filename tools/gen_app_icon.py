"""Generate a simple round ARGB8888 LVGL icon C file."""
import math
import sys

def write_icon(path, symbol, name, rgb):
    w = 112
    r, g, b = rgb
    cx = cy = (w - 1) / 2.0
    rad = w / 2.0 - 2
    buf = bytearray(w * w * 4)
    for y in range(w):
        for x in range(w):
            dx = x - cx
            dy = y - cy
            d = math.hypot(dx, dy)
            i = (y * w + x) * 4
            if d <= rad - 1:
                a = 255
            elif d < rad + 1:
                a = int(max(0, min(255, (rad + 1 - d) * 127)))
            else:
                a = 0
            rr, gg, bb = r, g, b
            # inner glyph: horizon for GY, gear-ish ring for settings, chevron for drone
            if symbol == "gy" and a:
                if abs(dy) < 3 and abs(dx) < rad * 0.55:
                    rr, gg, bb = 255, 255, 255
                elif dy < -8 and abs(dx) < 3:
                    rr, gg, bb = 255, 220, 80
            elif symbol == "set" and a:
                inner = math.hypot(dx, dy)
                if 18 < inner < 28:
                    rr, gg, bb = 230, 236, 242
            elif symbol == "drone" and a:
                if abs(dx) < 4 and -28 < dy < 20:
                    rr, gg, bb = 255, 255, 255
                if abs(dy + 8) < 4 and abs(dx) < 28:
                    rr, gg, bb = 255, 255, 255
            buf[i:i + 4] = bytes((rr, gg, bb, a))

    lines = []
    lines.append('#include "lvgl.h"')
    lines.append(f"const uint8_t {name}_map[] = {{")
    row = []
    for n, v in enumerate(buf):
        row.append(f"0x{v:02x}")
        if len(row) == 16:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    lines.append(f"const lv_image_dsc_t {name} = {{")
    lines.append("    .header.cf = LV_COLOR_FORMAT_ARGB8888,")
    lines.append("    .header.magic = LV_IMAGE_HEADER_MAGIC,")
    lines.append(f"    .header.w = {w},")
    lines.append(f"    .header.h = {w},")
    lines.append(f"    .data_size = {w * w * 4},")
    lines.append(f"    .data = {name}_map,")
    lines.append("};")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

if __name__ == "__main__":
    outdir = sys.argv[1]
    write_icon(f"{outdir}/icon_gy87.c", "gy", "icon_app_gy87_112", (26, 150, 212))
    write_icon(f"{outdir}/icon_settings.c", "set", "icon_app_settings_112", (90, 108, 125))
    write_icon(f"{outdir}/icon_drone.c", "drone", "icon_app_drone_112", (214, 110, 24))
