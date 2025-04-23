#include "pir.h"
#include "stdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "soc/gpio_struct.h" 
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "debug.h"
#include "misc.h"

#define TAG "-->PIR"

// Read mode sensor configuration values
uint8_t SENS_C = 0x0f; //[7:0] Sensitivity setting, recommended to set greater than 20. If the environment has no interference, it can be set to a minimum value of 10. The smaller the value, the more sensitive, but the easier it is to trigger false alarms. (Effective in interrupt mode, ineffective in read mode)
uint8_t BLIND_C= 0x03; //[3:0] The time to ignore motion detection after the interrupt output switches back to 0, range: 0.5s ~ 8s, interrupt time = register value * 0.5s + 0.5s. (Effective in interrupt mode, ineffective in read mode)
uint8_t PULSE_C= 0x01; //[1:0] Pulse counter, the number of pulses required to be reached within the window time. Range: 1 ~ 4 signed pulses, pulse count = register value + 1. The larger the value, the stronger the anti-interference ability, but the sensitivity is slightly reduced. (Effective in interrupt mode, ineffective in read mode)
uint8_t WINDOW_C= 0x00; //[1:0] Window time, range: 2s~8s, window time = register value * 2s + 2s. (Effective in interrupt mode, ineffective in read mode)
uint8_t MOTION_C =0x01; //[0] Must be 1
uint8_t INT_C= 0x00; // Interrupt source. 0 = motion detection, 1 = raw data from the filter. Read mode must be set to 1.
uint8_t VOLT_C =0x00; //[1:0] Multiplex ADC resources. The input sources selectable for the ADC are as follows: PIR signal BFP output = 0; PIR signal LPF output = 1; power supply voltage = 2; temperature sensor = 3; choose as needed
uint8_t SUPP_C = 0x00; // Set to 0
uint8_t RSV_C = 0x00; // Set to 0

static uint8_t SENS_W, BLIND_W, PULSE_W, WINDOW_W, MOTION_W, INT_W, VOLT_W, SUPP_W, RSV_W;
static uint8_t PIR_OUT, DATA_H, DATA_L, SENS_R, BLIND_R, PULSE_R, WINDOW_R, MOTION_R, INT_R;
static uint8_t VOLT_R, SUPP_R, RSV_R, BUF1;

static uint8_t pir_init_flag = 0;

static void Delay_us(uint32_t us) 
{
    esp_rom_delay_us(us);
}

static void pir_io_cfg(uint8_t io, bool input, bool pulldown, bool pullup)
{
    if(pulldown == 1 && pullup == 1)
        return;

    gpio_config_t config = {
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = BIT64(io),
        .mode = input ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT,
        .pull_down_en = pulldown,
        .pull_up_en = pullup,
    };
    gpio_config(&config);
}

static void pir_serialIn_init(void)
{
    pir_io_cfg(PIR_SERIAL_IO, 0, 0, 0);
}

static void pir_serialIn_set(uint8_t value)
{
    gpio_set_level(PIR_SERIAL_IO, value);
}

static void pir_do_in()
{
    pir_io_cfg(PIR_INTDOUT_IO, 1, 0, 0);
}

static void pir_do_out()
{
    pir_io_cfg(PIR_INTDOUT_IO, 0, 0, 0);
}

/* Need to disable info log, printing log will consume time and cause read timing errors */
static void pir_do_set(uint8_t value)
{
    gpio_set_level(PIR_INTDOUT_IO, value); 
    if(value == 1){
        pir_io_cfg(PIR_INTDOUT_IO, 0, 0, 1);
    }else{
        pir_io_cfg(PIR_INTDOUT_IO, 0, 1, 0);
    }
}

int gpio_direct_read() 
{
    // return (GPIO.in >> PIR_INTDOUT_IO) & 0x1;  // Read GPIO level
    return gpio_get_level(PIR_INTDOUT_IO);
}

//======== Write NBIT subroutine ====================================
static void W_DATA(uint8_t num)
{
    char i;
    for(i=num ;i>0;i--)
    {   
        pir_serialIn_set(0);
        Delay_us(2); // Delay must be accurate, total 2us
        pir_serialIn_set(1);
        Delay_us(2); // Delay must be accurate, total 2us

        if(BUF1 & 0x80){
            pir_serialIn_set(1);
        }else{
            pir_serialIn_set(0);
        }
        Delay_us(100); // Delay must be accurate, total 100us
        BUF1 = BUF1 << 1;
    }
}

//====== Write config to IC ==========================
static void CONFIG_W()
{
    BUF1 = SENS_W;
    W_DATA(8);
    BUF1 = BLIND_W;
    BUF1 = BUF1 << 0x04;
    W_DATA(4);
    BUF1 = PULSE_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = WINDOW_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = MOTION_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);
    BUF1 = INT_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);
    BUF1 = VOLT_W;
    BUF1 = BUF1 << 0x06;
    W_DATA(2);
    BUF1 = SUPP_W;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 1;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    BUF1 = 0;
    BUF1 = BUF1 << 0x07;
    W_DATA(1);

    pir_serialIn_set(0);
    Delay_us(1000);
}

//======= Initialize sensor configuration parameters ==============================
static void CONFIG_INI()
{
    SENS_W = SENS_C;  
    BLIND_W = BLIND_C;
    PULSE_W = PULSE_C;
    WINDOW_W = WINDOW_C;
    MOTION_W = MOTION_C;
    INT_W = INT_C;
    VOLT_W = VOLT_C;
    SUPP_W = SUPP_C;
    RSV_W = RSV_C;
}

//====== Read Nbit ====================
static void RD_NBIT(uint8_t num)
{
    uint8_t i;
    BUF1 = 0x00;
    
    for(i=0;i<num;i++){
        pir_do_set(0);
        Delay_us(2);

        pir_do_set(1);
        Delay_us(2);
        pir_do_in();
        BUF1 = BUF1 << 1;
        if(gpio_direct_read() != 0x00u){
            BUF1 = BUF1 + 1;
        }else{
            BUF1 = BUF1 + 0;
        }
    }
    return;
}

//======= Read end clear subroutine ==================
static void RD_END()
{
    pir_do_out();
    pir_do_set(0);
    Delay_us(200); // Delay must be accurate, total 200us
    pir_do_in();
}

//===== Force DOCI interrupt subroutine ===============
static void F_INT()
{
    pir_do_out();
    pir_do_set(1);
    Delay_us(200); // Delay must be accurate, total 200us
}

//===== DOCI read out =======================
static void RD_DOCI()
{
    F_INT();

    PIR_OUT = 0;
    RD_NBIT(1);
    PIR_OUT = BUF1;

    DATA_H = 0x00;
    RD_NBIT(6);
    DATA_H = BUF1;

    DATA_L = 0x00;
    RD_NBIT(8);
    DATA_L = BUF1;

    SENS_R = 0x00;
    RD_NBIT(8);
    SENS_R = BUF1;

    BLIND_R = 0x00;
    RD_NBIT(4);
    BLIND_R = BUF1;

    PULSE_R = 0x00;
    RD_NBIT(2);
    PULSE_R = BUF1;

    WINDOW_R = 0x00;
    RD_NBIT(2);
    WINDOW_R = BUF1;

    MOTION_R = 0x00;
    RD_NBIT(1);
    MOTION_R = BUF1;

    INT_R = 0x00;
    RD_NBIT(1);
    INT_R = BUF1;

    VOLT_R = 0x00;
    RD_NBIT(2);
    VOLT_R = BUF1;

    SUPP_R = 0x00;
    RD_NBIT(1);
    SUPP_R = BUF1;

    RSV_R = 0x00;
    RD_NBIT(4);
    RSV_R = BUF1;

    RD_END(); // Read end clear subroutine
    // printf("PIR_OUT:%x\r\n", PIR_OUT); printf("DATA_H:%x\r\n", DATA_H); printf("DATA_L:%x\r\n", DATA_L); printf("SENS_R:%x\r\n", SENS_R);
    // printf("BLIND_R:%x\r\n", BLIND_R); printf("PULSE_R:%x\r\n", PULSE_R); printf("WINDOW_R:%x\r\n", WINDOW_R); printf("MOTION_R:%x\r\n", MOTION_R);
    // printf("INT_R:%x\r\n", INT_R); printf("VOLT_R:%x\r\n", VOLT_R); printf("SUPP_R:%x\r\n", SUPP_R); printf("RSV_R:%x\r\n", RSV_R);
}

//==== Configuration IC write and read check ==============
static uint8_t CFG_CHK()
{
    pir_serialIn_init();
    pir_serialIn_set(0);
    pir_do_out();
    pir_do_set(0);
    Delay_us(1000);
    CONFIG_INI(); // Initialize sensor configuration parameters
    CONFIG_W(); // Write config to IC
    Delay_us(25000); // Delay
    RD_DOCI(); // Read data
    // Check if the write is correct
    if(SENS_W != SENS_R)
    { return 1; }
    else if(BLIND_W != BLIND_R)
    { return 2; }
    else if(PULSE_W != PULSE_R)
    { return 3; }
    else if(WINDOW_W != WINDOW_R)
    { return 4; }
    else if(MOTION_W != MOTION_R)
    { return 5; }
    else if(INT_W != INT_R)
    { return 6; }
    else if(VOLT_W != VOLT_R)
    { return 7; }
    else if(SUPP_W != SUPP_R)
    { return 8; }

    return 0;
}

void pir_init(uint8_t is_first)
{
    int err, i;
    if(is_first){
        for(i = 0; i < PIR_INIT_RETRY; i++){
            err = CFG_CHK();
            if(err != 0){
                printf("pir_init err:%d \r\n", err);
            }else{
                break;
            }
        }
    }else{
        pir_do_in();
    }
    pir_do_set(0);
    pir_do_in();
    pir_init_flag = 1;
}

void pir_int_trigger(void)
{
    if(pir_init_flag == 1 && gpio_get_level(PIR_INTDOUT_IO) == 1){
        vTaskDelay(pdMS_TO_TICKS(10));
        pir_do_out();
        pir_do_set(0);
        vTaskDelay(pdMS_TO_TICKS(10));
        pir_do_in();
        printf("------pir int trigger---\r\n");
    }
}

