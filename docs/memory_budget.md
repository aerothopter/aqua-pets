# Memory budget: ESP32-S3R8 (512 KB SRAM, 8 MB octal PSRAM, 16 MB flash)

Asserted at boot by `firmware/main/psram_plan.h` / `assert_plan()`. Measured numbers are in [stats.md](stats.md).

## Flash (16 MB): `firmware/partitions.csv`

| Partition | Offset | Size | Holds |
|---|---|---|---|
| nvs | 0x9000 | 24 KB | the save (`tank`/`save`), the settings, `bat`/`hist`, the batlog's `bed`. It never moves: `tools/make_installer.py` refuses a table that moves it |
| phy_init | 0xF000 | 4 KB | |
| ota_0 | 0x10000 | 2.5 MB | the app |
| **model** (raw, type 0x40) | 0x290000 | **8 MB** | `model_q4.bin` = **7.56 MB** (14.19M params, 4-bit, GS 64, fp16 scales); its last sector is the model trailer |
| otadata | 0xA90000 | 8 KB | which app slot boots |
| ota_1 | 0xAA0000 | 2.5 MB | the app, the other slot (over-the-air updates: [OTA.md](OTA.md)) |
| storage (spiffs) | 0xD20000 | ~2.9 MB | headroom |

The model is **memory-mapped** (`esp_partition_mmap`) and read through the flash cache during inference; it is never copied into
RAM. It is flashed by `tools/flash.sh --model`.

## PSRAM (8 MB)

| Item | Bytes | Note |
|---|---|---|
| framebuffers 2 × 448×368×2 | 659 KB | double-buffered RGB565 |
| KV cache 2 × 8 layers × 64 seq × 384 × fp32 | 1.57 MB | word tokens, so seq 64 is enough: a decision is at most 46 |
| run state + batched-prefill buffers | ~1 MB | |
| scene cache, vignette LUT, card cache, decor scratch | | the renderer's (`render_set_*`) |
| **headroom insisted on** | 1 MB | `PLAN_PSRAM_MIN_FREE_AFTER` |

## SRAM (512 KB)

FreeRTOS, task stacks, hot-loop scratch. The matmul inner loop streams flash weights and int8 activations, with no large SRAM
residents. **Watch `.dram0.bss`:** internal RAM has only ~23 KB of headroom, and the display driver needs 57 KB of DMA memory at
boot, so a new static table belongs in PSRAM (the decor scratch's way).

## Latency

A decision is ~44 prompt tokens + 3 generated. Single-token matmuls are bandwidth-bound: every token would be a full pass over 7.56 MB
of weights. **Batched prefill** does the prompt as one matrix-matrix pass, so weights are read once per layer rather than 44 times, and
the group dot product runs on the S3's PIE unit (`common/llm/q4_model.c`). On the 1.8 that comes to 10-13 tok/s, 3.7-4.4 s a decision.
