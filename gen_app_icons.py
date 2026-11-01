from pathlib import Path

W = 64
FONTS = {
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "W": ["10001", "10001", "10001", "10101", "10101", "01010", "01010"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
}


def pix(letter, rgb):
    r, g, b = rgb
    rows = FONTS[letter]
    scale = 6
    ox = (W - 5 * scale) // 2
    oy = (W - 7 * scale) // 2
    data = []
    for y in range(W):
        for x in range(W):
            dx = min(x, W - 1 - x, y, W - 1 - y)
            if dx < 4:
                a = 0
                pr = pg = pb = 0
            else:
                a = 255
                pr, pg, pb = rgb
                gx = (x - ox) // scale
                gy = (y - oy) // scale
                if 0 <= gx < 5 and 0 <= gy < 7 and rows[gy][gx] == "1":
                    pr = pg = pb = 255
            data += [pb, pg, pr, a]
    return data


BASE = Path(__file__).resolve().parent / "components"
APPS = [
    ("app_humid", "icon_humid.c", "icon_app_humid_64", "H", (42, 196, 128)),
    ("app_weather", "icon_weather.c", "icon_app_weather_64", "W", (61, 140, 255)),
    ("app_files", "icon_files.c", "icon_app_files_64", "F", (245, 176, 65)),
    ("app_game_2048", "icon_2048.c", "icon_app_2048_64", "2", (232, 120, 48)),
    ("app_gps", "icon_gps.c", "icon_app_gps_64", "G", (61, 184, 255)),
    ("app_map", "icon_map.c", "icon_app_map_64", "M", (232, 76, 60)),
]

for folder, fname, sym, letter, rgb in APPS:
    d = pix(letter, rgb)
    out = BASE / folder
    out.mkdir(parents=True, exist_ok=True)
    lines = [
        '#include "lvgl.h"',
        "",
        f"const unsigned char {sym}_map[] = {{",
    ]
    for i in range(0, len(d), 16):
        chunk = ", ".join(f"0x{v:02x}" for v in d[i : i + 16])
        lines.append(f"    {chunk},")
    lines += [
        "};",
        f"const lv_image_dsc_t {sym} = {{",
        "    .header.cf = LV_COLOR_FORMAT_ARGB8888,",
        "    .header.magic = LV_IMAGE_HEADER_MAGIC,",
        "    .header.w = 64,",
        "    .header.h = 64,",
        f"    .data_size = {len(d)},",
        f"    .data = {sym}_map,",
        "};",
        "",
    ]
    path = out / fname
    path.write_text("\n".join(lines), encoding="utf-8")
    print("wrote", path)
