#include "feedback.hpp"

#include <M5Unified.h>

#include "driver/gpio.h"
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

void feedback_ack()
{
    // Dim white and one brief note. Not a choice colour, so it cannot be read
    // as "your guess landed"; not red, so it cannot be read as "no".
    ESP_LOGI(TAG, "acknowledged (already answered today)");
    led_set(0x60, 0x60, 0x60);
    if (M5.Speaker.isEnabled()) M5.Speaker.tone(880, 70);
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

    feedback_release_straps();
}

void feedback_release_straps()
{
    // RELEASE BOTH AUDIO ENABLES. M5Unified drives G45 (codec enable) and G46
    // (speaker enable) as outputs and takes them HIGH whenever the speaker is
    // enabled -- M5Unified.cpp:529-548. BOTH are ESP32-S3 boot strapping pins.
    // Left driven, the next reset samples them and boots to DOWNLOAD mode
    // instead of running the application, and only a full power removal
    // clears it.
    //
    // That is not theoretical, and it has now happened twice. The first time
    // it made the board appear permanently bricked: flashes verified, nothing
    // ever ran, boot line "rst:0x3 (RTC_SW_SYS_RST), boot:0x21 (DOWNLOAD)".
    //
    // The second time was worse, because the fix was already written and only
    // half applied. This ran on the button path alone, and it released G46
    // alone. So every ORDINARY boot -- the one that draws the page, twice a
    // day, every day -- left both pins driven and armed the trap for the next
    // reset. A power cycle bought exactly one good boot, which re-armed it.
    // The symptom was a board that drew its page once and then refused to boot
    // again, which looks nothing like an audio bug.
    //
    // So this is its own function, it takes both pins, and main.cpp calls it
    // on EVERY path rather than only the one that makes a noise.
    M5.Speaker.end();
    for (gpio_num_t pin : { GPIO_NUM_45, GPIO_NUM_46 }) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
    }
}
