#include <stdio.h>
#include <windows.h>
#include "brain_event_handler.h"
#include "block_config_manager.h"

extern void mock_set_config(const block_config_state_t* cfg);
extern int g_mock_i2c_execute_counts[256];
extern void mock_i2c_reset_spies(void);
extern void sim_log_init(const char *path);
extern void sim_log_close(void);
extern void sim_log_write(const char *fmt, ...);

// Error injection globals from mock_i2c.c
extern uint8_t g_mock_i2c_fail_address;
extern int     g_mock_i2c_busy_delay_ms;

// Route plain printf through the tee logger too
#define printf(...) sim_log_write(__VA_ARGS__)

void setup_test_loop_sequence() {
    block_config_state_t cfg = {0};
    
    cfg.blocks[0].i2c_address = 0x0B;
    cfg.blocks[0].block_type = BLOCK_TYPE_LOOP;
    cfg.blocks[0].present = true;
    
    cfg.blocks[1].i2c_address = 0x0C;
    cfg.blocks[1].block_type = BLOCK_TYPE_NOTE;
    cfg.blocks[1].present = true;
    
    cfg.blocks[2].i2c_address = 0x14; 
    cfg.blocks[2].block_type = BLOCK_TYPE_END_LOOP;
    cfg.blocks[2].present = true;

    cfg.block_count = 3;
    cfg.error_count = 0;
    cfg.has_changed = false;

    mock_set_config(&cfg);
    brain_event_handler_set_config_validation(true, 0, 0); 
}

void setup_test_ifthen_sequence() {
    block_config_state_t cfg = {0};
    
    // IF -> BUTTON -> THEN -> NOTE -> END_IF
    cfg.blocks[0].i2c_address = 0x08;
    cfg.blocks[0].block_type = BLOCK_TYPE_IF;
    cfg.blocks[0].present = true;
    
    cfg.blocks[1].i2c_address = 0x15; 
    cfg.blocks[1].block_type = BLOCK_TYPE_BUTTON;
    cfg.blocks[1].present = true;
    
    cfg.blocks[2].i2c_address = 0x09;
    cfg.blocks[2].block_type = BLOCK_TYPE_THEN;
    cfg.blocks[2].present = true;
    
    cfg.blocks[3].i2c_address = 0x0C;
    cfg.blocks[3].block_type = BLOCK_TYPE_NOTE;
    cfg.blocks[3].present = true;

    cfg.blocks[4].i2c_address = 0x0A;
    cfg.blocks[4].block_type = BLOCK_TYPE_END_IF;
    cfg.blocks[4].present = true;

    cfg.block_count = 5;
    cfg.error_count = 0;
    cfg.has_changed = false;

    mock_set_config(&cfg);
    brain_event_handler_set_config_validation(true, 0, 0); 
}

void setup_test_delay_sequence() {
    block_config_state_t cfg = {0};
    
    // DELAY -> NOTE
    cfg.blocks[0].i2c_address = 0x14;
    cfg.blocks[0].block_type = BLOCK_TYPE_DELAY;
    cfg.blocks[0].present = true;
    
    cfg.blocks[1].i2c_address = 0x0C;
    cfg.blocks[1].block_type = BLOCK_TYPE_NOTE;
    cfg.blocks[1].present = true;

    cfg.block_count = 2;
    cfg.error_count = 0;
    cfg.has_changed = false;

    mock_set_config(&cfg);
    brain_event_handler_set_config_validation(true, 0, 0); 
}

void setup_test_sequence_4() {
    block_config_state_t cfg = {0};
    cfg.blocks[0] = (block_config_entry_t){.i2c_address = 0x0B, .block_type = BLOCK_TYPE_LOOP, .present = true};
    cfg.blocks[1] = (block_config_entry_t){.i2c_address = 0x0F, .block_type = BLOCK_TYPE_MUSIC_SEQ, .present = true};
    cfg.blocks[2] = (block_config_entry_t){.i2c_address = 0x10, .block_type = BLOCK_TYPE_MUSIC_SEQ, .present = true};
    cfg.blocks[3] = (block_config_entry_t){.i2c_address = 0x08, .block_type = BLOCK_TYPE_IF, .present = true};
    cfg.blocks[4] = (block_config_entry_t){.i2c_address = 0x15, .block_type = BLOCK_TYPE_BUTTON, .present = true};
    cfg.blocks[5] = (block_config_entry_t){.i2c_address = 0x09, .block_type = BLOCK_TYPE_THEN, .present = true};
    cfg.blocks[6] = (block_config_entry_t){.i2c_address = 0x0C, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[7] = (block_config_entry_t){.i2c_address = 0x0D, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[8] = (block_config_entry_t){.i2c_address = 0x0E, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[9] = (block_config_entry_t){.i2c_address = 0x11, .block_type = BLOCK_TYPE_LED_FLASH, .present = true};
    cfg.blocks[10] = (block_config_entry_t){.i2c_address = 0x12, .block_type = BLOCK_TYPE_LED_FLASH, .present = true};
    cfg.blocks[11] = (block_config_entry_t){.i2c_address = 0x14, .block_type = BLOCK_TYPE_DELAY, .present = true};
    cfg.blocks[12] = (block_config_entry_t){.i2c_address = 0x0A, .block_type = BLOCK_TYPE_END_IF, .present = true};
    cfg.blocks[13] = (block_config_entry_t){.i2c_address = 0x13, .block_type = BLOCK_TYPE_END_LOOP, .present = true};

    cfg.block_count = 14;
    cfg.error_count = 0;
    cfg.has_changed = false;
    mock_set_config(&cfg);
    brain_event_handler_set_config_validation(true, 0, 0);
}

void setup_test_sequence_5() {
    block_config_state_t cfg = {0};
    cfg.blocks[0] = (block_config_entry_t){.i2c_address = 0x14, .block_type = BLOCK_TYPE_DELAY, .present = true};
    cfg.blocks[1] = (block_config_entry_t){.i2c_address = 0x0F, .block_type = BLOCK_TYPE_MUSIC_SEQ, .present = true};
    cfg.blocks[2] = (block_config_entry_t){.i2c_address = 0x10, .block_type = BLOCK_TYPE_MUSIC_SEQ, .present = true};
    cfg.blocks[3] = (block_config_entry_t){.i2c_address = 0x08, .block_type = BLOCK_TYPE_IF, .present = true};
    cfg.blocks[4] = (block_config_entry_t){.i2c_address = 0x15, .block_type = BLOCK_TYPE_BUTTON, .present = true};
    cfg.blocks[5] = (block_config_entry_t){.i2c_address = 0x09, .block_type = BLOCK_TYPE_THEN, .present = true};
    cfg.blocks[6] = (block_config_entry_t){.i2c_address = 0x0B, .block_type = BLOCK_TYPE_LOOP, .present = true};
    cfg.blocks[7] = (block_config_entry_t){.i2c_address = 0x0C, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[8] = (block_config_entry_t){.i2c_address = 0x0D, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[9] = (block_config_entry_t){.i2c_address = 0x0E, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[10] = (block_config_entry_t){.i2c_address = 0x11, .block_type = BLOCK_TYPE_LED_FLASH, .present = true};
    cfg.blocks[11] = (block_config_entry_t){.i2c_address = 0x12, .block_type = BLOCK_TYPE_LED_FLASH, .present = true};
    cfg.blocks[12] = (block_config_entry_t){.i2c_address = 0x13, .block_type = BLOCK_TYPE_END_LOOP, .present = true};
    cfg.blocks[13] = (block_config_entry_t){.i2c_address = 0x0A, .block_type = BLOCK_TYPE_END_IF, .present = true};

    cfg.block_count = 14;
    cfg.error_count = 0;
    cfg.has_changed = false;
    mock_set_config(&cfg);
    brain_event_handler_set_config_validation(true, 0, 0);
}

void setup_test_sequence_6() {
    block_config_state_t cfg = {0};
    cfg.blocks[0] = (block_config_entry_t){.i2c_address = 0x0B, .block_type = BLOCK_TYPE_LOOP, .present = true};
    cfg.blocks[1] = (block_config_entry_t){.i2c_address = 0x0C, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[2] = (block_config_entry_t){.i2c_address = 0x11, .block_type = BLOCK_TYPE_LED_FLASH, .present = true};
    cfg.blocks[3] = (block_config_entry_t){.i2c_address = 0x13, .block_type = BLOCK_TYPE_END_LOOP, .present = true};
    cfg.blocks[4] = (block_config_entry_t){.i2c_address = 0x14, .block_type = BLOCK_TYPE_DELAY, .present = true};
    cfg.blocks[5] = (block_config_entry_t){.i2c_address = 0x0F, .block_type = BLOCK_TYPE_MUSIC_SEQ, .present = true};
    cfg.blocks[6] = (block_config_entry_t){.i2c_address = 0x10, .block_type = BLOCK_TYPE_MUSIC_SEQ, .present = true};
    cfg.blocks[7] = (block_config_entry_t){.i2c_address = 0x08, .block_type = BLOCK_TYPE_IF, .present = true};
    cfg.blocks[8] = (block_config_entry_t){.i2c_address = 0x15, .block_type = BLOCK_TYPE_BUTTON, .present = true};
    cfg.blocks[9] = (block_config_entry_t){.i2c_address = 0x09, .block_type = BLOCK_TYPE_THEN, .present = true};
    cfg.blocks[10] = (block_config_entry_t){.i2c_address = 0x0D, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[11] = (block_config_entry_t){.i2c_address = 0x0E, .block_type = BLOCK_TYPE_NOTE, .present = true};
    cfg.blocks[12] = (block_config_entry_t){.i2c_address = 0x12, .block_type = BLOCK_TYPE_LED_FLASH, .present = true};
    cfg.blocks[13] = (block_config_entry_t){.i2c_address = 0x0A, .block_type = BLOCK_TYPE_END_IF, .present = true};

    cfg.block_count = 14;
    cfg.error_count = 0;
    cfg.has_changed = false;
    mock_set_config(&cfg);
    brain_event_handler_set_config_validation(true, 0, 0);
}

void setup_test_nesting_error() {
    block_config_state_t cfg = {0};
    // IF -> END_LOOP (Invalid!)
    cfg.blocks[0] = (block_config_entry_t){.i2c_address = 0x08, .block_type = BLOCK_TYPE_IF, .present = true};
    cfg.blocks[1] = (block_config_entry_t){.i2c_address = 0x13, .block_type = BLOCK_TYPE_END_LOOP, .present = true};
    cfg.block_count = 2;
    mock_set_config(&cfg);
    brain_event_handler_set_config_validation(true, 0, 0);
}

void run_test_executor(const char* test_name, bool auto_press_button, int expected_final_pc, int expected_final_state) {
    mock_i2c_reset_spies();
    
    if (brain_event_handle_message("START")) {
        printf("START accepted. Running ticks...\n");
    } else {
        printf("START failed!\n");
        // If it failed immediately (e.g. validation), check if we expected that
        if (expected_final_state == EXECUTOR_IDLE) {
             printf("✅ RESULT: PASS (Immediate validation rejection)\n\n");
        } else {
             printf("❌ RESULT: FAIL (Immediate failure not expected)\n\n");
        }
        return;
    }
    
    Sleep(50); // allow background queue to process START

    const brain_executor_context_t* ctx = brain_executor_get_context();
    
    int safeguard = 0;
    int last_pc = -1;
    brain_executor_state_t last_state = EXECUTOR_IDLE;
    
    while (ctx->state != EXECUTOR_DONE && ctx->state != EXECUTOR_STOPPED && safeguard < 600) {
        if (ctx->pc != last_pc || ctx->state != last_state) {
            printf("[Tick] PC: %d, State: %d\n", ctx->pc, ctx->state);
            last_pc = ctx->pc;
            last_state = ctx->state;
        }
        
        if (ctx->state == EXECUTOR_WAIT_INPUT && auto_press_button) {
            static int press_cooldown = 0;
            if (press_cooldown <= 0) {
                printf("[Sim] Simulating physical button press...\n");
                uint8_t payload[1] = {1};
                brain_event_handle_block_event(0x15, BRAIN_BLOCK_EVENT_BUTTON_PRESS, payload, 1);
                press_cooldown = 10; // Don't spam, wait 10 ticks
            } else {
                press_cooldown--;
            }
        }
        
        brain_executor_tick();
        Sleep(5); // Speed up simulation slightly
        safeguard++;
    }
    
    // Upping safeguard for tests with real human-like delay
    int max_ticks = (ctx->state == EXECUTOR_WAIT_INPUT) ? 2000 : 800; 
    
    printf("%s Finished. PC: %d, State: %d (Ticks run: %d)\n", test_name, ctx->pc, ctx->state, safeguard);
    
    bool is_pass = (ctx->state == expected_final_state) && (expected_final_pc == -1 || ctx->pc == expected_final_pc);
    if (is_pass) {
        printf("✅ RESULT: PASS\n");
    } else {
        printf("❌ RESULT: FAIL (Expected PC=%d State=%d, Final PC=%d State=%d)\n", 
               expected_final_pc, expected_final_state, ctx->pc, ctx->state);
    }

    printf("--- Mock I2C Execution Spy Report ---\n");
    for (int i = 0; i < 256; i++) {
        if (g_mock_i2c_execute_counts[i] > 0) {
            printf("  Address 0x%02X received CMD_EXECUTE count: %d\n", i, g_mock_i2c_execute_counts[i]);
        }
    }
    printf("\n");
}

int main() {
    sim_log_init("sim_log.txt");
    printf("Starting PC Simulator for Brain Executor...\n");
    brain_event_handler_init();

    printf("\n=== TEST 1: LOOP SEQUENCE ===\n");
    setup_test_loop_sequence();
    run_test_executor("Test 1", false, 3, EXECUTOR_DONE);
    
    printf("\n=== TEST 2: IF/THEN SEQUENCE ===\n");
    setup_test_ifthen_sequence();
    run_test_executor("Test 2", true, 5, EXECUTOR_DONE);
    
    printf("\n=== TEST 3: DELAY SEQUENCE ===\n");
    setup_test_delay_sequence();
    run_test_executor("Test 3", false, 2, EXECUTOR_DONE);
    
    printf("\n=== TEST 4: EXTENDED SEQUENCE 1 ===\n");
    setup_test_sequence_4();
    run_test_executor("Test 4", true, 14, EXECUTOR_DONE);

    printf("\n=== TEST 5: EXTENDED SEQUENCE 2 ===\n");
    setup_test_sequence_5();
    run_test_executor("Test 5", true, 14, EXECUTOR_DONE);

    printf("\n=== TEST 6: EXTENDED SEQUENCE 3 ===\n");
    setup_test_sequence_6();
    run_test_executor("Test 6", true, 14, EXECUTOR_DONE);

    printf("\n=== TEST 7: I2C FAILURE (DISCONNECT) ===\n");
    setup_test_delay_sequence(); // Simple Delay -> Note
    g_mock_i2c_fail_address = 0x0C; // Fail on the Note block
    // Expect it to finish the program (PC=2) even if 0x0C failed
    run_test_executor("Test 7", false, 2, EXECUTOR_DONE); 

    printf("\n=== TEST 8: BUSY TIMEOUT ===\n");
    setup_test_sequence_4(); // Loop -> Music -> Music...
    g_mock_i2c_busy_delay_ms = 500; // Longer than 200ms timeout
    run_test_executor("Test 8", true, 14, EXECUTOR_DONE);

    printf("\n=== TEST 9: NESTING ERROR VALIDATION ===\n");
    setup_test_nesting_error(); // IF -> END_LOOP
    // Since we removed pre-flight validation, it starts, hits an error at PC 0, and stops.
    run_test_executor("Test 9", false, 0, EXECUTOR_DONE); 
    
    sim_log_close();
    return 0;
}
