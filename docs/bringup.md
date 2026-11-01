# Bringing up a new board (Waveshare ESP32-S3-Touch-AMOLED-1.8)

A blank board has nothing to preserve, so its first flash goes around `tools/flash.sh`, whose preflight asks a running tank for its
log. Once the tank runs on it, the [DEVICE.md](DEVICE.md) rules apply. Work top to bottom; each step has a pass signal.

1. **Identify the revision** from the back label. V1 = SH8601 panel + FT3168 touch; V2 = CO5300 + CST816. The firmware auto-detects
   (it probes CST816 at 0x15) and applies the V2 x-gap; both use the same init sequence.
2. **Flash Waveshare's stock demo** first (their wiki), which proves the display and touch.
3. **Toolchain:** ESP-IDF v5.4 (`tools/esp_env.sh` says how on each system). The first build needs a signing key:
   `tools/ota_key.sh`. Then flash the app and the model:
   ```
   cd firmware
   idf.py -p <port> flash
   python -m esptool --chip esp32s3 -p <port> write_flash 0x290000 ../model/out/model_q4.bin
   idf.py -p <port> monitor
   ```
   Pass: the log shows `PSRAM plan OK`, `model partition 8192 KB mmap'd, advisor LLM`, `sh8601: panel up`, `touch: FT3168 ready`, and
   the creatures are on the glass.
4. **Pins** (`firmware/main/board_pins.h`): I2C SDA/SCL are 15/14 in Waveshare's code and 14/15 in the Arduino variant. If the expander,
   touch or RTC fail to probe, swap them. QSPI CS12 CLK11 D0-3 = 4/5/6/7 agree across sources.
5. **Power rails:** the IO expander (TCA9554 at 0x20) drives LCD_RST (bit 0), display power (bit 1) and touch reset (bit 2), and the
   firmware sequences them. If the panel stays dark, check whether the AXP2101 PMIC needs a rail enabled (Waveshare's example
   `90_axp2101_pmu`).
6. **Orientation:** the tank is rendered landscape 448×368 and rotated 90° in software in `display_port_flush`. If the image is mirrored
   or upside down, flip the mapping there (two lines), not the renderer.
7. **Touch mapping:** the same rotation in `touch_port_ft3168.c`. Tap a corner and watch the log. `tools/touch_calib.py` runs the
   nine-crosshair calibration and fits the map.
8. **RTC:** the first boot seeds the PCF85063 from build time, and later boots read it. Pass: power off overnight and power on, and the
   tank has lived the night (taller grass, film on the glass, hungrier creatures).
9. **Measure:** decision latency and tok/s from the `advisor:` log lines, render fps from `display:`, the heap from the 10 s status line.
   Compare with [stats.md](stats.md) and [memory_budget.md](memory_budget.md).
