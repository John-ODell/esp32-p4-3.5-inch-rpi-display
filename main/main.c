/* 3.5" RPi display (MHS-3.5inch Display-IPS) on the ESP32-P4: speed test, touch calibrator, touch tester.
 *
 *  1. Red, green, blue full-screen fills, timed (shown on screen and on serial).
 *  2. Colour bars for 2 s. Hold the screen now to force a new calibration.
 *  3. 5-point touch calibration if none is saved (or forced). Saved in NVS, survives reflashing
 *     the app (not a full erase). The coefficients are printed on serial for use in other code.
 *  4. Touch tester: live x/y, raw readings and pressure; drag to draw; tap near one of the five
 *     grey reference crosses to see how far off the touch lands. Bottom buttons: CLEAR,
 *     RECALIBRATE, COLOURS. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "rpi_lcd.h"
#include "rpi_text.h"
#include "xpt2046.h"
#include "touch_cal.h"
#include "pins.h"

static const char *TAG = "demo";

#define BAR_H   32                          /* bottom button bar */
#define STAT_H  18                          /* top status line */
#define C_BG    RGB565(0, 0, 0)
#define C_GRID  RGB565(40, 40, 40)
#define C_REF   RGB565(120, 120, 120)
#define C_INK   RGB565(255, 220, 0)
#define C_TXT   RGB565(220, 220, 220)
#define C_DIM   RGB565(130, 130, 130)
#define C_BTN   RGB565(40, 48, 70)
#define C_GOOD  RGB565(60, 230, 90)
#define C_WARN  RGB565(255, 160, 40)
#define C_BAD   RGB565(255, 70, 50)

static touch_cal_t s_cal;
static const int REF_X[5] = {30, LCD_W - 31, LCD_W - 31, 30, LCD_W / 2};
static const int REF_Y[5] = {30, 30, LCD_H - 31, LCD_H - 31, LCD_H / 2};

static void centre_text(int y, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    rpi_lcd_text(LCD_W / 2 - (int)strlen(s) * 4 * scale, y, s, fg, bg, scale);
}

static void colour_bars(void)
{
    static uint16_t line[LCD_W];
    const uint16_t bars[8] = {RGB565(255,255,255), RGB565(255,255,0), RGB565(0,255,255), RGB565(0,255,0),
                              RGB565(255,0,255), RGB565(255,0,0), RGB565(0,0,255), RGB565(0,0,0)};
    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            uint16_t c;
            if (y < 200)      c = bars[x * 8 / LCD_W];
            else if (y < 260) c = RGB565(x * 255 / LCD_W, x * 255 / LCD_W, x * 255 / LCD_W);
            else              c = RGB565(x * 255 / LCD_W, 0, 255 - x * 255 / LCD_W);
            if (x == 0 || y == 0 || x == LCD_W - 1 || y == LCD_H - 1) c = RGB565(255, 255, 255);
            line[x] = c;
        }
        rpi_lcd_blit(0, y, LCD_W, 1, line);
    }
}

static int speed_test(void)
{
    const uint16_t c[3] = {RGB565(255, 0, 0), RGB565(0, 255, 0), RGB565(0, 0, 255)};
    const char *n[3] = {"red", "green", "blue"};
    int64_t total = 0;
    for (int i = 0; i < 3; i++) {
        int64_t t0 = esp_timer_get_time();
        rpi_lcd_fill_rect(0, 0, LCD_W, LCD_H, c[i]);
        int64_t dt = esp_timer_get_time() - t0;
        total += dt;
        ESP_LOGI(TAG, "full-screen %s fill: %lld us (%.1f fps)", n[i], dt, 1e6 / dt);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    return (int)(total / 3);
}

static void calibrate(bool forced)
{
    float err = 0;
    if (!forced && touch_cal_load(&s_cal, &err) == ESP_OK) {
        ESP_LOGI(TAG, "loaded saved calibration (worst point %.1f px)", err);
    } else {
        ESP_LOGI(TAG, "%s", forced ? "calibration requested" : "no saved calibration, calibrating");
        ESP_ERROR_CHECK(touch_cal_run(&s_cal, &err));
    }
    printf("\n/* 3.5in RPi display touch calibration, worst point %.1f px. Paste into your code if you\n"
           " * want to skip calibration: */\n"
           "touch_cal_t cal = { %.6ff, %.6ff, %.3ff, %.6ff, %.6ff, %.3ff };\n\n",
           err, s_cal.a, s_cal.b, s_cal.c, s_cal.d, s_cal.e, s_cal.f);
}

/* ---------------------------------------------------------------- touch tester -- */

static const char *BTN[3] = {"CLEAR", "RECALIBRATE", "COLOURS"};

static void tester_screen(void)
{
    rpi_lcd_fill_rect(0, 0, LCD_W, LCD_H, C_BG);
    for (int x = 0; x < LCD_W; x += 40) rpi_lcd_fill_rect(x, STAT_H, 1, LCD_H - STAT_H - BAR_H, C_GRID);
    for (int y = 40; y < LCD_H - BAR_H; y += 40) rpi_lcd_fill_rect(0, y, LCD_W, 1, C_GRID);
    for (int i = 0; i < 5; i++) {
        rpi_lcd_fill_rect(REF_X[i] - 10, REF_Y[i], 21, 1, C_REF);
        rpi_lcd_fill_rect(REF_X[i], REF_Y[i] - 10, 1, 21, C_REF);
    }
    for (int i = 0; i < 3; i++) {
        int x = i * 160;
        rpi_lcd_fill_rect(x + 2, LCD_H - BAR_H + 2, 156, BAR_H - 4, C_BTN);
        rpi_lcd_text(x + 80 - (int)strlen(BTN[i]) * 4, LCD_H - BAR_H + 8, BTN[i], C_TXT, C_BTN, 1);
    }
    rpi_lcd_fill_rect(0, 0, LCD_W, STAT_H, C_BG);
    rpi_lcd_text(4, 1, "drag to draw, tap a grey cross to check accuracy", C_DIM, C_BG, 1);
}

static void status(uint16_t fg, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void status(uint16_t fg, const char *fmt, ...)
{
    char b[61];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    int x = rpi_lcd_text(4, 1, b, fg, C_BG, 1);
    rpi_lcd_fill_rect(x, 0, LCD_W - x, STAT_H, C_BG);
}

static void dot(int x, int y)
{
    if (y < STAT_H + 1 || y > LCD_H - BAR_H - 2) return;
    rpi_lcd_fill_rect(x - 1, y - 1, 3, 3, C_INK);
}

static void line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        dot(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

static void wait_release(void) { while (xpt2046_pressed()) vTaskDelay(pdMS_TO_TICKS(10)); }

static void tester(void)
{
    tester_screen();
    int lx = -1, ly = -1;
    bool was_down = false;
    int64_t last_status = 0;
    for (;;) {
        uint16_t rx, ry, rz;
        if (!xpt2046_read_raw(&rx, &ry, &rz)) {
            was_down = false;
            lx = -1;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        int px, py;
        touch_cal_map(&s_cal, rx, ry, &px, &py);
        bool edge = !was_down;
        was_down = true;

        if (py >= LCD_H - BAR_H) {                       /* buttons act on touch-down only */
            if (!edge) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
            int b = px / 160;
            wait_release();
            if (b == 1) calibrate(true);
            if (b == 2) {
                colour_bars();
                centre_text(212, " tap to go back ", C_TXT, C_BG, 1);
                while (!xpt2046_pressed()) vTaskDelay(pdMS_TO_TICKS(10));
                wait_release();
            }
            tester_screen();
            was_down = false;
            continue;
        }

        if (edge) {                                      /* accuracy check on touch-down */
            for (int i = 0; i < 5; i++) {
                float d = hypotf(px - REF_X[i], py - REF_Y[i]);
                if (d < 25) {
                    status(d <= 4 ? C_GOOD : d <= 8 ? C_WARN : C_BAD,
                           "cross %d: touch landed %.0f px off (%+d,%+d)", i + 1, d,
                           px - REF_X[i], py - REF_Y[i]);
                    ESP_LOGI(TAG, "reference %d (%d,%d): touch (%d,%d), off %.1f px", i + 1,
                             REF_X[i], REF_Y[i], px, py, d);
                    last_status = esp_timer_get_time() + 1500000;   /* keep it up a while */
                }
            }
        }
        if (esp_timer_get_time() > last_status) {
            status(C_TXT, "x %3d  y %3d   raw %4u %4u   z %4u", px, py, rx, ry, rz);
            last_status = esp_timer_get_time() + 50000;
        }
        /* join to the previous point when close; a big jump is a new stroke or noise */
        if (lx >= 0 && abs(px - lx) < 30 && abs(py - ly) < 30) line(lx, ly, px, py); else dot(px, py);
        lx = px;
        ly = py;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);
    ESP_ERROR_CHECK(rpi_lcd_init());
    ESP_ERROR_CHECK(xpt2046_init());

    int us = speed_test();
    colour_bars();
    char m[48];
    snprintf(m, sizeof m, " full frame %.1f ms = %.1f fps ", us / 1000.0, 1e6 / us);
    centre_text(204, m, C_TXT, C_BG, 1);
    centre_text(228, " hold the screen now to recalibrate ", C_INK, C_BG, 1);
    bool held = false;
    for (int i = 0; i < 200 && !held; i++) { held = xpt2046_pressed(); vTaskDelay(pdMS_TO_TICKS(10)); }
    if (held) {
        centre_text(228, "   release to start calibration    ", C_INK, C_BG, 1);
        wait_release();
    }
    calibrate(held);
    tester();
}
