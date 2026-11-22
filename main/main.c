#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"

#include "ulp_riscv.h"
#include "ulp_riscv_main.h"

#include "ulp_riscv/config.h"
// #include "samples/tzx_basic.h"

extern const uint8_t ulp_riscv_main_bin_start[] asm("_binary_ulp_riscv_main_bin_start");
extern const uint8_t ulp_riscv_main_bin_end[]   asm("_binary_ulp_riscv_main_bin_end");

static TaskHandle_t ulp_ack_task_handle = NULL;

static QueueHandle_t main_pulse_buf;
volatile int main_pulse_buf_len = 0;
volatile uint32_t main_pulse_queued = 0;

/**
 * Task Handler for responding to ULP ACKing after taking a cycles value off the queue
 * - Check to see if we are a buffer-size ahead of the ULP
 * - If less than a buffer-size
 *   - Pop data off Main Queue (FreeRTOS)
 *   - Push onto ULP Queue (Array)
 */
void ulp_ack_task(void *pvParameters) {
    int isr_pin = *(int *)pvParameters;

    uint32_t x = 0;
    uint32_t cycles = 0;
    
    for (;;) {
        // Wait for ISR call
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /*
         * Take the following where we have 6 items in the queue and want to keep a queue of 8:
         * - ulp_pulse_queued = 18
         * - ulp_pulse_played = 12
         * - ulp_queue_len    = 8
         * 
         * That means we should preload:
         * - data[18 % 8] --> ulp_pulse_buf[2]
         * - data[19 % 8] --> ulp_pulse_buf[3]
         */

        // printf("Main(%ld) -> ISR(%ld) -> ULP(%ld)\n", main_pulse_queued, ulp_pulse_queued, ulp_pulse_played);
        for (x = ulp_pulse_queued; x < (ulp_pulse_played + ULP_PULSE_BUF_SIZE); x++) {
            // Take off the ESP queue
            if (xQueueReceive(main_pulse_buf, &cycles, portMAX_DELAY) == pdTRUE) {
                // Put onto the ULP queue
                ulp_pulse_buf[x % ULP_PULSE_BUF_SIZE] = cycles;
                // Increment the absolute queue level
                ulp_pulse_queued++;
            } else {
                printf("Queue issues\n");
            }
        }

        // Tell the ULP the ISR task is free
        rtc_gpio_set_level(isr_pin, 0);
        ulp_isr_pending = 0;        
    }
}

/**
 * ISR Handler for responding to ULP ACKing after taking a value off the queue
 */
void IRAM_ATTR ulp_ack_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify the ULP ACK Task
    vTaskNotifyGiveFromISR(ulp_ack_task_handle, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * ULP Setup: Set GPIO + RTC GPIO, then load the assembler into memory
 */
static void init_ulp_riscv_program(void)
{
    esp_err_t err;

    /*
     * ULP -> Signal Out
     * Used to generate the tone
     */
    gpio_num_t gpio_ulp_out_pin = GPIO_ULP_OUT_PIN;
    assert(rtc_gpio_is_valid_gpio(gpio_ulp_out_pin) && "Not a valid ULP GPIO");
    err = rtc_gpio_init(gpio_ulp_out_pin);
    ESP_ERROR_CHECK(err);
    err = rtc_gpio_set_direction(gpio_ulp_out_pin, RTC_GPIO_MODE_OUTPUT_ONLY);
    ESP_ERROR_CHECK(err);

    /*
     * ULP -> C ISR-based ACK
     * Mechanism to notify the  when a value is read
     */
    gpio_num_t gpio_ulp_ack_pin = GPIO_ULP_ACK_PIN;
    assert(rtc_gpio_is_valid_gpio(gpio_ulp_ack_pin) && "Not a valid ULP GPIO");  
    err = rtc_gpio_init(gpio_ulp_ack_pin);
    ESP_ERROR_CHECK(err);
    err = rtc_gpio_set_direction(gpio_ulp_ack_pin, RTC_GPIO_MODE_INPUT_OUTPUT);
    ESP_ERROR_CHECK(err);

    // Ensure the GPIO is pulled down
    rtc_gpio_set_level(gpio_ulp_ack_pin, 0);
    
    /*
     * ULP Program Load
     */
    err = ulp_riscv_load_binary(ulp_riscv_main_bin_start,
            (ulp_riscv_main_bin_end - ulp_riscv_main_bin_start));
    ESP_ERROR_CHECK(err);
}

/**
 * Send pulse data onto the FreeRTOS queue
 */
void send_pulse(uint32_t t_states) {
    bool queue_blocked = 1;

    // Downsample t_states to ULP cycles
    uint32_t cycles = (t_states - ULP_TICKS_OFFSET) * TZX_TICKS_TO_ULP_LOOPS;

    while (queue_blocked) {
        // Sent to the ISR-invoked Task - needs to be fast - no printf()
        if(xQueueSend(main_pulse_buf, &cycles, 10) == pdPASS) {
            queue_blocked = 0;
            main_pulse_queued++;
            ulp_state = ULP_STATE_RUNNING;
        }
    }
}

/**
 * Send some sample data via send_pulse()
 */
void send_sample_data(void) {
    uint32_t value;

    uint8_t block_header[19] = {
        0x00, 0x00, 0x48, 0x45, 0x4c, 0x4c, 0x4f, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x0d, 0x00, 0x00, 0x80,
        0x0d, 0x00, 0xe2
    };

    uint8_t block_data[15] = {
        0xff, 0x00, 0x0a, 0x09, 0x00, 0xf5, 0x22, 0x48, 
	    0x45, 0x4c, 0x4c, 0x4f, 0x22, 0x0d, 0x46 
    };

    /*
     * Standard Block Header
     */

    // Pilot
    for (int x = 0; x < 8063; x++) {
        send_pulse(2168);
    }
    // Sync
    send_pulse(667);
    send_pulse(735);

    // Data
    for (int x = 0; x < sizeof(block_header); x++) {
        for (int y = 7; y >= 0; y--) {
            // It will either be 855 (0x0) or 1710 (0x1)
            value = 855 + (((block_header[x] >> y) & 0x1) * 855);
            send_pulse(value);
            send_pulse(value);
        } 
    }

    // Pause
    send_pulse(3500000);

    /*
     * Standard Block Data
     */
    // Pilot
    for (int x = 0; x < 3223; x++) {
        send_pulse(2168);
    }
    // Sync
    send_pulse(667);
    send_pulse(735);

    // Data
    for (int x = 0; x < sizeof(block_data); x++) {
        for (int y = 7; y >= 0; y--) {
            // It will either be 855 (0x0) or 1710 (0x1)
            value = 855 + (((block_data[x] >> y) & 0x1) * 855);
            send_pulse(value);
            send_pulse(value);
        } 
    }
    // Pause
    send_pulse(3500000);
}

void app_main(void)
{
    esp_err_t err;
    int gpio_ulp_ack_pin = GPIO_ULP_ACK_PIN;

    // Prime the ULP Variables
    ulp_state = ULP_STATE_PAUSED;
    ulp_pulse_played = 0;
    ulp_pulse_queued = 0;

    printf("Start\n"); 

    vTaskDelay(pdMS_TO_TICKS(1000));

    init_ulp_riscv_program();

    /* Stop RISC-V ULP */
    ulp_riscv_timer_stop();

    // Stop current execution
    ulp_riscv_halt();

    /*
     * ISR and Task for ULP ACK
     */
    // queue for Main->ISR
    main_pulse_buf = xQueueCreate(MAIN_PULSE_BUF_SIZE, sizeof(uint32_t));

    xTaskCreate(ulp_ack_task, "Value Read Task", 4096, &gpio_ulp_ack_pin, 10, &ulp_ack_task_handle);

    ESP_ERROR_CHECK(gpio_set_direction(GPIO_ULP_ACK_PIN, GPIO_MODE_INPUT_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_intr_type(GPIO_ULP_ACK_PIN, GPIO_INTR_POSEDGE));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_ULP_ACK_PIN, ulp_ack_isr_handler, NULL));

    /*
     * Queue Processing
     */
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Run the ULP RISC-V Program
    err = ulp_riscv_run();
    if (err != ESP_OK) {
        printf("Error loading ULP RISC-V Program\n");
    }

    // Enqueue data in a blocking fashion
    send_sample_data();

    // Monitor the queues and data played
    while (true) {
        printf("Main(%ld) -> ISR(%ld) -> ULP(%ld)\n", main_pulse_queued, ulp_pulse_queued, ulp_pulse_played); 
        if (ulp_state == ULP_STATE_COMPLETE) {
            ulp_riscv_timer_stop();
            ulp_riscv_halt();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}