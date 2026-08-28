#include "feedback.hpp"

#include <M5Unified.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "feedback";

namespace {

// One colour and one note per choice, rising left to right. The pairing is
// deliberate: colour alone fails for a colour-blind reader and sound alone
// fails in a noisy kitchen, so each channel carries the whole answer.
struct choice_fb { uint8_t r, g, b; int hz; };
constexpr choice_fb kChoice[3] = {
    { 0x00, 0x80, 0xFF,  784 },   // A: blue,   G5
    { 0x00, 0xC0, 0x30,  988 },   // B: green,  B5
    { 0xFF, 0x80, 0x00, 1175 },   // C: orange, D6
};

constexpr int kOnMs = 220;

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!M5.Led.isEnabled()) return;
    M5.Led.setBrightness(60);       // a wall device at night; not a torch
    M5.Led.setAllColor(r, g, b);
    M5.Led.display();
}

}  // namespace

void feedback_guess(int choice)
{
    if (choice < 0 || choice > 2) { feedback_reject(); return; }
    const choice_fb &f = kChoice[choice];

    ESP_LOGI(TAG, "guess %d: led #%02x%02x%02x, %d Hz", choice, f.r, f.g, f.b, f.hz);
    led_set(f.r, f.g, f.b);
    if (M5.Speaker.isEnabled()) M5.Speaker.tone(f.hz, kOnMs);
}

void feedback_reject()
{
    // Red and a low double note. Different in colour AND rhythm, so it does
    // not read as "a guess landed" to anyone who only catches one channel.
    ESP_LOGI(TAG, "rejected");
    led_set(0xFF, 0x00, 0x00);
    if (M5.Speaker.isEnabled()) {
        M5.Speaker.tone(330, 90);
        vTaskDelay(pdMS_TO_TICKS(130));
        M5.Speaker.tone(262, 140);
    }
}

void feedback_settle()
{
    // Wait for the tone to actually finish. Deep sleep cuts I2S and RMT
    // mid-output, which turns the chirp into a click and can leave the LED
    // latched on -- for hours, on a battery, with nobody there to notice.
    if (M5.Speaker.isEnabled()) {
        const int64_t deadline = esp_timer_get_time() + 2'000'000;   // 2s cap
        while (M5.Speaker.isPlaying() && esp_timer_get_time() < deadline)
            vTaskDelay(pdMS_TO_TICKS(10));
        M5.Speaker.stop();
    }
    vTaskDelay(pdMS_TO_TICKS(kOnMs));
    led_set(0x00, 0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(20));      // let the RMT frame clock out
}
