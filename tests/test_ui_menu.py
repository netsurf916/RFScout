"""Host-side semantic menu contract tests using the production C implementation."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DISPLAY = ROOT / "main" / "services" / "display"

HARNESS = r'''
#include "ui_menu.h"
#include "ui_theme.h"
#include "ui_game.h"
#include "ui_icon.h"
#include <assert.h>
#include <string.h>
static ui_theme_palette_t palette = { .text = 11, .selected = 22, .accent = 33, .secondary = 44 };
static unsigned markers;
static unsigned locks;
static char last_marker[4];
const ui_theme_palette_t *ui_theme_get(void) { return &palette; }
void ui_game_draw_menu_marker(int x, int y, const char *marker, uint16_t color)
{ (void)x; (void)y; (void)color; ++markers; strcpy(last_marker, marker); }
void ui_icon_draw_lock(int x, int y, bool app, uint16_t color)
{ (void)x; (void)y; (void)app; (void)color; ++locks; }
static unsigned texts;
void display_draw_text(int x, int y, const char *text, uint16_t color, unsigned scale)
{ (void)x; (void)y; (void)text; (void)color; (void)scale; ++texts; }
int main(void)
{
    ui_menu_state_t normal = { .available = true };
    ui_menu_state_t selected = { .selected = true, .available = true };
    ui_menu_state_t editing = { .selected = true, .editing = true, .available = true };
    ui_menu_state_t unavailable = { .selected = true, .available = false };
    ui_menu_state_t locked = { .selected = true, .available = false, .locked = true };
    assert(ui_menu_label_color(normal) == 11);
    assert(ui_menu_label_color(selected) == 22);
    assert(ui_menu_value_color(selected) == 33);
    assert(ui_menu_label_color(editing) == 22);
    assert(ui_menu_label_color(unavailable) == 44);
    assert(ui_menu_value_color(unavailable) == 44);
    assert(ui_menu_label_color(locked) == 44);
    ui_menu_draw_marker(0, 25, normal);
    assert(markers == 0 && locks == 0);
    ui_menu_draw_marker(0, 25, selected);
    assert(markers == 1 && strcmp(last_marker, ">") == 0);
    ui_menu_draw_marker(0, 25, editing);
    assert(markers == 2 && strcmp(last_marker, "*") == 0);
    ui_menu_draw_marker(0, 25, unavailable);
    assert(markers == 3 && locks == 0);
    ui_menu_draw_marker(0, 25, locked);
    assert(markers == 4 && locks == 1);
    assert(ui_menu_window_start(0, 10, 5) == 0);
    assert(ui_menu_window_start(4, 10, 5) == 0);
    assert(ui_menu_window_start(5, 10, 5) == 1);
    assert(ui_menu_window_start(9, 10, 5) == 5);
    assert(ui_menu_window_start(3, 3, 5) == 0);
    const ui_menu_row_t rows[3] = { { "Back", NULL, { .available = true } },
                                    { "Auto", "On", { .available = true } },
                                    { "Item", NULL, { .available = true } } };
    assert(ui_menu_draw_rows(rows, 3, 2, 2, 38) == 1);
    assert(texts == 3 && markers == 5);
    return 0;
}
'''

class MenuPresentationTest(unittest.TestCase):
    def test_production_menu_semantics(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "esp_err.h").write_text("typedef int esp_err_t;\n")
            (temp / "display.h").write_text(
                "#include <stdint.h>\n"
                "void display_draw_text(int, int, const char *, uint16_t, unsigned);\n")
            (temp / "menu_harness.c").write_text(HARNESS)
            executable = temp / "menu_test"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(temp), "-I", str(DISPLAY),
                            str(DISPLAY / "ui_menu.c"), str(temp / "menu_harness.c"),
                            "-o", str(executable)], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

if __name__ == "__main__":
    unittest.main()
