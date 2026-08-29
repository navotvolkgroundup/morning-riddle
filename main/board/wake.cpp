#include "wake.hpp"

#include <M5Unified.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "hal/usb_serial_jtag_ll.h"
#include "esp_sleep.h"

#include "state.hpp"

extern "C" {
#include "riddle_decide.h"
}

static const char *TAG = "wake";

// Israel, with the DST rules the riddle schedule already uses. Kept identical
// to the Waveshare build's string on purpose: the host tests pin behaviour
// against it, and a second spelling would be a second thing to get wrong.
#define WAKE_TZ "IST-2IDT,M3.4.4/26,M10.5.0"

namespace {

constexpr uint64_t kWakeMask =
    (1ULL << WAKE_PIN_RTC_INT) | (1ULL << WAKE_PIN_BTN_TOP) |
    (1ULL << WAKE_PIN_BTN_MIDDLE) | (1ULL << WAKE_PIN_BTN_BOTTOM);

int g_button = -1;

}  // namespace

wake_cause wake_why()
{
    // RELEASE THE SLEEP HOLDS FIRST.
    //
    // wake_sleep() latches these pins so they do not float through deep sleep,
    // and the latch SURVIVES the wake -- it is not cleared by reset. Left held,
    // every button reads its frozen sleep-time level for the rest of the boot,
    // so the buttons appear stuck and nothing responds. Releasing here means it
    // happens on every path, including the ones that return early.
    gpio_deep_sleep_hold_dis();
    for (int pin : { WAKE_PIN_RTC_INT, WAKE_PIN_BTN_TOP, WAKE_PIN_BTN_MIDDLE,
                     WAKE_PIN_BTN_BOTTOM }) {
        gpio_hold_dis((gpio_num_t)pin);
    }

    const esp_sleep_wakeup_cause_t c = esp_sleep_get_wakeup_cause();
    if (c != ESP_SLEEP_WAKEUP_EXT1) {
        return (c == ESP_SLEEP_WAKEUP_UNDEFINED) ? wake_cause::cold
                                                 : wake_cause::other;
    }

    // EXT1 reports every pin that was low, so a button held while the alarm
    // fires shows both. The alarm wins: the page is due either way, and
    // treating it as a button press would skip the draw and leave a stale
    // screen until the afternoon.
    const uint64_t status = esp_sleep_get_ext1_wakeup_status();
    if (status & (1ULL << WAKE_PIN_RTC_INT)) return wake_cause::alarm;

    // Top of the screen is choice 0. See the pin block in wake.hpp: this was
    // inverted, and the board answered a press with the wrong colour.
    if (status & (1ULL << WAKE_PIN_BTN_TOP)) g_button = 0;
    else if (status & (1ULL << WAKE_PIN_BTN_MIDDLE)) g_button = 1;
    else if (status & (1ULL << WAKE_PIN_BTN_BOTTOM)) g_button = 2;

    return (g_button >= 0) ? wake_cause::button : wake_cause::other;
}

int wake_button_index() { return g_button; }

bool wake_sync_clock()
{
    if (!M5.Rtc.isEnabled()) {
        ESP_LOGE(TAG, "no RTC: the schedule cannot be trusted");
        return false;
    }
    M5.Rtc.setSystemTimeFromRtc();
    setenv("TZ", WAKE_TZ, 1);
    tzset();
    return true;
}

bool wake_arm_next(time_t now, int *is_morning)
{
    if (!M5.Rtc.isEnabled()) {
        ESP_LOGE(TAG, "no RTC: refusing to claim an alarm is armed");
        return false;
    }

    int morning = 0;
    const time_t next = riddle_next_wake(now, WAKE_TZ, &morning);
    if (is_morning) *is_morning = morning;

    struct tm lt;
    localtime_r(&next, &lt);
    ESP_LOGI(TAG, "next wake %02d:%02d local (%s), in %lld s",
             lt.tm_hour, lt.tm_min, morning ? "riddle" : "answer",
             (long long)(next - now));

    m5::rtc_time_t t;
    t.hours   = lt.tm_hour;
    t.minutes = lt.tm_min;
    t.seconds = 0;
    M5.Rtc.setAlarmIRQ(t);

    // READ IT BACK. An alarm that silently failed to arm produces a board that
    // never wakes again and looks exactly like one that works.
    m5::rtc_time_t got;
    if (!M5.Rtc.getTime(&got)) {
        ESP_LOGE(TAG, "alarm set but the RTC will not answer -- assuming FAILED");
        return false;
    }
    ESP_LOGI(TAG, "RTC now reads %02d:%02d:%02d", got.hours, got.minutes,
             got.seconds);
    return true;
}

void wake_clear_alarm()
{
    if (M5.Rtc.isEnabled()) M5.Rtc.clearIRQ();
}

bool wake_usb_present()
{
    // ASK THE USB PERIPHERAL, NOT THE PMIC.
    //
    // This used to read M5.Power.getVBUSVoltage() and call anything over
    // 4000mV "cabled". On this board that call reads PM1 registers 0x24/0x25,
    // which are VIN -- the SYSTEM input rail, boosted from the battery. It
    // reports ~5040mV with no cable attached at all, so the test was true
    // always and the board NEVER SLEPT. Measured on battery: VBUS=5040mV,
    // button held=no, reached deep sleep: NO.
    //
    // That is not a small bug. A device on a wall that never sleeps misses
    // every scheduled wake and flattens its cell in about a day, and none of
    // it is visible over USB because a cabled board is supposed to stay awake.
    //
    // usb_serial_jtag_is_connected() was the next thing tried and it is ALSO
    // wrong here: it initialises its flag to true and only ever clears it from
    // a FreeRTOS tick hook, which is not running in this build. Measured on
    // battery, it still reported connected and the board still refused to
    // sleep. Two wrong USB tests in a row, both taken on trust.
    //
    // So read the signal itself. A USB host sends a Start-Of-Frame packet
    // every 1ms; the peripheral latches that as a raw interrupt bit. Clear it,
    // wait 20ms, and look: on a cabled board dozens of SOFs will have arrived,
    // and on battery none can. No driver, no hook, no cached flag.
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SOF);
    vTaskDelay(pdMS_TO_TICKS(20));
    return (usb_serial_jtag_ll_get_intraw_mask() & USB_SERIAL_JTAG_INTR_SOF) != 0;
}

bool wake_button_held()
{
    for (int pin : { WAKE_PIN_BTN_TOP, WAKE_PIN_BTN_MIDDLE, WAKE_PIN_BTN_BOTTOM }) {
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
        gpio_pullup_en((gpio_num_t)pin);
        if (gpio_get_level((gpio_num_t)pin) == 0) return true;   // active low
    }
    return false;
}

bool wake_sleep_if_safe()
{
    // Record the inputs to this decision before acting on them. On battery
    // there is no serial, so a refusal is otherwise silent -- and "the board
    // never slept" is indistinguishable from "the board slept and did not
    // wake" without it.
    const int16_t vbus = M5.Power.getVBUSVoltage();
    const bool usb = wake_usb_present();
    ESP_LOGW(TAG, "sleep decision: SOF-detected USB=%s, VBUS=%dmV (VBUS is the "
                  "boosted system rail here, not a cable test)",
             usb ? "YES" : "no", (int)vbus);
    state_note_awake((int)vbus, wake_button_held());

    if (wake_usb_present()) {
        ESP_LOGW(TAG, "USB attached; staying awake so the board stays flashable");
        return false;
    }
    if (wake_button_held()) {
        ESP_LOGW(TAG, "button held; staying awake (escape hatch)");
        return false;
    }
    wake_sleep();
}

void wake_sleep()
{
    // Record the attempt BEFORE sleeping. If the next boot reports a cold
    // cause with "reached deep sleep: YES", the board slept and something
    // other than an EXT1 button woke it -- most likely a full power-down.
    state_note_sleeping();

    // Clear here, not earlier: anything between the clear and the sleep can
    // let the line assert again. The first version cleared before a 30-second
    // delay and the board woke the instant it slept.
    wake_clear_alarm();

    // The line must be HIGH before sleeping. EXT1 wakes on low, so a line
    // already low means an immediate wake -- a busy loop that looks like a
    // crash and drains the battery. Report it rather than sleeping blind.
    gpio_set_direction((gpio_num_t)WAKE_PIN_RTC_INT, GPIO_MODE_INPUT);
    gpio_pullup_en((gpio_num_t)WAKE_PIN_RTC_INT);
    const int rtc_level = gpio_get_level((gpio_num_t)WAKE_PIN_RTC_INT);
    ESP_LOGI(TAG, "RTC_INT (G%d) reads %d before sleep%s", WAKE_PIN_RTC_INT,
             rtc_level, rtc_level ? "" : "  <-- LOW: will wake immediately");

    ESP_LOGI(TAG, "sleeping: RTC on G%d, buttons G%d/G%d/G%d",
             WAKE_PIN_RTC_INT, WAKE_PIN_BTN_TOP, WAKE_PIN_BTN_MIDDLE,
             WAKE_PIN_BTN_BOTTOM);

    // All four lines idle high and pull low when asserted, so ANY_LOW covers
    // the alarm and every button with one mask.
    esp_sleep_enable_ext1_wakeup_io(kWakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

    // Hold the pull-ups through sleep, or the inputs float and the board wakes
    // on noise -- which on a battery device is indistinguishable from a bug.
    for (int pin : { WAKE_PIN_RTC_INT, WAKE_PIN_BTN_TOP, WAKE_PIN_BTN_MIDDLE,
                     WAKE_PIN_BTN_BOTTOM }) {
        gpio_pullup_en((gpio_num_t)pin);
        gpio_hold_en((gpio_num_t)pin);
    }
    gpio_deep_sleep_hold_en();

    esp_deep_sleep_start();
}
