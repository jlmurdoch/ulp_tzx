#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "ulp_riscv_register_ops.h" // RISCV_REG_CONV
#include "config.h"

/*
 * All the GPIO pieces are here:
 * ./soc/esp32s3/register/soc/rtc_io_reg.h
 *
 * #define RTC_GPIO_OUT_W1TS_REG          (DR_REG_RTCIO_BASE + 0x4)
 * #define RTC_GPIO_OUT_DATA_W1TS_S  10
 * #define RTC_GPIO_OUT_W1TC_REG          (DR_REG_RTCIO_BASE + 0x8)
 * #define RTC_GPIO_OUT_DATA_W1TC_S  10
 *
 * #define RTC_IO_TOUCH_PAD0_REG          (DR_REG_RTCIO_BASE + 0x84)
 * #define RTC_IO_TOUCH_PAD14_REG          (DR_REG_RTCIO_BASE + 0xBC)
 * #define RTC_IO_XTAL_32P_PAD_REG          (DR_REG_RTCIO_BASE + 0xC0)
 * #define RTC_IO_XTAL_32N_PAD_REG          (DR_REG_RTCIO_BASE + 0xC4)
 * #define RTC_IO_PAD_DAC1_REG          (DR_REG_RTCIO_BASE + 0xC8)
 * #define RTC_IO_PAD_DAC2_REG          (DR_REG_RTCIO_BASE + 0xCC)
 * #define RTC_IO_RTC_PAD19_REG          (DR_REG_RTCIO_BASE + 0xD0)
 * #define RTC_IO_RTC_PAD20_REG          (DR_REG_RTCIO_BASE + 0xD4)
 * #define RTC_IO_RTC_PAD21_REG          (DR_REG_RTCIO_BASE + 0xD8)
 *
 * #define RTC_IO_TOUCH_PAD0_MUX_SEL_S  19
 * #define RTC_IO_TOUCH_PAD14_MUX_SEL_S  19
 * #define RTC_IO_X32P_MUX_SEL_S  19
 * #define RTC_IO_X32N_MUX_SEL_S  19
 * #define RTC_IO_PDAC1_MUX_SEL_S  19
 * #define RTC_IO_PDAC2_MUX_SEL_S  19
 * #define RTC_IO_PAD19_MUX_SEL_S  19
 * #define RTC_IO_PAD20_MUX_SEL_S  19
 * #define RTC_IO_PAD21_MUX_SEL_S  19
 */

// RISC-V macro to set bits
.macro reg_set_bit addr, value
    li t1, \value
    lw t2, 0(\addr)
    or t2, t2, t1
    sw t2, 0(\addr)
.endm

.macro reg_clear_bit addr, value
    li t1, \value
    not t1, t1
    lw t2, 0(\addr)
    and t2, t2, t1
    sw t2, 0(\addr)
.endm

// RISC-V macro to get the cycles value and increment
.macro get_cycles output
    // get latest played and queued - if equal, quit
    lw          t2, 0(t6) // played
    lw          t1, 0(t5) // queued
    beq         t2, t1  , .L_abort

    // Calculate array position: (pulses_played % 8) << 2
    andi        t0, t2  , 0x07
    slli        t1, t0  , 2

    // Use pulses_played to reach the queue
    mv          t0, t4
    add         t0, t0  , t1
    lw          \output, 0(t0)
    
    // Increment pulses_played (t6)
    addi        t2, t2  , 1
    sw          t2, 0(t6)

    // Trigger C ISR for another cycles value
    li          t0, RISCV_REG_CONV(RTC_GPIO_OUT_W1TS_REG)
    reg_set_bit t0, (1 << (RTC_GPIO_OUT_DATA_W1TS_S + GPIO_ULP_ACK_PIN))
.endm

/*
 * Global vars
 */
.section .data

// Pulse Buffer
.global pulse_buf  // t4
.type pulse_buf, @object
pulse_buf:
    .rept ULP_PULSE_BUF_SIZE
    .word 0
    .endr
// ESP-IDF uses .size to define array size in the header
pulse_buf_end:
.size pulse_buf, pulse_buf_end - pulse_buf // .size is uint8_t, not uint32_t

// Pulses Queued Counter
.global pulse_queued  // t5
pulse_queued:
    .word 0

// Pulses Played Counter
.global pulse_played  // t6
pulse_played:
    .word 0

// ISR ACK Flag
.global isr_pending
isr_pending:
    .byte 0

.global state
state:
    .byte 0

/*
 * Main Code
 */
.section .text

.global main
main:
    // Map variable addresses to upper registers
    la          t4, pulse_buf
    la          t5, pulse_queued
    la          t6, pulse_played

    // Setup signal output, mapping output GPIO to IOMUX
    li          t0, RISCV_REG_CONV(RTC_IO_TOUCH_PAD4_REG)
    reg_set_bit t0, (1 << RTC_IO_TOUCH_PAD4_MUX_SEL_S)

    // Setup ack, mapping output GPIO to IOMUX
    li          t0, RISCV_REG_CONV(RTC_IO_TOUCH_PAD14_REG)
    reg_set_bit t0, (1 << RTC_IO_TOUCH_PAD14_MUX_SEL_S)

.L_pause:
    la          t0, state
    lb          t1, 0(t0)
    bnez        t1, .L_pause

/*
 * Prefill the queue 
 */
.L_prefill_loop:
    // If ISR is pending, keep waiting
    la          t0, isr_pending
    lb          t1, 0(t0)
    bnez        t1, .L_prefill_loop        

    // Is the queue fully populated?
    li          t0, ULP_PULSE_BUF_SIZE
    lw          t1, 0(t5) // pulse queued
    beq         t1, t0  , .L_hi_begin 
    
    // Tell the ISR we want a value and set the isr_pending flag
    li          t0, RISCV_REG_CONV(RTC_GPIO_OUT_W1TS_REG)
    reg_set_bit t0, (1 << (RTC_GPIO_OUT_DATA_W1TS_S + GPIO_ULP_ACK_PIN))

    // Set pending flag and wait for ISR to reset it
    la          t0, isr_pending
    li          t1, 1
    sb          t1, 0(t0)
    j           .L_prefill_loop     

/*
 * HIGH Pulse Sequence
 */
.L_hi_begin:
    // Get the latest value
    get_cycles  t3

    // If value is zero, jump to next label
    beqz        t3, .L_lo_begin

    // Set GPIO HIGH
    li          t0, RISCV_REG_CONV(RTC_GPIO_OUT_W1TS_REG)
    reg_set_bit t0, (1 << (RTC_GPIO_OUT_DATA_W1TS_S + GPIO_ULP_OUT_PIN))

.L_hi_loop:
    addi        t3, t3  , -1
    bnez        t3, .L_hi_loop

    nop         // Fall-thru / j .L_lo_begin

/*
 * LOW Pulse Sequence
 */
.L_lo_begin:
    // Get the latest value
    get_cycles  t3

    // If value is zero, jump to next label
    beqz        t3, .L_hi_begin

    // Set GPIO LOW
    li          t0, RISCV_REG_CONV(RTC_GPIO_OUT_W1TC_REG)
    reg_set_bit t0, (1 << (RTC_GPIO_OUT_DATA_W1TC_S + GPIO_ULP_OUT_PIN))

.L_lo_loop:
    addi        t3, t3  , -1
    bnez        t3, .L_lo_loop

    j           .L_hi_begin

.L_abort:
    la          t0, state
    li          t1, ULP_STATE_COMPLETE
    sb          t1, 0(t0)
