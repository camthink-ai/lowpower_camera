#ifndef __MISC_H__
#define __MISC_H__

/* Header for miscellaneous hardware control functions */
#include "esp_console.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Status LED control pins and states */
#define STATUS_LED_IO       (42)
#define STATUS_LED_ON       (0)
#define STATUS_LED_OFF      (1)

/* Flash LED control pins and states */ 
#define FLASH_LED_IO        (47)
#define FLASH_LED_ON        (1)
#define FLASH_LED_OFF       (0)

/* Light sensor control pins and parameters */
#define LIGHT_DET_IO        (1)
#define LIGHT_POWER_IO      (3)//(3)
#define LIGHT_POWER_ON      (1)
#define LIGHT_POWER_OFF     (0)
#define LIGHT_MIN_SENS      (0)    /* Minimum light sensor value */
#define LIGHT_MAX_SENS      (2500) /* Maximum light sensor value */

/* Battery monitoring pins and parameters */
#define BATTERY_DET_IO      (14)
#define BATTERY_POWER_IO    (3)//(46)
#define BATTERY_POWER_ON    (1)
#define BATTERY_POWER_OFF   (0)
#define BATTERY_MIN_VOLTAGE (1800)  /* Minimum battery voltage in mV */
#define BATTERY_MAX_VOLTAGE (3000)  /* Maximum battery voltage in mV */

/* TF (TransFlash) card power control */
#define TF_POWER_IO         (42)
#define TF_POWER_ON         (1)
#define TF_POWER_OFF        (0)

/* Alarm input pin and active state */
#define ALARM_IN_IO         (2)
#define ALARM_IN_ACTIVE     (0)

/* Button input pin and active state */
#define BUTTON_IO           (21)
#define BUTTON_ACTIVE       (0)
#define BUTTON_RESET_TIME   (10 * 1000 * 1000)//

/* Type-C connector detection */
#define TYPEC_DET_IO        (19)
#define TYPEC_INSERT        (1)  /* Type-C inserted */
#define TYPEC_REMOVE        (0)  /* Type-C removed */

/* Camera power control */
#define CAMERA_POWER_IO (3)
#define CAMERA_POWER_ON  (1)
#define CAMERA_POWER_OFF (0)

/* Sensor power and PWM control */
#define SENSOR_POWER_IO (3)
#define PWM_IO          (47)      /* PWM output pin */
#define PWM_FREQ        (20000)   /* PWM frequency in Hz */
#define PWM_MIN_DUTY    (5)       /* Minimum PWM duty cycle */

void misc_set_btnWakeFlag(void);
/* Initialize miscellaneous hardware */
void misc_open(uint8_t* mode);
/* Shutdown miscellaneous hardware */
void misc_close(void);
/* Set GPIO pin state */
void misc_io_set(uint8_t io, bool value);
/* Get GPIO pin state */
bool misc_io_get(uint8_t io);
/* Configure GPIO pin direction and pull */
void misc_io_cfg(uint8_t io, bool input, bool pulldown);
/* Turn on flash LED */
void misc_flash_led_open();
/* Turn off flash LED */
void misc_flash_led_close();
/* Enable/disable status LED */
void misc_led_able(uint8_t is_able);
/* Blink status LED */
void misc_led_blink(uint8_t blink_cnt, uint16_t blink_interval);
/* Get light sensor reading as percentage */
uint8_t misc_get_light_value_rate();
/* Get battery voltage as percentage */
uint8_t misc_get_battery_voltage_rate();
/* Read raw battery voltage */
uint8_t misc_read_battery_voltage();
/* Set flash LED PWM duty cycle */
void misc_set_flash_duty(int duty);
#ifdef __cplusplus
}
#endif


#endif /* __MISC_H__ */
