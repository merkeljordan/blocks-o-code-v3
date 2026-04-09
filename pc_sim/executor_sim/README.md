# Brain Firmware Executor Simulator

This directory contains a **native PC-side mock framework** that compiles the exact ESP-IDF `brain_event_handler.c` logic directly onto your computer without requiring physical hardware.

This allows for deterministic stress testing of control-flow paradigms (like `LOOP`, `IF/THEN`), wait mechanics (`DELAY`), and callback injections (`BUTTON` presses) instantly.

## How it Works

Instead of real I2C transactions to physical child block ESP32s over wires, the simulator injects a fake block layout using `mock_set_config()`. 

The `main.c` testing hook repeatedly pumps `brain_executor_tick()` using a custom 10ms frame loop, and monitors the executor's internal Program Counter (PC) and State representations.

Stubbed hardware drivers redirect the Brain's generic commands into console log printouts:
*   `esp_err` / `esp_log` / FreeRTOS APIs
*   `i2c_execute()` -> **Mocked!** Always returns immediately, printing payload data.
*   Wait timeouts and block statuses (`STATUS_BUSY`) -> **Mocked!** Skips infinite polling and transitions states logically.

## Compiling & Running

Open a PowerShell terminal and run the following standalone GCC build command. It links our mocked standard library alongside the live `brain_event_handler.c`:

```powershell
cd c:\Users\merke\blocks-o-code-v3\pc_sim\executor_sim

gcc -g -O0 -Iinclude -I. -I..\..\firmware_blocks\brain_block\main -I..\..\firmware_blocks\include -I..\..\firmware_blocks\shared_components\status_strip -I..\..\firmware_blocks\shared_components\audio main.c mock_esp_framework.c mock_i2c.c mock_components.c ..\..\firmware_blocks\brain_block\main\brain_event_handler.c -o executor_sim.exe; .\executor_sim.exe
```

## Writing Your Own Tests

To construct a new sequence and test how the core Brain firmware logic handles it, simply modify the configurations inside `main.c`:

1.  **Create a Config Array:** Assign the I2C block addresses and `block_type` definitions mirroring standard hardware layout chains.
    ```c
    block_config_state_t cfg = {0};
    
    // Simulate: DELAY -> NOTE
    cfg.blocks[0].i2c_address = 0x14;
    cfg.blocks[0].block_type = BLOCK_TYPE_DELAY;
    cfg.blocks[0].present = true;
    
    cfg.blocks[1].i2c_address = 0x0C;
    cfg.blocks[1].block_type = BLOCK_TYPE_NOTE;
    cfg.blocks[1].present = true;

    cfg.block_count = 2; // VERY IMPORTANT: Set total count!
    ```

2.  **Bind the Configuration:** Pass the struct to the mock system.
    ```c
    mock_set_config(&cfg);
    brain_event_handler_set_config_validation(true, 0, 0); 
    ```

3.  **Run the Test & Inject Async Hardware Events:** If your simulation requires testing callbacks—such as a user physically pushing a Button block during the executor's `EXECUTOR_WAIT_INPUT` cycle—you can dispatch the `BRAIN_BLOCK_EVENT_BUTTON_PRESS` payload directly into the brain's internal event router!
    ```c
    // If the executor halted and is waiting for a hardware submission
    if (ctx->state == EXECUTOR_WAIT_INPUT) {
        printf("[Sim] Simulating physical button press...\n");
        uint8_t payload[1] = {1};
        brain_event_handle_block_event(0x15, BRAIN_BLOCK_EVENT_BUTTON_PRESS, payload, 1);
    }
    ```

## Interpreting the Output Log

The output streams directly to standard out (`stdout`), where you can verify exact clock latency and PC jump mechanics.

Example Output Trace:
```text
=== TEST 3: DELAY SEQUENCE ===
START accepted. Running ticks...
[Tick] PC: 0, State: 1
D (brain_evt): EXEC pc=0 type=21 (DELAY)
I (brain_evt): DELAY pc=0 wait_ms=500
[Tick] PC: 0, State: 3 (WAIT_DELAY)     <-- Simulator confirms correct halt state
... Waits 500ms ...
[Tick] PC: 1, State: 1
D (brain_evt): EXEC pc=1 type=48 (NOTE)
I (MOCK_I2C): i2c_execute(addr=0x0C)    <-- Dispatch over mock I2C
Test 3 Finished. PC: 2, State: 5 (Ticks run: 31)
```
