// Copyright 2020 Espressif Systems (Shanghai) Co. Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "stdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "iot_button.h"
#include "led_indicator.h"
#include "sleep.h"
#include "utils.h"
#include "camera.h"
#include "storage.h"
#include "misc.h"
#include "debug.h"
#include "esp_sleep.h"
#include "pir.h"
#include "http.h"
#include "wifi.h"

#define TAG "-->MISC"

#define LIGHT_DET_ADC1_CHN      (ADC_CHANNEL_0)
#define BATTERY_DET_ADC2_CHN    (ADC_CHANNEL_3)
#define ADC_ATTEN               (ADC_ATTEN_DB_12)
#define ADC_CALI_SCHEME         (ESP_ADC_CAL_VAL_EFUSE_TP_FIT)
#define ADC_SUM_N               (10)

typedef enum {
    LED_MODE_FLASH         =  0,
    LED_MODE_LIGHT,
} LED_MODE_E;

ledc_timer_config_t ledc_timer = {
    .speed_mode       = LEDC_LOW_SPEED_MODE,
    .timer_num        = LEDC_TIMER_0,
    .duty_resolution  = LEDC_TIMER_10_BIT,
    .freq_hz          = PWM_FREQ,  
    .clk_cfg          = LEDC_AUTO_CLK,
};

ledc_channel_config_t ledc_channel = {
    .speed_mode     = LEDC_LOW_SPEED_MODE,
    .channel        = LEDC_CHANNEL_0,
    .timer_sel      = LEDC_TIMER_0,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = PWM_IO,
    .duty           = 0,
    .hpoint         = 0,
};

/**
 * Structure for LED control state
 */
typedef struct miscLed {
    LED_MODE_E mode;                ///< Current LED mode (flash/light)
    SemaphoreHandle_t mutex;        ///< Mutex for thread-safe access
    ledc_timer_config_t *ledc_timer_c;  ///< LEDC timer configuration
    ledc_channel_config_t *ledc_channel_c; ///< LEDC channel configuration
    esp_timer_handle_t timer;       ///< Timer handle for blinking
    bool timer_state;               ///< Timer running state
    uint8_t flash_duty;             ///< Duty cycle for flash mode (0-100%)
    uint8_t light_duty;             ///< Duty cycle for light mode (0-100%) 
    uint8_t blink_cnt;              ///< Number of remaining blinks
    bool light_state;               ///< Current light state (on/off)
    bool hold_on;                   ///< Whether to hold light on
    bool light_update;              ///< Flag indicating light needs update
} miscLed_t;

typedef struct miscBtn {
    button_handle_t handle;
    button_event_t  event;
    int64_t press_time;
} miscBtn_t;
/**
 * Main miscellaneous module state structure
 */
typedef struct mdMisc {
    bool isInit;                    ///< Initialization flag
    miscBtn_t btn;                  ///< Button handler
    miscLed_t led;                  ///< LED control state
    uint8_t voltage;                ///< Last measured battery voltage
    adc_oneshot_unit_handle_t adc1_unit_handle; ///< ADC1 unit handle
    adc_oneshot_unit_handle_t adc2_unit_handle; ///< ADC2 unit handle  
    adc_cali_handle_t adc1_cali_handle; ///< ADC1 calibration handle
    adc_cali_handle_t adc2_cali_handle; ///< ADC2 calibration handle
    bool reset_flag;                ///< Flag indicating reset requested
} mdMisc_t;

static mdMisc_t g_misc = {0};

static void button_press_down_cb(void *arg, void *priv)
{
    ESP_LOGI(TAG, "BUTTON_PRESS_DOWN");
    g_misc.btn.event = BUTTON_PRESS_DOWN;
    g_misc.btn.press_time = esp_timer_get_time();
}

static void button_press_up_cb(void *arg, void *priv)
{
    ESP_LOGI(TAG, "BUTTON_PRESS_UP");
    g_misc.btn.event = BUTTON_PRESS_UP;
}

static void button_press_repeat_cb(void *arg, void *priv)
{
    ESP_LOGI(TAG, "BUTTON_PRESS_REPEAT[%d]", iot_button_get_repeat((button_handle_t)arg));
}

static void button_single_click_cb(void *arg, void *priv)
{
    ESP_LOGI(TAG, "BUTTON_SINGLE_CLICK");
    if (system_get_mode() == MODE_CONFIG && camera_snapshot(SNAP_BUTTON, 1) == ESP_OK) {
        misc_led_blink(1, 1000);
        wifi_clear_timeout();
        http_clear_timeout();
    }else if(system_get_mode() != MODE_CONFIG){
        sleep_set_wakeup_todo(WAKEUP_TODO_CONFIG);
        esp_sleep_enable_timer_wakeup(100000ULL);
        esp_deep_sleep_start();
    }
}

static void button_double_click_cb(void *arg, void *priv)
{
    ESP_LOGI(TAG, "BUTTON_DOUBLE_CLICK");
    storage_show_file();
    system_show_meminfo();
}

static void button_long_press_start_cb(void *arg, void *priv)
{
    ESP_LOGI(TAG, "BUTTON_LONG_PRESS_START");
    g_misc.btn.event = BUTTON_LONG_PRESS_START;
}

static void button_long_press_hold_cb(void *arg, void *priv)
{
    ESP_LOGI(TAG, "BUTTON_LONG_PRESS_HOLD");
    if(g_misc.reset_flag != 1 && ((esp_timer_get_time() - g_misc.btn.press_time) > BUTTON_RESET_TIME)){
        /*LED is controlled by timer, button callback is also processed in timer, so it is necessary to exit the callback. So put the restart event on task.*/
        misc_led_blink(5, 200);
        g_misc.reset_flag = 1;
        g_misc.btn.press_time = esp_timer_get_time();
    }
}

static void button_start()
{
    button_config_t cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = BUTTON_IO,
            .active_level = BUTTON_ACTIVE,
        },
    };
    g_misc.btn.handle = iot_button_create(&cfg);
    g_misc.btn.event = BUTTON_NONE_PRESS;
    iot_button_register_cb(g_misc.btn.handle, BUTTON_PRESS_DOWN, button_press_down_cb, NULL);
    iot_button_register_cb(g_misc.btn.handle, BUTTON_PRESS_UP, button_press_up_cb, NULL);
    iot_button_register_cb(g_misc.btn.handle, BUTTON_PRESS_REPEAT, button_press_repeat_cb, NULL);
    iot_button_register_cb(g_misc.btn.handle, BUTTON_SINGLE_CLICK, button_single_click_cb, NULL);
    iot_button_register_cb(g_misc.btn.handle, BUTTON_DOUBLE_CLICK, button_double_click_cb, NULL);
    iot_button_register_cb(g_misc.btn.handle, BUTTON_LONG_PRESS_START, button_long_press_start_cb, NULL);
    iot_button_register_cb(g_misc.btn.handle, BUTTON_LONG_PRESS_HOLD, button_long_press_hold_cb, NULL);
}

static void button_stop()
{
    iot_button_delete(g_misc.btn.handle);
}

static bool adc_calibration_new(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten,
                                adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif
    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void adc_calibration_delete(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}

static bool adc_calibration_init(void)
{
//-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &g_misc.adc1_unit_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_misc.adc1_unit_handle, LIGHT_DET_ADC1_CHN, &config));

    //-------------ADC1 Calibration Init---------------//
    bool do_calibration1_chan0 = adc_calibration_new(ADC_UNIT_1, LIGHT_DET_ADC1_CHN, ADC_ATTEN, &g_misc.adc1_cali_handle);

    //-------------ADC2 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config2 = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config2, &g_misc.adc2_unit_handle));

    //-------------ADC2 Calibration Init---------------//
    bool do_calibration2 = adc_calibration_new(ADC_UNIT_2, BATTERY_DET_ADC2_CHN, ADC_ATTEN, &g_misc.adc2_cali_handle);
    //-------------ADC2 Config---------------//
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_misc.adc2_unit_handle, BATTERY_DET_ADC2_CHN, &config));

    return do_calibration1_chan0 && do_calibration2;
}

static void adc_calibration_deinit(void)
{
    adc_calibration_delete(g_misc.adc1_cali_handle);
    adc_calibration_delete(g_misc.adc2_cali_handle);
    adc_oneshot_del_unit(g_misc.adc1_unit_handle);
    adc_oneshot_del_unit(g_misc.adc2_unit_handle);
}

static uint8_t  get_battery_voltage_rate()
{
    esp_err_t ret = ESP_OK;
    int voltage = 0;
    uint32_t sum = 0;
    int raw;
    uint8_t n = ADC_SUM_N;
    uint8_t rate;

    // misc_io_set(BATTERY_POWER_IO, BATTERY_POWER_ON);
    // vTaskDelay(pdMS_TO_TICKS(50));
    while (n) {
        do {
            ret = adc_oneshot_read(g_misc.adc2_unit_handle, BATTERY_DET_ADC2_CHN, &raw);
        } while (ret == ESP_ERR_INVALID_STATE);
        if (g_misc.adc2_cali_handle) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(g_misc.adc2_cali_handle, raw, &voltage));
            n--;
            // ESP_LOGI(TAG, "voltage %d", voltage);
        }
        sum += voltage;
    }
    // misc_io_set(BATTERY_POWER_IO, BATTERY_POWER_OFF);
    voltage = sum / ADC_SUM_N;
    if (voltage < BATTERY_MIN_VOLTAGE / 2) {
        // maybe typec inserted
        rate = 100;
    } else {
        voltage = MIN(MAX(voltage, BATTERY_MIN_VOLTAGE), BATTERY_MAX_VOLTAGE);
        rate = (uint8_t)((voltage - BATTERY_MIN_VOLTAGE) * 100 / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE));
    }
    ESP_LOGI(TAG, "battery voltage rate %d", rate);

    return rate;
}

static void adc_start()
{
    if (adc_calibration_init()) {
        ESP_LOGI(TAG, "adc calibration init success");
    } else {
        ESP_LOGW(TAG, "adc calibration init failed");
    }
    misc_io_cfg(LIGHT_POWER_IO, 0, 1);
    misc_io_set(LIGHT_POWER_IO, LIGHT_POWER_ON);
    misc_io_cfg(BATTERY_POWER_IO, 0, 1);
    misc_io_set(BATTERY_POWER_IO, BATTERY_POWER_ON);
    // misc_io_cfg(TYPEC_DET_IO, 1, 1);
}

static void adc_stop()
{
    adc_calibration_deinit();
}

/**
 * Set GPIO output level
 * @param io GPIO number to set
 * @param value Level to set (0=low, 1=high)
 */
void misc_io_set(uint8_t io, bool value)
{
    gpio_set_level(io, value);
}

/**
 * Get GPIO input level
 * @param io GPIO number to read
 * @return Current input level (0=low, 1=high)
 */
bool misc_io_get(uint8_t io)
{
    return gpio_get_level(io);
}

/**
 * Configure GPIO pin direction and pull mode
 * @param io GPIO number to configure
 * @param input True for input, false for output
 * @param pulldown True for pulldown, false for pullup
 */
void misc_io_cfg(uint8_t io, bool input, bool pulldown)
{
    gpio_config_t config = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = BIT64(io),
        .mode = input ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT,
        .pull_down_en = pulldown,
        .pull_up_en = !pulldown,
    };
    gpio_config(&config);
}

uint8_t misc_get_light_value_rate()
{
    esp_err_t ret = ESP_OK;
    int voltage = 0;
    int raw;
    uint8_t rate;
    uint32_t sum = 0;
    uint8_t n = ADC_SUM_N;

    // misc_io_set(LIGHT_POWER_IO, LIGHT_POWER_ON);
    // vTaskDelay(pdMS_TO_TICKS(20));
    while (n) {
        do {
            ret = adc_oneshot_read(g_misc.adc1_unit_handle, LIGHT_DET_ADC1_CHN, &raw);
        } while (ret == ESP_ERR_INVALID_STATE);
        if (g_misc.adc1_cali_handle) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(g_misc.adc1_cali_handle, raw, &voltage));
            sum += voltage;
            n--;
            // vTaskDelay(pdMS_TO_TICKS(1));
            // ESP_LOGI(TAG, "voltage %d", voltage);
        }
    }
    // misc_io_set(LIGHT_POWER_IO, LIGHT_POWER_OFF);
    voltage = sum / ADC_SUM_N;
    voltage = MIN(MAX(voltage, LIGHT_MIN_SENS), LIGHT_MAX_SENS);
    rate = (uint8_t)((voltage - LIGHT_MIN_SENS) * 100 / (LIGHT_MAX_SENS - LIGHT_MIN_SENS));
    ESP_LOGI(TAG, "light voltage rate %d", rate);

    return rate;
}

uint8_t misc_read_battery_voltage()
{
    g_misc.voltage = get_battery_voltage_rate();
    return g_misc.voltage;
}

uint8_t  misc_get_battery_voltage_rate()
{
    // if (misc_io_get(TYPEC_DET_IO) == TYPEC_INSERT) {
    //     return 100;
    // }
    if (g_misc.voltage == 0) {
        g_misc.voltage = get_battery_voltage_rate();
    }
    return g_misc.voltage;
}

void misc_pwm_ctrl(uint8_t enable, uint8_t duty)
{
    static int is_pause = 1;
    static int _duty;
    if(enable == 0 ){
        duty = 0;
        if(is_pause == 1)
            return;
    }else if(duty > 0 && duty < PWM_MIN_DUTY){
        duty = PWM_MIN_DUTY;
    }else if(duty >= 100){
        duty = 99;
    }
    // ESP_LOGI(TAG,"misc_pwm_ctrl enable:%d duty:%d\r\n",enable , duty);
    _duty = (1024 - 1) * (duty) / 100;
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, _duty);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
    if(is_pause && _duty > 0){
        // ledc_timer_resume(ledc_channel.speed_mode, ledc_channel.timer_sel);
        is_pause = 0;
    }else if(!is_pause && _duty == 0){
        // ledc_timer_pause(ledc_channel.speed_mode, ledc_channel.timer_sel);
        is_pause = 1;

    }
}

void misc_flash_led_open()
{
    xSemaphoreTake(g_misc.led.mutex, portMAX_DELAY);
    g_misc.led.mode = LED_MODE_FLASH;
    misc_pwm_ctrl(1, g_misc.led.flash_duty);
    xSemaphoreGive(g_misc.led.mutex);
}

void misc_flash_led_close()
{
    xSemaphoreTake(g_misc.led.mutex, portMAX_DELAY);
    if(g_misc.led.mode != LED_MODE_FLASH){
        xSemaphoreGive(g_misc.led.mutex);
        return;
    }
    g_misc.led.mode = LED_MODE_LIGHT;
    misc_pwm_ctrl(0, 0);
    g_misc.led.light_update = 1;
    xSemaphoreGive(g_misc.led.mutex);
}


void misc_led_able(uint8_t is_able)
{
    xSemaphoreTake(g_misc.led.mutex, portMAX_DELAY);
    g_misc.led.hold_on = is_able;
    xSemaphoreGive(g_misc.led.mutex);
}

void misc_led_blink(uint8_t blink_cnt,  uint16_t blink_interval)
{
    xSemaphoreTake(g_misc.led.mutex, portMAX_DELAY);
    g_misc.led.light_state = 1;
    g_misc.led.blink_cnt = blink_cnt;

    if(g_misc.led.timer_state == 1)
        esp_timer_stop(g_misc.led.timer);
    esp_timer_start_periodic(g_misc.led.timer, blink_interval * 1000); 
    g_misc.led.timer_state = 1;
    g_misc.led.light_update = 1;
    xSemaphoreGive(g_misc.led.mutex);
}

static void pwm_timer_cb(void *arg)
{
    xSemaphoreTake(g_misc.led.mutex, portMAX_DELAY);
    if(g_misc.led.blink_cnt > 0 && g_misc.led.light_state == 0){
        g_misc.led.light_state = 1;
        g_misc.led.light_update = 1;
    }else if(g_misc.led.light_state == 1){
        if(g_misc.led.blink_cnt > 0 )
            g_misc.led.blink_cnt--;
        g_misc.led.light_state = 0;
    }
    xSemaphoreGive(g_misc.led.mutex);
}

static void pwm_config()
{
    lightAttr_t light;
    g_misc.led.ledc_timer_c = &ledc_timer;
    g_misc.led.ledc_channel_c = &ledc_channel;
    ledc_timer_config(g_misc.led.ledc_timer_c);
    ledc_channel_config(g_misc.led.ledc_channel_c);
    g_misc.led.mutex = xSemaphoreCreateMutex();
    g_misc.led.mode = LED_MODE_LIGHT;

    const esp_timer_create_args_t timer_args = {
        pwm_timer_cb,
        &g_misc.led.timer,
        ESP_TIMER_TASK,
        "misc_led_timer",
        true,
    };
    esp_timer_create(&timer_args, &g_misc.led.timer);

    cfg_get_light_attr(&light);
    g_misc.led.flash_duty = light.duty;
    g_misc.led.light_duty = PWM_MIN_DUTY;
}

void misc_set_flash_duty(int duty)
{
    xSemaphoreTake(g_misc.led.mutex, portMAX_DELAY);
    g_misc.led.flash_duty = duty;
    if(g_misc.led.mode == LED_MODE_FLASH){
        misc_pwm_ctrl(1, g_misc.led.flash_duty);
    }
    xSemaphoreGive(g_misc.led.mutex);
}

void misc_set_led_duty(int duty)
{
    lightAttr_t light;
    g_misc.led.flash_duty = duty;
    cfg_get_light_attr(&light);
    if(light.duty != duty){
        light.duty = duty;
        cfg_set_light_attr(&light);
    }
    g_misc.led.light_duty = duty;
    misc_led_blink(1, 2000);
}
static int misc_test(int argc, char **argv)
{
    int cnt,blink_interval,duty;
    if (argc < 2) {
        return ESP_FAIL;
    }

    if (strcmp(argv[1], "led") == 0) {
        if (argc < 4) {
            return ESP_FAIL;
        }

        if(strcmp(argv[2], "flash") == 0){
            sscanf(argv[3], "%d", &duty);
            if(duty < 0 || duty > 100){
                return ESP_FAIL;
            }
            misc_set_led_duty(duty);
        }else{
            sscanf(argv[2], "%d", &cnt);
            sscanf(argv[3], "%d", &blink_interval);
            misc_led_blink(cnt, blink_interval);
        }
    }else if (strcmp(argv[1], "bat") == 0) {
        get_battery_voltage_rate();
    }else if (strcmp(argv[1], "pir") == 0) {
        if(strcmp(argv[2], "init") == 0){
            pir_init(1);
        }else if(strcmp(argv[2], "test") == 0){
            pir_int_trigger();
        }
    }else if (strcmp(argv[1], "light") == 0) {
        misc_get_light_value_rate();
    }else{
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_console_cmd_t g_cmd[] = {
    {"misc", "misc [led/bat/light/pir] (cmd)", NULL, misc_test, NULL},
};


static void misc_task()
{
    while (true) {
        xSemaphoreTake(g_misc.led.mutex, portMAX_DELAY);
        if(g_misc.led.timer_state == 1 && g_misc.led.blink_cnt == 0){ //Turn off the timer
            esp_timer_stop(g_misc.led.timer);
            g_misc.led.timer_state = 0;
        }

        if(g_misc.led.mode == LED_MODE_LIGHT){
            if(g_misc.led.light_state == 1 || g_misc.led.hold_on){
                if(g_misc.led.light_update){
                    misc_pwm_ctrl(1, g_misc.led.light_duty);
                    g_misc.led.light_update = 0;
                }
            }else{
                misc_pwm_ctrl(0, 0);
                g_misc.led.light_duty = PWM_MIN_DUTY;
            }
        }
        xSemaphoreGive(g_misc.led.mutex);

        if(g_misc.reset_flag == 1 && g_misc.btn.event == BUTTON_PRESS_UP){
            if(g_misc.led.timer_state == 0){//Waiting for the light task to complete
                // g_misc.reset_flag = 0;
                system_reset();
                system_restart();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        pir_int_trigger();
    }
    vTaskDelete(NULL);
}

void misc_open(uint8_t* mode)
{
    // memset(&g_misc, 0, sizeof(g_misc));
    misc_io_cfg(SENSOR_POWER_IO, 0, 0);
    misc_io_set(SENSOR_POWER_IO, 1);
    adc_start();
    button_start();
    pwm_config();
    xTaskCreatePinnedToCore((TaskFunction_t)misc_task, "misc_task", 3 * 1024, NULL, 4, NULL, 1);
    g_misc.isInit = 1;
    debug_cmd_add(g_cmd, sizeof(g_cmd) / sizeof(esp_console_cmd_t));
}

void misc_close(void)
{
    if(g_misc.isInit ){
        button_stop();
        adc_stop();
        misc_io_set(SENSOR_POWER_IO, 0);
    }
}
