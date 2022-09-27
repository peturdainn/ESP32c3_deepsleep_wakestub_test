/*
 * Sample deep sleep with wake stub for ESP32-c3
 * (May contain bits of code found online, no license info)
 */

#include <stdio.h>
#include <string.h>
#include "esp_sleep.h"
#include "esp_log.h"
#include "rom/rtc.h"
#include "soc/rtc.h"

#include "soc/timer_group_reg.h"
#include "soc/timer_periph.h"
#include "soc/rtc_cntl_reg.h"

// gpio
#include "rom/gpio.h"
#include "driver/rtc_io.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WAKETEST_USE_GPIO
#define WAKETEST_USE_TIMER

#ifdef WAKETEST_USE_GPIO
#define WAKETEST_GPIO_NUM   1
#define WAKETEST_GPIO_LEVEL ESP_GPIO_WAKEUP_GPIO_HIGH
#endif

#ifdef WAKETEST_USE_GPIO
#define WAKETEST_TIMER_INTERVAL_S   10
#endif


#ifdef WAKETEST_USE_TIMER
#define S_TO_NS 1000000ULL
// Comment out this line if you're using the internal RTC RC (150KHz) oscillator.
//#define USE_EXTERNAL_RTC_CRYSTAL
#ifdef USE_EXTERNAL_RTC_CRYSTAL
#define DEEP_SLEEP_TIME_OVERHEAD_US (650 + 100 * 240 / CONFIG_ESP32C3_DEFAULT_CPU_FREQ_MHZ)
#else
#define DEEP_SLEEP_TIME_OVERHEAD_US (250 + 100 * 240 / CONFIG_ESP32C3_DEFAULT_CPU_FREQ_MHZ)
#endif // USE_EXTERNAL_RTC_CRYSTAL
RTC_IRAM_ATTR void set_deepsleep_timer(uint64_t duration_us)
{
    // Get RTC calibration
    uint32_t period = REG_READ(RTC_SLOW_CLK_CAL_REG);

    // Calculate sleep duration in microseconds
    int64_t sleep_duration = (int64_t)duration_us - (int64_t)DEEP_SLEEP_TIME_OVERHEAD_US;
    if (sleep_duration < 0)
        sleep_duration = 0;

    // Convert microseconds to RTC clock cycles
    int64_t rtc_count_delta = (sleep_duration << RTC_CLK_CAL_FRACT) / period;

    // Get current RTC time
    SET_PERI_REG_MASK(RTC_CNTL_TIME_UPDATE_REG, RTC_CNTL_TIME_UPDATE);      // force an update first
    uint64_t now = ((uint64_t)(READ_PERI_REG(RTC_CNTL_TIME0_REG))) | (((uint64_t)(READ_PERI_REG(RTC_CNTL_TIME1_REG))) << 32);

    // Set wakeup time
    uint64_t future = now + (uint64_t)rtc_count_delta;
    WRITE_PERI_REG(RTC_CNTL_SLP_TIMER0_REG, future & UINT32_MAX);
    WRITE_PERI_REG(RTC_CNTL_SLP_TIMER1_REG, future >> 32);
    WRITE_PERI_REG(RTC_CNTL_SLP_TIMER1_REG, ((future >> 32) & 0xFFFF) | (RTC_CNTL_MAIN_TIMER_ALARM_EN));
}
#endif

static void RTC_IRAM_ATTR wake_stub()
{
    esp_default_wake_deep_sleep();

#ifdef WAKETEST_USE_GPIO
    // wait for GPIO to go idle, else we keep entering the stub
    while (GPIO_INPUT_GET(WAKETEST_GPIO_NUM) == WAKETEST_GPIO_LEVEL)
    {
        REG_WRITE(TIMG_WDTFEED_REG(0), 1);
    }
#endif

#ifdef WAKETEST_USE_TIMER
    // set a new timer trigger value
    set_deepsleep_timer(WAKETEST_TIMER_INTERVAL_S * S_TO_NS);
    // clear pending interrupts (if not, causes boot loop)
    WRITE_PERI_REG(RTC_CNTL_INT_CLR_REG, 0xFFFF);
#endif

    // to boot, return here.
    // to deep sleep again, continue below

    // required to stay in deep sleep
    WRITE_PERI_REG(RTC_ENTRY_ADDR_REG, (uint32_t)&wake_stub);
    set_rtc_memory_crc();

    // Go to sleep.
    CLEAR_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    SET_PERI_REG_MASK(RTC_CNTL_STATE0_REG, RTC_CNTL_SLEEP_EN);
    while (true);
}

void app_main(void)
{
    printf("Deep sleep test: Booted, sleep in 2 seconds\n");
    vTaskDelay(2000 / portTICK_PERIOD_MS);

#ifdef WAKETEST_USE_GPIO
    // Wake up on with logic level
    const gpio_config_t config = {
        .pin_bit_mask = BIT(WAKETEST_GPIO_NUM),
        .mode = GPIO_MODE_INPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK( esp_deep_sleep_enable_gpio_wakeup(BIT(WAKETEST_GPIO_NUM), WAKETEST_GPIO_LEVEL));
#endif

#ifdef WAKETEST_USE_TIMER
    // Wake up with timer
    esp_sleep_enable_timer_wakeup(10*1000*1000); // 10 seconds
#endif

    // configure to use custom stub
    esp_set_deep_sleep_wake_stub(&wake_stub);

    // Enter deep sleep
    //ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON));
    esp_deep_sleep_start();
}
