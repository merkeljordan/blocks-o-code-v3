# Blocks-o-Code Execution Engine: Technical Overview

This document describes the architecture and logic of the block-coding execution engine, encompassing program creation, I2C orchestration, and the asynchronous execution lifecycle.

---

## 1. System Architecture

The system operates on an **Orchestrated Master-Slave** model over I2C:

*   **Brain Block (Master/Orchestrator)**: Responsible for program control flow (loops, if-statements), managing block addresses, and triggering execution steps.
*   **Child Blocks (Slaves/Peripherals)**: Responsible for localized actions (e.g., Note playback, LED patterns). They report their status (Busy/Ready) back to the Brain via a register map.

---

## 2. Program Lifecycle

### Phase A: Program Creation (App to Brain)
1.  The **Companion App** (Flutter) scans the physical topology of the block chain.
2.  The App assigns a unique **I2C Runtime Address** to every block.
3.  The App transmits the **Program Configuration** to the Brain. This includes the step definitions, nesting rules (loops/ifs), and parameters like Note IDs or Song indices.

### Phase B: Block Configuration
Before execution, the Brain block communicates specific parameters to each child:
*   **CMD_SET_LED**: Sets the identity color or specific Note ID for the block.
*   **Payload Transfer**: For complex blocks (like Music Sequence), data is transferred via I2C before the program starts.

---

## 3. The Execution Lifecycle

When the program starts, the Brain's `brain_executor_task` begins a series of non-blocking ticks to step through the code.

### Step Execution (The "Handshake")
For every block in the program, the Brain follows this handshake:

1.  **Broadcast (`CMD_RUNTIME_BROADCAST`)**: The Brain tells all blocks which step is currently active. This drives the synchronized LED "running" pulses you see on the blocks.
2.  **Execute (`CMD_EXECUTE`)**: The Brain sends a command to the target Child block to start its action (play a note, start a song).
3.  **Busy Wait**: The Brain waits briefly for the child to set its `STATUS_BUSY` flag.
4.  **Idle Wait**: The Brain polls the child's status every 10ms. Once the child finishes its action (e.g., the audio finishes playing), it clears the `BUSY` flag, and the Brain moves to the next block.

---

## 4. Asynchronous Architecture

To keep the system responsive and prevent "freezing" during long songs:

*   **Child Side**: I2C commands are handled by a background queue. The block says "OK" to the Brain immediately, then plays the music in a separate background task.
*   **Brain Side**: The Brain uses a non-blocking state machine. It can still handle UI updates and battery checks while waiting for a child block to finish.

---

## 5. Visual Synchronization
*   **Running State**: The active block pulses its color to show progress.
*   **Terminal States (Done/Stop)**: When the program finishes, the Brain broadcasts a "Done" signal. Child blocks then **lock** their LED matrix to **Green** so the user knows the entire sequence was successful.

---

## 6. Reliability
*   **I2C Retries**: The Brain retries failed communications 3 times to handle any electrical noise on the bus.
*   **Real-time Logic**: The Brain immediately broadcasts status changes to ensure the LEDs match the audio perfectly.