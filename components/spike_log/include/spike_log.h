#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Step-0 spike (docs/append-log-persistence-plan.md): prove that an append-only
 * write pattern on /sdcard does NOT corrupt and does NOT leak — isolating the SD
 * behaviour from SQLite. Spawns a writer (appends ~1.5 KB records at the real
 * measurement rate with periodic flush, NOT per-record fsync) and a reader
 * (reopens + re-reads + verifies a seq/checksum on every record, validating the
 * read-while-append model). Both log free heap.
 *
 * Build-flag gated (SPIKE_LOG). Call AFTER the SD card is mounted; it never
 * returns control to the normal measurement/persistence path. */
void spike_log_start(void);

#ifdef __cplusplus
}
#endif
