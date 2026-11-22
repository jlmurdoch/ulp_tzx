// GPIO
#define GPIO_ULP_OUT_PIN 4      // ULP->Audio Out
#define GPIO_ULP_ACK_PIN 14     // ULP->Main Ack

// Queues
#define MAIN_PULSE_BUF_SIZE 64
#define ULP_PULSE_BUF_SIZE 8

// Time Conversion
#define ULP_TICKS_OFFSET 31 
#define TZX_TICKS_TO_ULP_LOOPS 0.421 // with offset

// Old values
// #define TZX_TICKS_TO_ULP_LOOPS 0.251 // with offset
// #define TZX_TICKS_TO_ULP_LOOPS 0.248 // without offset

#define ULP_STATE_RUNNING 0
#define ULP_STATE_PAUSED 1 
#define ULP_STATE_COMPLETE 2