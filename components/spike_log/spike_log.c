/*
 * spike_log.c — Step-0 SD append-only spike (docs/append-log-persistence-plan.md).
 *
 * Goal: decide whether the events-DB corruption is SQLite's write pattern or the
 * card. This hammers /sdcard with the SAME load that broke SQLite (4 "channels"
 * of ~1.5 KB records every 5 s), but APPEND-ONLY with periodic flush — the
 * pattern that logged to a TXT file for months in the field. A reader reopens
 * and verifies every record (seq + checksum), validating read-while-append on
 * this FATFS/per-file-cache build. Both tasks log free heap.
 *
 * Pass (hours on the bad card): no "CORRUPTION"/"FAILED" lines, the reader keeps
 * up (off ≈ size), and free heap is flat. Then build the real event_log. If it
 * still corrupts, the fault is below SQLite (FATFS/SDMMC) — stop and rethink.
 */

#include "spike_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#define TAG "spike"

#define SPIKE_DIR        "/sdcard/spike"
#define SPIKE_FILE       SPIKE_DIR "/spike.log"
#define REC_HDR          24            /* "SPK %010lu %08lx " == exactly 24 bytes */
#define REC_FILL         1480          /* payload bytes (record ~1.5 KB) */
#define REC_TOTAL        (REC_HDR + REC_FILL + 1)   /* + '\n' */
#define RECS_PER_CYCLE   4             /* mimic 4 AMBIT channels */
#define WRITE_PERIOD_MS  5000          /* mimic the stress measurement loop */
#define FLUSH_PERIOD_MS  1000          /* periodic flush — NOT per-record fsync */
#define READ_PERIOD_MS   10000         /* mimic sync_runner drain cadence */

static FILE *s_wf = NULL;

static inline char fill_byte(unsigned long seq, int i) { return (char)('0' + ((seq + (unsigned long)i) % 10)); }

static unsigned long fill_sum(unsigned long seq)
{
    unsigned long s = 0;
    for (int i = 0; i < REC_FILL; i++) s += (unsigned char)fill_byte(seq, i);
    return s;
}

static bool write_record(unsigned long seq)
{
    static char buf[REC_TOTAL + 4];           /* single writer task → static is safe */
    snprintf(buf, sizeof buf, "SPK %010lu %08lx ", seq, fill_sum(seq));  /* exactly REC_HDR */
    for (int i = 0; i < REC_FILL; i++) buf[REC_HDR + i] = fill_byte(seq, i);
    buf[REC_HDR + REC_FILL] = '\n';
    return fwrite(buf, 1, REC_TOTAL, s_wf) == REC_TOTAL;
}

static void writer_task(void *arg)
{
    (void)arg;
    mkdir(SPIKE_DIR, 0777);
    remove(SPIKE_FILE);                        /* fresh spike each boot */
    s_wf = fopen(SPIKE_FILE, "a");
    if (s_wf == NULL) { ESP_LOGE(TAG, "writer: open failed"); vTaskDelete(NULL); return; }

    unsigned long seq = 0;
    TickType_t last_flush = xTaskGetTickCount();
    uint32_t cycle = 0;
    for (;;) {
        for (int r = 0; r < RECS_PER_CYCLE; r++) {
            if (!write_record(seq)) {
                ESP_LOGE(TAG, "writer: fwrite FAILED at seq %lu (SD write error!)", seq);
            }
            seq++;
        }
        if ((xTaskGetTickCount() - last_flush) >= pdMS_TO_TICKS(FLUSH_PERIOD_MS)) {
            fflush(s_wf);
            if (fsync(fileno(s_wf)) != 0) ESP_LOGE(TAG, "writer: fsync FAILED (SD error!)");
            last_flush = xTaskGetTickCount();
        }
        if ((cycle++ % 6) == 0) {              /* ~every 30 s */
            struct stat st; st.st_size = 0; stat(SPIKE_FILE, &st);
            ESP_LOGW(TAG, "WRITE seq=%lu size=%ldKB free=%lu",
                     seq, (long)(st.st_size / 1024), (unsigned long)esp_get_free_heap_size());
        }
        vTaskDelay(pdMS_TO_TICKS(WRITE_PERIOD_MS));
    }
}

static void reader_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(20000));          /* let the writer get well ahead */

    long          read_off   = 0;
    unsigned long expect_seq = 0;
    unsigned long bad        = 0;
    static char   line[REC_TOTAL + 8];         /* single reader task → static is safe */

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));

        FILE *rf = fopen(SPIKE_FILE, "r");      /* fresh handle → current on-media view */
        if (rf == NULL) { ESP_LOGW(TAG, "reader: open failed"); continue; }
        if (fseek(rf, read_off, SEEK_SET) != 0) { fclose(rf); continue; }

        unsigned long verified = 0;
        while (fgets(line, sizeof line, rf) != NULL) {
            size_t len = strlen(line);
            if (len == 0 || line[len - 1] != '\n') break;   /* partial tail — not flushed yet */

            unsigned long seq = 0, crc = 0;
            bool ok = (len == (size_t)REC_TOTAL) &&
                      (sscanf(line, "SPK %lu %lx", &seq, &crc) == 2);
            if (ok) {
                unsigned long sum = 0;
                for (int i = 0; i < REC_FILL; i++) sum += (unsigned char)line[REC_HDR + i];
                if (seq != expect_seq) { ESP_LOGE(TAG, "SEQ GAP exp %lu got %lu — CORRUPTION", expect_seq, seq); bad++; }
                if (sum != crc)        { ESP_LOGE(TAG, "CRC MISMATCH seq %lu got %08lx want %08lx — CORRUPTION", seq, sum, crc); bad++; }
                expect_seq = seq + 1;
            } else {
                ESP_LOGE(TAG, "BAD RECORD at off %ld (len %u) — CORRUPTION", read_off, (unsigned)len);
                bad++;
            }
            read_off += (long)len;
            verified++;
        }
        fclose(rf);
        ESP_LOGW(TAG, "READ +%lu upto_seq=%lu bad=%lu off=%ldKB free=%lu",
                 verified, expect_seq, bad, read_off / 1024, (unsigned long)esp_get_free_heap_size());
    }
}

void spike_log_start(void)
{
    ESP_LOGW(TAG, "=== SD APPEND-ONLY SPIKE (Step 0) — normal storage path DISABLED ===");
    xTaskCreate(writer_task, "spk_w", 6144, NULL, 5, NULL);
    xTaskCreate(reader_task, "spk_r", 6144, NULL, 4, NULL);
}
