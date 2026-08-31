#include "wake.hpp"

#include <M5Unified.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "hal/usb_serial_jtag_ll.h"
#include "esp_sleep.h"

#include <M5PM1.h>

extern "C" {
#include "riddle_decide.h"
}

static const char *TAG = "wake";

// Seconds until the alarm wake_arm_next() last armed. The PM1 timer uses it as
// a second, independent way back from a power-off.
static uint32_t g_secs_to_wake = 0;

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
    g_secs_to_wake = (next > now) ? (uint32_t)(next - now) : 0;

    struct tm lt;
    localtime_r(&next, &lt);
    ESP_LOGI(TAG, "next wake %02d:%02d local (%s), in %lld s",
             lt.tm_hour, lt.tm_min, morning ? "riddle" : "answer",
             (long long)(next - now));

    m5::rtc_time_t t;
    t.hours   = lt.tm_hour;
    t.minutes = lt.tm_min;
    t.seconds = 0;
    // CHECK THE RETURN. It was discarded for the whole life of this function,
    // which is how an alarm that never armed could still report success.
    // -1 means M5Unified has no RTC driver bound at all.
    if (M5.Rtc.setAlarmIRQ(t) < 0) {
        ESP_LOGE(TAG, "setAlarmIRQ refused -- no RTC driver");
        return false;
    }

    // THIS DOES NOT VERIFY THE ALARM, and the comment here used to claim it
    // did: "READ IT BACK. An alarm that silently failed to arm produces a
    // board that never wakes again and looks exactly like one that works."
    // getTime() reads the CLOCK. It proves the RTC is answering on the bus and
    // nothing whatever about the alarm register, so the failure it was written
    // to catch was exactly the failure it could not see. M5Unified exposes no
    // alarm readback, so the honest position is: this is a bus check, and the
    // real guarantee that the board comes back is the timer backstop now armed
    // on both sleep paths.
    m5::rtc_time_t got;
    if (!M5.Rtc.getTime(&got)) {
        ESP_LOGE(TAG, "RTC will not answer on the bus -- assuming FAILED");
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
    // FOURTH ATTEMPT AT "IS A CABLE ATTACHED". The first three:
    //
    //   M5.Power.getVBUSVoltage()      PM1 0x24/0x25 = VIN, the boosted SYSTEM
    //                                  rail. ~5100mV on battery. Always true.
    //   usb_serial_jtag_is_connected() flag initialised to true, cleared only
    //                                  by a tick hook not running in this build.
    //   SOF with a clear-then-wait     CLEARING was the bug. The console driver
    //                                  clears the same status, so the 20ms
    //                                  window raced it and came back empty on a
    //                                  CABLED board -- which then powered off
    //                                  while plugged in.
    //
    // pm1.readVin() looks like the obvious fix and is not: it reads exactly the
    // same 0x24/0x25 that M5Unified does. The PM1 offers 5VIN insert/remove
    // EVENTS but no level, so there is no status bit to ask either.
    //
    // So: SOF again, WITHOUT clearing. A USB host sends a Start-Of-Frame every
    // 1ms and the peripheral latches it as a raw interrupt bit. On battery no
    // SOF can have arrived since boot, so the bit reads zero however long you
    // look; cabled, it is set within a millisecond. Never clearing removes the
    // race entirely -- we only read -- and sampling for 200ms catches the bit
    // even if something else clears it between passes.
    for (int i = 0; i < 40; i++) {
        if (usb_serial_jtag_ll_get_intraw_mask() & USB_SERIAL_JTAG_INTR_SOF)
            return true;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
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

namespace {
M5PM1 g_pm1;

// PM1 GPIO2 is the RTC interrupt line -- PYG2 in M5Stack's PaperColor pin
// table, and the same pin the factory firmware arms in hal.cpp.
constexpr m5pm1_gpio_num_t kPm1RtcWake = M5PM1_GPIO_NUM_2;

// PM1 GPIO0 switches the e-paper rail. Named EPD_EN in M5Stack's own hal.h.
constexpr m5pm1_gpio_num_t kPm1EpdEn = M5PM1_GPIO_NUM_0;

bool pm1_ready()
{
    static int state = -1;
    if (state < 0) {
        state = (g_pm1.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K)
                 == M5PM1_OK) ? 1 : 0;
        if (!state) ESP_LOGE(TAG, "PM1 did not answer on I2C");
    }
    return state == 1;
}
}  // namespace

void wake_panel_power_on()
{
    if (!pm1_ready()) {
        ESP_LOGE(TAG, "no PM1: cannot power the panel, the screen will not update");
        return;
    }

    // MEASURE FIRST. Three fixes have been written for "EPD_EN is low after a
    // power-off" without anyone reading the pin. These four registers are the
    // PM1's GPIO control, bit 0 being EPD_EN:
    //   0x16 function (0 = plain GPIO)   0x10 direction (1 = output)
    //   0x13 drive    (0 = push-pull)    0x11 output level
    auto reg = [](uint8_t r) {
        return (int)M5.In_I2C.readRegister8(0x6E, r, 100000);
    };
    ESP_LOGW(TAG, "EPD_EN before: func=%d dir=%d drive=%d level=%d "
                  "(raw 0x16=%02x 0x10=%02x 0x13=%02x 0x11=%02x)",
             reg(0x16) & 1, reg(0x10) & 1, reg(0x13) & 1, reg(0x11) & 1,
             reg(0x16), reg(0x10), reg(0x13), reg(0x11));

    const bool ok =
        g_pm1.gpioSetFunc(kPm1EpdEn, M5PM1_GPIO_FUNC_GPIO) == M5PM1_OK &&
        g_pm1.gpioSetMode(kPm1EpdEn, M5PM1_GPIO_MODE_OUTPUT) == M5PM1_OK &&
        g_pm1.gpioSetOutput(kPm1EpdEn, 1) == M5PM1_OK;

    ESP_LOGW(TAG, "EPD_EN after:  func=%d dir=%d drive=%d level=%d  (writes %s)",
             reg(0x16) & 1, reg(0x10) & 1, reg(0x13) & 1, reg(0x11) & 1,
             ok ? "ok" : "FAILED");
}

bool wake_was_pm1_rtc()
{
    if (!pm1_ready()) return false;
    uint8_t src = 0;
    if (g_pm1.getWakeSource(&src, M5PM1_CLEAN_ALL) != M5PM1_OK) return false;
    ESP_LOGI(TAG, "PM1 wake source mask 0x%02x", (unsigned)src);
    return (src & M5PM1_WAKE_SRC_EXT_WAKE) != 0;
}

void wake_power_off()
{
    // The RTC line must be released before arming a falling edge on it, or the
    // PM1 sees the edge that is already there and powers straight back on.
    wake_clear_alarm();

    bool armed = pm1_ready();
    if (armed) {
        armed = g_pm1.gpioSetFunc(kPm1RtcWake, M5PM1_GPIO_FUNC_WAKE) == M5PM1_OK
             && g_pm1.gpioSetPull(kPm1RtcWake, M5PM1_GPIO_PULL_UP) == M5PM1_OK
             && g_pm1.gpioSetWakeEdge(kPm1RtcWake, M5PM1_GPIO_WAKE_FALLING) == M5PM1_OK
             && g_pm1.gpioSetWakeEnable(kPm1RtcWake, true) == M5PM1_OK;
    }

    if (!armed) {
        // NEVER power off unarmed. A board that shuts down with no wake source
        // is not asleep, it is off until a person finds it and presses power --
        // and on a wall that is indistinguishable from broken. Deep sleep is
        // more expensive and always wakes.
        ESP_LOGE(TAG, "PM1 wake pin would not arm -- falling back to deep sleep");
        wake_sleep();
    }

    // TWO WAYS BACK, NOT ONE. The RTC edge above depends on the IRQ line being
    // wired to PM1 GPIO2 and on the alarm firing exactly as configured. The
    // PM1's own timer depends on neither. A board that powers off and does not
    // come back is not asleep, it is bricked until someone finds it -- so the
    // timer is armed a minute late as a backstop, and whichever fires first
    // wins.
    //
    // g_secs_to_wake is set by wake_arm_next(), which has already run.
    if (g_secs_to_wake > 0) {
        const uint32_t backstop = g_secs_to_wake + 60;
        if (g_pm1.timerSet(backstop, M5PM1_TIM_ACTION_POWERON) == M5PM1_OK)
            ESP_LOGW(TAG, "PM1 timer backstop armed for %u s", (unsigned)backstop);
        else
            ESP_LOGE(TAG, "PM1 timer backstop would not arm; RTC edge is alone");
    }

    ESP_LOGW(TAG, "PM1 power off; back on the RTC edge or the %u s timer",
             (unsigned)(g_secs_to_wake + 60));
    g_pm1.sysCmd(M5PM1_SYS_CMD_OFF);

    // sysCmd does not return on success. If we are still here it failed, and
    // sleeping is better than falling through into the rest of app_main.
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGE(TAG, "PM1 refused to power off -- deep sleeping instead");
    wake_sleep();
}

// See wake.hpp. This used to check once and return, which is the bug this
// replaces.
[[noreturn]] void wake_sleep_if_safe(bool next_is_morning)
{
    // Poll often enough that unplugging the cable puts the board to sleep
    // while you are still standing next to it. 30s is well under the time it
    // takes to walk away and wonder whether it worked.
    constexpr int kPollSecs = 30;

    // AND SLEEP ANYWAY AFTER THIS, whatever the cable check says.
    //
    // 30 minutes is chosen against both things it has to survive. It is longer
    // than any flash-and-watch cycle -- a build, a flash and a 17s refresh is
    // three minutes -- so it will not interrupt work. And it is far shorter
    // than the 6.5 hours between the morning and the reveal, so a board that
    // trips it on a wall is asleep again long before its next alarm.
    //
    // The cost when it fires wrongly is one 30-minute awake period, roughly 4%
    // of the battery, once. The cost of not having it is a board that stays
    // awake forever.
    constexpr int kMaxAwakeSecs = 30 * 60;

    bool said_it = false;
    for (int waited = 0; ; waited += kPollSecs) {
        const bool usb = wake_usb_present();
        const bool btn = wake_button_held();
        if (!usb && !btn) {
            if (said_it) ESP_LOGW(TAG, "cable gone after %ds; sleeping", waited);
            break;
        }
        if (waited >= kMaxAwakeSecs) {
            // WE HAVE BEEN AWAKE TOO LONG TO STILL BE RIGHT. Either the cable
            // really has been in for half an hour, or the check is stuck --
            // and the second one is indistinguishable from the first while it
            // is happening. Sleeping is recoverable; staying awake is not.
            ESP_LOGE(TAG, "awake %ds with %s still asserted -- sleeping anyway",
                     waited, usb ? "USB" : "a button");
            // Deep sleep, not a PM1 power-off, on this path only. We are in a
            // state we do not understand, and deep sleep comes back on a
            // button as well as the alarm. A power-off that mis-arms its timer
            // never comes back at all.
            wake_sleep();
        }
        if (!said_it) {
            ESP_LOGW(TAG, "%s; staying awake (rechecking every %ds, hard limit %ds)",
                     usb ? "USB attached, so the board stays flashable"
                         : "button held (escape hatch)",
                     kPollSecs, kMaxAwakeSecs);
            // Log the three readings once, for the record. VBUS is the rail
            // that fooled this code for a day; 5VINOUT is the one candidate
            // not yet ruled out.
            if (pm1_ready()) {
                uint16_t vinout = 0;
                g_pm1.read5VInOut(&vinout);
                ESP_LOGW(TAG, "cable check: SOF=%s, VBUS=%dmV, 5VINOUT=%umV",
                         usb ? "YES" : "no",
                         (int)M5.Power.getVBUSVoltage(), (unsigned)vinout);
            }
            said_it = true;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollSecs * 1000));
    }

    // The guess window decides how we sleep. See wake.hpp.
    if (next_is_morning) wake_power_off();
    wake_sleep();
}

void wake_sleep()
{
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

    // TWO WAYS BACK, NOT ONE -- the same rule wake_power_off() already states
    // and this path did not follow.
    //
    // The EXT1 mask above depends on the RX8130's IRQ line reaching G7 and on
    // the alarm firing exactly as configured. Nothing had ever proven either.
    // The power-off path hedged that with a PM1 timer; this one hedged it with
    // nothing, so the 13:00 reveal -- the only wake that takes this path --
    // rested entirely on an untested line. On 2026-08-31 it did not fire, and
    // the board sat asleep through its own reveal looking exactly like a board
    // that was working.
    //
    // The ESP32's own RTC timer depends on no external wiring at all. A minute
    // late, so the alarm edge still wins when it works and the page is not
    // drawn twice; whichever fires first wins.
    if (g_secs_to_wake > 0) {
        const uint64_t backstop = (uint64_t)(g_secs_to_wake + 60) * 1000000ULL;
        esp_sleep_enable_timer_wakeup(backstop);
        ESP_LOGW(TAG, "timer backstop armed for %u s", (unsigned)(g_secs_to_wake + 60));
    } else {
        // Reached when nothing armed an alarm this boot. Buttons still wake
        // it, but nothing else will, and that is worth saying out loud.
        ESP_LOGE(TAG, "NO TIMER BACKSTOP: only the RTC edge and the buttons "
                      "can wake this board");
    }

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
