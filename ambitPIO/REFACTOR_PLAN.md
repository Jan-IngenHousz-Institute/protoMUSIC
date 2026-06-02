# ambitPIO Firmware — Refactor / Cleanup Plan

Critical review of the project's own firmware (vendor libs excluded: `wrench`,
`Adafruit_AS7341`, ADI ADPD driver). Back-compatibility is **not** a constraint —
delete and restructure freely. Git holds history.

Reviewed: 2026-05-29. Scope: efficiency + maintainability.

---

## Top-level architecture observations

### 1. Full embedded scripting VM (Wrench, ~17.4k LOC) to parse ~8 commands
`do_c.cpp` spins up a Wrench `WRState`, registers `run`, `set_arr`, `config`,
`set_currents`, etc., then compiles + runs a script for every `C...?` command.
`src/src/wrench.cpp` is **17,420 lines** — the largest flash-size and maintenance
liability, duplicating what `do_command`'s `hash()` dispatch already does.
**Strategic call:** unless on-device uploadable scripting/control-flow is genuinely
needed, replace `do_c` with the same token dispatch used elsewhere and remove ~17k
lines. Confirm the command surface is fully replaceable first.

### 2. Two command dialects with THREE parallel config copies
- Computer path: `do_command.h` / `do_c.cpp` mutate the **global**
  `adpd_current_config` / `adpd_gains_config`, and `do_c.cpp` keeps its own
  `static pulsed_620_current` etc. (3rd copy).
- Ambyte path: `run_esp.cpp` mutates file-static `adpd_current_config_local` /
  `adpd_gains_config_local`.
Same logical setting in 3 places → divergence bug. Unify into one config struct
all paths read/write.

### 3. Massive dead code
- `PAM.cpp:1434-1905` — ~470 commented-out lines (`run_arr`, `data_allocation`,
  `steady_state`, `detector_preset_1`).
- `data_utils.cpp:524-673` — ~150 commented lines (`send_esp`, `send_data`, `write32`).
Delete.

### 4. Measurement loop copy-pasted 4–5 times
`run_arr_type1` (PAM.cpp:153), `run_trigger_spacer` (402), `external_trigger_run`
(580), `external_trigger_run_Flash` (727), `MPF` (910), `fluor_offset` (1319) all
repeat: STOP → GPIO/sync setup → allocate 7 `dataclass` → FIFO drain loop
`while (fifo_c >= expected_readout_bytes) { readfifo… }` → ambient correction
`ret[i] > 65000 ? ret[i]-65000 : 0` → CONNECTION_TYPE send block → delete block.
Extract: (a) `PamBuffers` RAII struct (7 channels), (b) `read_one_frame(ret)`,
(c) `send_results(connection_type)`.

---

## Correctness landmines

### 5. Buffer overflow in `arr_reset` (do_c.cpp:21-30)
`arr_num = argv[0].asInt() - 1` (uint8_t); passing `0` underflows to 255. Guard
`if ((arr_num == 0) || (arr_num > 7)) return;` wrongly rejects array #1, and loop
`for (i=0; i<WR_MAX_ARR=640; i++) wr_run_arr[i + arr_num*64]` writes past the
640-byte array. Loop bound should be `64`; guard logic is inverted.

### 6. `get_FW_info` checksum uses `&` not `|` (nvs1.cpp:16)
`(MAJOR<<4) & (MINOR<<2) & (BATCH<<0)` → always 0 for 0,0,3. Meant bitwise-OR.

### 7. `PAM_retrieve_env` decode no longer matches encode
Phase-A made `PAM_get_env` (PAM.cpp:1169) emit plain `(uint16_t)centi`, but
`PAM_retrieve_env` (1178-1192) still bit-unpacks the OLD format
(`r & 0xFFF`, `>>12`, `/20.0 - 20`). Stale/wrong. Delete if no FW caller; else fix.

### 8. Truncating casts in command parsing
`do_command.h:319` `uint8_t length = (uint16_t) Serial_Input_Long(...)` clips to
255 though `run_trigger_spacer` takes uint16_t up to 3000. Same pointless
`(uint16_t)→uint8_t` in `a`/`aa` cases (329, 341).

### 9. Non-void FSM functions missing returns
`fsm_send_waitesp` (data_utils.cpp:463-478) declared `int`, returns nothing on all
paths (UB). `fsm_wake_up_calls(void)` dead vs the `(bool)` overload.
`_num_checksum_err`/`num_retry` incremented but never reset (one-shot guards).

### 10. Uninitialized locals (C declaration quirk)
`PAM.cpp:201` `uint32_t counter, ploter1, ploter2 = 0;` — only `ploter2` init'd.
Same at line 195 (`farred, actinic, subsampling = 0`). Latent traps.

### 11. Memory leaks on early return
`run_arr_type1` (PAM.cpp:180-186) does 7× `new dataclass` then `return -1` on alloc
failure without deleting. `run_trigger_spacer` uses `goto del_classes` (inconsistent).
**Fix:** make `dataclass` stack-allocated (it manages its own heap buffer in
`init`/`clean`), drop `new`/`delete`, let destructor clean up on every path.

### 12. Strict-aliasing / unaligned float reads
`run_esp.cpp:234,242,294,317` `*((float *) &(cmd_arr[3]))` — unaligned 4-byte read
from uint8_t offset; UB and slow/trapping on RISC-V (C3). Use `memcpy`.

---

## Efficiency / structure

### 13. Protocol magic numbers scattered
`data_utils.h` defines `WAKE_AMBYTE 211` etc., but raw bytes appear as literals:
main loop matches `170,160,222` (ambit-1.ino:163), `do_esp_cmd` hardcodes
`160/161/240/170`, `133`=`AMBIT_BOOT_IDLE`, UART wake cause `8`
(`ESP_SLEEP_WAKEUP_UART`) hardcoded ~4 places. Centralize into one
`namespace proto { enum {...} }` header.

### 14. Pin numbers half-symbolic
`STF_FLASH_PIN`/`BOOT_PIN` #defined, but `digitalWrite(10,…)` (ADPD trigger),
`digitalWrite(1,…)` (== STF_FLASH_PIN, written both ways), raw `GPIO_NUM_9/20/1/10`
used directly. Name pin 10; use symbols consistently.

### 15. `serial_read_until` re-declared with default args in 4 TUs
data_utils.cpp:10, PAM.cpp:12, run_esp.cpp:26, ambit-1.ino:60. Fragile. One
prototype in `serial.h`.

### 16. `serial_read_until` / `flush_serial` echo non-target bytes back
They `Serial.write(b)` / `Serial.print(b)` every non-match onto the same line. On
the ambyte binary link this injects bytes into the protocol stream — leftover
terminal-echo debug that doesn't belong in the parser.

### 17. `load_calibration_info` does `isKey()` + `getX()` for ~20 keys
nvs1.cpp:61-83 — two NVS lookups each; `getX(key, default)` already handles absence.

### 18. `dataclass` ring-buffer over-engineered
Buffers always `init()`'d to exact `data_count`, so wrap path (`loop_ahead`,
`_overwritten_counter`, suspicious `length -= write_ptr - read_ptr + 1` underflow
at data_utils.cpp:142) likely never runs yet complicates every read. Naming
inverted: public `length`=count, private `_length`=capacity. Simplify to fixed
capacity + count.

### 19. `hash()` command dispatch silent collision risk
do_command.h:22-25 — no uniqueness check; two commands can collide to one `case`
with no compiler warning. Prefer `strcmp` table / `string_view` switch.

### 20. Protocol mode-switch is glitch-sensitive (`c > 127` heuristic)
ambit-1.ino:159-181 — the main loop decides "computer text" vs "ambyte binary"
purely on whether the first peeked byte is `> 127`. A single stray high byte (line
noise, or a UART framing glitch from the DTR/RTS reset when a host opens the port)
forces a 50 ms binary-header hunt (`serial_read_until(170,160,222,…)`) and emits
`[E] Unknown cmd <n>` (e.g. the observed `Unknown cmd 251` / 0xFB). It self-recovers
(drops the byte, replies 128), so it's benign as a one-off but noisy and fragile.
Also `serial_read_until`/`flush_serial` echo every received byte back (decimal for
>127, raw otherwise; data_utils.cpp:28-33), which pollutes the link.
**Fix direction:** a properly framed protocol with a sync preamble + length +
checksum that rejects junk bytes cleanly, instead of mode-switching on a single
byte's MSB. Drop the echo-on-parse behavior. Until then, host side should
`setDTR(False)/setRTS(False)` and `reset_input_buffer()` on open (see the demo
notebook fix) so the open glitch never reaches the parser.

---

## Suggested order of attack

1. **Delete dead code** (#3) — zero risk, unblocks readability.
2. **Fix concrete bugs** (#5, #6, #7, #8, #9, #12) — small, isolated.
3. **Centralize protocol constants + pins** (#13, #14) + single `serial_read_until`
   prototype (#15). Address the glitch-sensitive mode-switch + parser echo (#20)
   here too — they share the protocol-framing rework.
4. **Unify config structs** (#2); make `dataclass` stack/RAII (#11).
5. **Extract shared PAM measurement scaffolding** (#4).
6. **Decide on Wrench** (#1) — the big strategic call.

### Pre-deletion checks
- Confirm no callers of `PAM_retrieve_env`, `fsm_send_waitesp`,
  `fsm_wake_up_calls(void)` before removing.
- Confirm the Wrench command surface (`run`, `set_arr`, `config`, `set_currents`,
  `set_gains`, `run_mpf`, `reset`, `disp`, `print`, `get_par`, `get_temp`) is fully
  replaceable before removing the VM.
