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

    def test_brain_broadcasts_all_present_blocks_on_output_trigger(self):
        handler = (FW_ROOT / "brain_block" / "main" / "brain_event_handler.c").read_text(encoding="utf-8")
        self.assertNotIn("if (!is_output_block(entry->block_type))", handler)
        self.assertIn("BROADCAST action step=", handler)
        self.assertIn("targets=", handler)
        self.assertIn("i2c_set_led_color_id", handler)

    def test_brain_preserves_delay_if_loop_executor_logic(self):
        handler = (FW_ROOT / "brain_block" / "main" / "brain_event_handler.c").read_text(encoding="utf-8")
        self.assertIn("s_executor_ctx.wait_until_ms = now_ms() + delay_ms;", handler)
        self.assertIn("if (!s_executor_ctx.button_pressed)", handler)
        self.assertIn("case BLOCK_TYPE_LOOP", handler)
        self.assertIn("case BLOCK_TYPE_END_LOOP", handler)

    def test_each_canonical_block_template_handles_cmd_execute(self):
        # With all-block broadcast, every present child block must safely
        # handle CMD_EXECUTE (even if behavior is marker/visual-only).
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
            "led_color_flash_block",
        ]

        for template in templates:
            with self.subTest(template=template):
                main_dir = FW_ROOT / "block_templates" / template / "main"
                c_files = sorted(main_dir.glob("*.c"))
                self.assertTrue(c_files, f"No C sources found for template {template}")

                merged = "\n".join(path.read_text(encoding="utf-8") for path in c_files)
                self.assertIn(
                    "case CMD_EXECUTE",
                    merged,
                    f"Template {template} must handle CMD_EXECUTE for all-block broadcast",
                )

    def test_long_running_templates_expose_busy_ready_status_on_execute(self):
        # Long-running/actuation templates should advertise BUSY/READY state
        # while handling CMD_EXECUTE so Brain-side orchestration can reason
        # about execution progress.
        templates = [
            "delay_block",
            "note_block",
            "music_sequence_block",
            "led_color_flash_block",
        ]

        for template in templates:
            with self.subTest(template=template):
                main_dir = FW_ROOT / "block_templates" / template / "main"
                c_files = sorted(main_dir.glob("*.c"))
                self.assertTrue(c_files, f"No C sources found for template {template}")

                merged = "\n".join(path.read_text(encoding="utf-8") for path in c_files)
                self.assertIn("case CMD_EXECUTE", merged)
                self.assertIn("STATUS_BUSY", merged)
                self.assertIn("STATUS_READY", merged)


if __name__ == "__main__":
    unittest.main()
