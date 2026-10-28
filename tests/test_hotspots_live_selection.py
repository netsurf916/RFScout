"""Regression checks for Hotspots Live selection before input dispatch."""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/apps/hotspots/hotspots_ui.c"


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"(?:static\s+)?(?:void|bool|size_t)\s+{re.escape(name)}\s*\([^;]*?\)\s*\{{",
        source,
    )
    if match is None:
        raise AssertionError(f"Missing {name}")
    start = source.index("{", match.start())
    depth = 0
    for end in range(start, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise AssertionError(f"Unterminated function {name}")


class HotspotsSelectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text()

    def test_network_cursor_is_reconciled_before_input(self):
        handler = function_body(self.source, "handle_menu_input")
        self.assertLess(handler.index("resolve_menu_selection( rows );"),
                        handler.index("step_index("))
        self.assertLess(handler.index("resolve_menu_selection( rows );"),
                        handler.index("switch( menu_selected )"))
        renderer = function_body(self.source, "hotspots_ui_render")
        self.assertIn("resolve_menu_selection( row_count );", renderer)

    def test_member_cursor_is_reconciled_before_input(self):
        handler = function_body(self.source, "handle_members_input")
        self.assertLess(handler.index("resolve_member_selection( group, count );"),
                        handler.index("step_index("))
        self.assertLess(handler.index("resolve_member_selection( group, count );"),
                        handler.index("RFSCOUT_INPUT_SELECT"))
        render = function_body(self.source, "render_members")
        self.assertIn("resolve_member_selection( group, count );", render)

    def test_vanished_items_cannot_redirect_selection(self):
        menu = function_body(self.source, "resolve_menu_selection")
        self.assertGreaterEqual(menu.count("menu_selected = MENU_BACK;"), 2)
        member = function_body(self.source, "resolve_member_selection")
        self.assertIn("members_selected = 0U;", member)
        self.assertIn("member_selected = false;", member)
        handler = function_body(self.source, "handle_members_input")
        self.assertIn("if( group >= group_count )", handler)
        self.assertIn("screen = SCREEN_MENU;", handler)


if __name__ == "__main__":
    unittest.main()
