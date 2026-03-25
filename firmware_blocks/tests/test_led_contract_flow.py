import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
FW_ROOT = REPO_ROOT / "firmware_blocks"


class LedContractFlowTests(unittest.TestCase):
    def test_brain_uses_shared_led_contract(self):
        brain_main = (FW_ROOT / "brain_block" / "main" / "main.c").read_text(encoding="utf-8")
        self.assertIn('#include "led_contract.h"', brain_main)
        self.assertIn("led_contract_supports_brain_mirroring", brain_main)
        self.assertIn("led_contract_identity_color", brain_main)

    def test_matrix_templates_mirror_matrix_and_strip_commands(self):
        templates = [
            "if_block",
            "then_block",
            "end_if_block",
            "loop_block",
            "end_loop_block",
            "delay_block",
            "buttonpress_block",
            "note_block",
            "music_sequence_block",
        ]
        for template in templates:
            with self.subTest(template=template):
                main_c = (
                    FW_ROOT / "block_templates" / template / "main" / "main.c"
                ).read_text(encoding="utf-8")
                self.assertIn("status_strip_handle_matrix_command", main_c)
                self.assertIn("case CMD_MATRIX_FILL", main_c)
                self.assertIn("case CMD_MATRIX_CLEAR", main_c)
                self.assertIn("case CMD_MATRIX_BRIGHTNESS", main_c)
                self.assertIn("case CMD_MATRIX_SHOW", main_c)

    def test_note_stub_handler_removed(self):
        note_handler = FW_ROOT / "block_templates" / "note_block" / "main" / "command_handler.c"
        self.assertFalse(note_handler.exists(), "NOTE duplicate command_handler.c should be removed")

    def test_shared_capability_table_contains_all_supported_blocks(self):
        header = (FW_ROOT / "include" / "led_contract.h").read_text(encoding="utf-8")
        for block in [
            "BLOCK_TYPE_IF",
            "BLOCK_TYPE_THEN",
            "BLOCK_TYPE_END_IF",
            "BLOCK_TYPE_LOOP",
            "BLOCK_TYPE_END_LOOP",
            "BLOCK_TYPE_DELAY",
            "BLOCK_TYPE_BUTTON",
            "BLOCK_TYPE_NOTE",
            "BLOCK_TYPE_MUSIC_SEQ",
            "BLOCK_TYPE_LED_FLASH",
        ]:
            with self.subTest(block=block):
                self.assertIn(block, header)


if __name__ == "__main__":
    unittest.main()
