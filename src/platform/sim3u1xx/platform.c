// Platform-dependent functions

#include "platform.h"
#include "type.h"
#include "devman.h"
#include "genstd.h"
#include "stacks.h"
#include <reent.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "utils.h"
#include "common.h"
#include "elua_adc.h"
#include "platform_conf.h"
#include "lrotable.h"
#include "buf.h"
#include "sermux.h"

// Platform includes
#include "sim3u1xx.h"
#include "sim3u1xx_Types.h"

// FreakUSB
#include "cdc.h"
#include "freakusb.h"
#include "hw.h"

// Generated code includes
#include <SI32_PBCFG_A_Type.h>
#include <SI32_PBSTD_A_Type.h>
#include <si32_device.h>
#include <SI32_RTC_A_Type.h>
#include <SI32_RSTSRC_A_Type.h>
#include <SI32_VREG_A_Type.h>
#include <SI32_PMU_A_Type.h>
#include "myPB.h"
#include <gVMON0.h>
#include <gLDO0.h>
#include <gVREG0.h>
#include <gRSTSRC0.h>

// ****************************************************************************
// Platform initialization

#define TRICK_TO_REBOOT_WITHOUT_DFU_MODE 628187
#define PIN_CHECK_INTERVAL 10
wake_type wake_reason = WAKE_UNKNOWN;

int rram_reg[8] __attribute__((section(".sret")));
int rtc_remaining = 0;

void sim3_pmu_reboot( void );

extern int load_lua_function (char *func);

// forward dcls
static void pios_init();
static void clk_init();
static void rtc_init();

void hard_fault_handler_c(unsigned int * hardfault_args)
{
  unsigned int stacked_r0;
  unsigned int stacked_r1;
  unsigned int stacked_r2;
  unsigned int stacked_r3;
  unsigned int stacked_r12;
  unsigned int stacked_lr;
  unsigned int stacked_pc;
  unsigned int stacked_psr;

  stacked_r0 = ((unsigned long) hardfault_args[0]);
  stacked_r1 = ((unsigned long) hardfault_args[1]);
  stacked_r2 = ((unsigned long) hardfault_args[2]);
  stacked_r3 = ((unsigned long) hardfault_args[3]);

  stacked_r12 = ((unsigned long) hardfault_args[4]);
  stacked_lr = ((unsigned long) hardfault_args[5]);
  stacked_pc = ((unsigned long) hardfault_args[6]);
  stacked_psr = ((unsigned long) hardfault_args[7]);

  printf ("[Hard fault handler]\n");
  printf ("R0 = %x\n", stacked_r0);
  printf ("R1 = %x\n", stacked_r1);
  printf ("R2 = %x\n", stacked_r2);
  printf ("R3 = %x\n", stacked_r3);
  printf ("R12 = %x\n", stacked_r12);
  printf ("LR = %x\n", stacked_lr);
  printf ("PC = %x\n", stacked_pc);
  printf ("PSR = %x\n", stacked_psr);
  printf ("BFAR = %x\n", (*((volatile unsigned  *)(0xE000ED38))));
  printf ("CFSR = %x\n", (*((volatile unsigned  *)(0xE000ED28))));
  printf ("HFSR = %x\n", (*((volatile unsigned  *)(0xE000ED2C))));
  printf ("DFSR = %x\n", (*((volatile unsigned  *)(0xE000ED30))));
  printf ("AFSR = %x\n", (*((volatile unsigned  *)(0xE000ED3C))));
  printf ("\n");
  printf ("\n");
  printf ("\n");

  sim3_pmu_reboot();
  while (1) { ;; }
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  asm(
  "tst lr, #4\n\t"
  "ite eq\n\t"
  "mrseq r0, msp\n\t"
  "mrsne r0, psp\n\t"
  "b hard_fault_handler_c"
  );
}

#if defined( BUILD_USB_CDC )
unsigned console_uart_id = CDC_UART_ID;
#endif


void sim3_pmu_pm9( unsigned seconds );

// SiM3 SystemInit calls this function, disable watchdog timer
void mySystemInit(void)
{
  SI32_WDTIMER_A_stop_counter(SI32_WDTIMER_0);

  // enable APB clock to the Port Bank module
  SI32_CLKCTRL_A_enable_apb_to_modules_0 (SI32_CLKCTRL_0, SI32_CLKCTRL_A_APBCLKG0_PB0CEN_MASK);
  // make the SWO pin (PB1.3) push-pull to enable SWV printf
  //SI32_PBSTD_A_set_pins_push_pull_output (SI32_PBSTD_1, (1<<3));

}

#if defined( ELUA_BOARD_GSBRD )
int external_power()
{
  //check USB DC 3.8 or HVDC 3.7
  if( ( SI32_PBSTD_A_read_pins( SI32_PBSTD_3 ) & ( 1 << 7 ) ) ||
      ( SI32_PBSTD_A_read_pins( SI32_PBSTD_3 ) & ( 1 << 8 ) ) )
    return 1;
  else
    return 0;
}
int external_buttons()
{ return 0;
  //check inputs 1 and 2
  if( ( SI32_PBSTD_A_read_pins( SI32_PBSTD_3 ) & ( 1 << 6 ) ) ||
      ( SI32_PBSTD_A_read_pins( SI32_PBSTD_0 ) & ( 1 << 1 ) ) )
    return 1;
  else
    return 0;
}
#endif
static int pmu_wake_status = -1;
static int pmu_status = -1;
static int reset_status = -1;
void reset_parameters()
{
  rram_reg[0] = 0;
  rram_reg[1] = 0;
  rram_reg[2] = 0;
  rram_reg[3] = 0;
}

int platform_init()
{
  SystemInit();

  // Configure the NVIC Preemption Priority Bits:
  // two (2) bits of preemption priority, six (6) bits of sub-priority.
  // Since the Number of Bits used for Priority Levels is five (5), so the
  // actual bit number of sub-priority is three (3)
  //NVIC_SetPriorityGrouping(0x05);

  // Setup peripherals
  // platform_setup_timers();
  SI32_VREG_A_enable_band_gap(SI32_VREG_0);
  SI32_VREG_A_exit_suspend_mode(SI32_VREG_0);

  // Peripheral Clocking setup
  clk_init();

  //Set flash read speed to slow
  //SI32_FLASHCTRL_A_select_flash_read_time_slow(SI32_FLASHCTRL_0);

  SI32_PMU_A_clear_pmu_level_shifter_hold(SI32_PMU_0);
  pmu_status = SI32_PMU_0->STATUS.U32;
  pmu_wake_status = SI32_PMU_0->WAKESTATUS.U32;
  reset_status = SI32_RSTSRC_0->RESETFLAG.U32;
  SI32_PMU_A_clear_pin_level_shifter_hold(SI32_PMU_0);
  SI32_PMU_A_clear_wakeup_flags(SI32_PMU_0);
  SI32_PMU_A_clear_por_flag(SI32_PMU_0);

  // GPIO setup
  pios_init();

  // System timer setup
  cmn_systimer_set_base_freq( cmsis_get_cpu_frequency() );
  cmn_systimer_set_interrupt_freq( SYSTICKHZ );

  // Enable SysTick
  SysTick_Config( cmsis_get_cpu_frequency() / SYSTICKHZ );

  NVIC_SetPriority(SysTick_IRQn, (1 << __NVIC_PRIO_BITS) - 1);

  // RTC Configuration
  SI32_RTC_A_start_timer_capture(SI32_RTC_0);
  while(SI32_RTC_A_is_timer_capture_in_progress(SI32_RTC_0));

  rtc_remaining = (SI32_RTC_A_read_alarm0(SI32_RTC_0)-SI32_RTC_A_read_setcap(SI32_RTC_0))/16384;
  rtc_init();

#if defined( ELUA_BOARD_GSBRD )
    platform_pio_op( 0, ( ( u32 ) 1 << 10 ), PLATFORM_IO_PIN_DIR_OUTPUT );
    platform_pio_op( 0, ( ( u32 ) 1 << 10 ), PLATFORM_IO_PIN_SET );
    platform_pio_op( 3, ( ( u32 ) 1 << 11 ), PLATFORM_IO_PIN_DIR_OUTPUT );
    platform_pio_op( 3, ( ( u32 ) 1 << 11 ), PLATFORM_IO_PIN_SET );
#endif

#if defined( BUILD_USB_CDC )
  //SI32_PBSTD_A_write_pins_low( SI32_PBSTD_3, ( 1 << 8 ) );

  if( ( SI32_PBSTD_A_read_pins( SI32_PBSTD_3 ) & ( 1 << 8 ) ) == 0 )
    console_uart_id = CON_UART_ID_FALLBACK;

  usb_init();
  hw_init();

  // init the class driver here
  cdc_init();

  // register the rx handler function with the cdc
  //cdc_reg_rx_handler(NULL);
#endif

  // Common platform initialization code
  cmn_platform_init();

#if defined( ELUA_BOARD_GSBRD )

  //Determine if we had a power failure, voltage dropout, or reset button pressed
  //The pre-generated code for SI32_RSTSRC_A_get_last_reset_source is incorrect and does
  //not take into account PORRF. Also, these registers MUST be read in order PORRF, VMONRF, PINRF
  //as the remainder are invalid if the previous one is set. See table 6.2.
  if((pmu_status & SI32_PMU_A_STATUS_PM9EF_MASK) == SI32_PMU_A_STATUS_PM9EF_SET_U32)
  {
    //Check for reset pin while in PM9
    if((pmu_wake_status & SI32_PMU_A_WAKESTATUS_RSTWF_MASK) == SI32_PMU_A_WAKESTATUS_RSTWF_SET_VALUE)
    {
      reset_parameters();
      wake_reason = WAKE_RESETPIN;
    }
    else if((pmu_status & SI32_PMU_A_STATUS_PWAKEF_MASK) == (0 << SI32_PMU_A_STATUS_PWAKEF_SHIFT)) //Check for pin wake NOTE: SiLabs headers are wrong, this bit is backwards per the manual...
    {
      //Wakeup from WAKE pins, just reset reg[0] so we stay awake
      //rram_reg[0] = 0;
      wake_reason = WAKE_WAKEPIN;

      //Put the remaining sleep time back into rram_reg[0]
      rram_reg[0] = rtc_remaining;
    }
    else
    {
      if(external_buttons() || external_power())
      {
        //Continue on as normal. The timer will count down rram_reg and execute
        //the appropriate script when it reaches zero
        wake_reason = WAKE_POWERCONNECTED;
      }
      else
      {
        if( rram_reg[0] > 0 )  //Go back to sleep if we woke from a PMU wakeup
        {
          sim3_pmu_pm9( rram_reg[0] );
        }
        wake_reason = WAKE_RTC;
      }
    }
  }
  else if (((reset_status & SI32_RSTSRC_A_RESETFLAG_PORRF_MASK) == SI32_RSTSRC_A_RESETFLAG_PORRF_SET_U32)
    ||  ((reset_status & SI32_RSTSRC_A_RESETFLAG_VMONRF_MASK) == SI32_RSTSRC_A_RESETFLAG_VMONRF_SET_U32)
    ||  ((reset_status & SI32_RSTSRC_A_RESETFLAG_PINRF_MASK) == SI32_RSTSRC_A_RESETFLAG_PINRF_SET_U32))
  {
    //Fresh powerup! Reset our retained ram registers
    reset_parameters();
    wake_reason = WAKE_POWERUP;
  }

#endif
#if defined( INT_SYSINIT )
    cmn_int_handler( INT_SYSINIT, 0 );
#endif

  return PLATFORM_OK;
}

void clk_init( void )
{

  //SI32_CLKCTRL_A_select_ahb_divider(SI32_CLKCTRL_0, SI32_CLKCTRL_A_CONTROL_AHBDIV_DIV8_VALUE);

  // Set system clock to AHB divider frequency
  //SystemCoreClock = 2500000;
#if defined( ELUA_BOARD_GSBRD )
  SI32_CLKCTRL_A_enable_apb_to_modules_0(SI32_CLKCTRL_0,
                                         SI32_CLKCTRL_A_APBCLKG0_PB0 |
                                         SI32_CLKCTRL_A_APBCLKG0_USART0 |
                                         SI32_CLKCTRL_A_APBCLKG0_USART1 |
                                         SI32_CLKCTRL_A_APBCLKG0_UART0 |
                                         SI32_CLKCTRL_A_APBCLKG0_UART1 |
                                         SI32_CLKCTRL_A_APBCLKG0_SPI0 |
                                         SI32_CLKCTRL_A_APBCLKG0_I2C0 |
                                         SI32_CLKCTRL_A_APBCLKG0_SARADC0 |
                                         SI32_CLKCTRL_A_APBCLKG0_AES0 |
                                         SI32_CLKCTRL_A_APBCLKG0_CRC0 |
                                         SI32_CLKCTRL_A_APBCLKG0_LPTIMER0 |
                                         SI32_CLKCTRL_A_APBCLKG0_USB0 |
                                         SI32_CLKCTRL_A_APBCLKG0_FLASHCTRL0 |
                                         SI32_CLKCTRL_A_APBCLKG0_CMP0
                                         );
  SI32_CLKCTRL_A_enable_apb_to_modules_1(SI32_CLKCTRL_0,
                                         SI32_CLKCTRL_A_APBCLKG1_MISC1 |
                                         SI32_CLKCTRL_A_APBCLKG1_MISC0);
  //SI32_CLKCTRL_A_enable_ahb_to_emif(SI32_CLKCTRL_0);

#else
  SI32_CLKCTRL_A_enable_apb_to_modules_0(SI32_CLKCTRL_0,
                                         SI32_CLKCTRL_A_APBCLKG0_PB0 |
                                         SI32_CLKCTRL_A_APBCLKG0_USART0 |
                                         SI32_CLKCTRL_A_APBCLKG0_USART1 |
                                         SI32_CLKCTRL_A_APBCLKG0_UART0 |
                                         SI32_CLKCTRL_A_APBCLKG0_UART1 |
                                         SI32_CLKCTRL_A_APBCLKG0_CMP0);
  SI32_CLKCTRL_A_enable_apb_to_modules_1(SI32_CLKCTRL_0,
                                         SI32_CLKCTRL_A_APBCLKG1_MISC1 |
                                         SI32_CLKCTRL_A_APBCLKG1_MISC0);
#endif
}
void RTC0ALRM_IRQHandler()
{
  if (SI32_RTC_A_is_alarm0_interrupt_pending(SI32_RTC_0))
  {
    SI32_RTC_A_clear_alarm0_interrupt(SI32_RTC_0);
    //printf("Alarm\n");
  }
}

void rtc_init( void )
{
  SI32_RTC_A_enable_high_speed_mode(SI32_RTC_0);
  // Low Frequency Oscillator Mode
  SI32_RTC_A_enable_low_frequency_oscillator(SI32_RTC_0);
  SI32_RTC_A_set_clock_source_lfo(SI32_RTC_0);
  SI32_RTC_A_disable_crystal_oscillator(SI32_RTC_0);

  SI32_RTC_A_enable_module(SI32_RTC_0);

  SI32_RTC_A_start_timer(SI32_RTC_0);
  SI32_RTC_A_enable_alarm0_auto_reset(SI32_RTC_0);
  SI32_RTC_A_write_alarm0(SI32_RTC_0, 0xF000);
  SI32_RTC_A_clear_alarm0_interrupt(SI32_RTC_0);

  SI32_RTC_A_enable_alarm0_interrupt(SI32_RTC_0);

  NVIC_ClearPendingIRQ(RTC0ALRM_IRQn);
  NVIC_EnableIRQ(RTC0ALRM_IRQn);
}

extern u32 SystemCoreClock;
u32 cmsis_get_cpu_frequency()
{
  return SystemCoreClock;
}
static u8 firstSecond = 1;
static u8 tickSeconds = 0;
void SecondsTick_Handler()
{
  //Check if we are supposed to be sleeping
  if(rram_reg[0] > 0)
  {
    rram_reg[0]--;
    if(rram_reg[0] == 0)
    {
      //Our timer has expired and we are still powered, start TX script
      //Do a software reboot UNTIL we get the memory leaks sorted out...
      //sim3_pmu_reboot();
      sim3_pmu_pm9(TRICK_TO_REBOOT_WITHOUT_DFU_MODE);
      //Normally we would run the startup script, but fix memory leaks first...
      printf("startup %i\n", load_lua_function("autorun"));
    }
    if(external_power() == 0)
    {
      printf("no power %i\n", rram_reg[0]);
      //sim3_pmu_pm9( rram_reg[0] );
    }
    else
      printf("powered %i\n", rram_reg[0]);
  }
  if(firstSecond)
  {
    SI32_RTC_A_start_timer_capture(SI32_RTC_0);
    while(SI32_RTC_A_is_timer_capture_in_progress(SI32_RTC_0));

    printf("PWS 0x%x PS 0x%x RS 0x%x - %i rtc %i\n",
         pmu_wake_status,
         pmu_status,
         reset_status,
         wake_reason,
         rtc_remaining  );
    //firstSecond = 0;
  }
}

// SysTick interrupt handler
void SysTick_Handler()
{
  // Handle virtual timers
  cmn_virtual_timer_cb();

  // Handle system timer call
  cmn_systimer_periodic();

#if defined( BUILD_USB_CDC )
  if( ( SI32_PBSTD_A_read_pins( SI32_PBSTD_3 ) & ( 1 << 8 ) ) == 0 )
  {
    if( console_uart_id == CDC_UART_ID )
    {
      if( CON_UART_ID_FALLBACK < SERMUX_SERVICE_ID_FIRST && ( CON_UART_ID_FALLBACK != CDC_UART_ID ) )
      {
        // Setup console UART
        platform_uart_setup( CON_UART_ID_FALLBACK, CON_UART_SPEED, 8, PLATFORM_UART_PARITY_NONE, PLATFORM_UART_STOPBITS_1 );
        platform_uart_set_flow_control( CON_UART_ID_FALLBACK, PLATFORM_UART_FLOW_NONE );
        platform_uart_set_buffer( CON_UART_ID_FALLBACK, CON_BUF_SIZE );
      }
    }
    console_uart_id = CON_UART_ID_FALLBACK;
  }
  else
    console_uart_id = CDC_UART_ID;

  if( console_uart_id == CDC_UART_ID )
    usb_poll();
#endif

#if defined( INT_SYSTICK )
    cmn_int_handler( INT_SYSTICK, 0 );
#endif

  tickSeconds++;
  if(tickSeconds == SYSTICKHZ)
  {
    SecondsTick_Handler();
    tickSeconds = 0;
  }
}

// ****************************************************************************
// PIO section

static SI32_PBSTD_A_Type* const port_std[] = { SI32_PBSTD_0, SI32_PBSTD_1, SI32_PBSTD_2, SI32_PBSTD_3 };

void pios_init( void )
{
#if defined( ELUA_BOARD_GSBRD )
  // SI32_PBCFG_A_unlock_ports(SI32_PBCFG_0);

  // // PB0 Setup
  // SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_0, 0x0603);
  // SI32_PBSTD_A_set_pins_push_pull_output(SI32_PBSTD_0, 0x1114);
  // SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_0, 0x37FB);

  // // PB1 Setup
  // SI32_PBSTD_A_set_pins_push_pull_output(SI32_PBSTD_1, 0x00A1);
  // SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_1, 0xFC1C);

  // // Enable Crossbar0 signals & set properties
  // SI32_PBCFG_A_enable_xbar0h_peripherals(SI32_PBCFG_0,
  //                                        SI32_PBCFG_A_XBAR0H_UART0EN |
  //                                        SI32_PBCFG_A_XBAR0H_UART1EN);
  // SI32_PBCFG_A_enable_xbar0l_peripherals(SI32_PBCFG_0,
  //                                        SI32_PBCFG_A_XBAR0L_USART0EN |
  //                                        SI32_PBCFG_A_XBAR0L_I2C0EN);
  // SI32_PBCFG_A_enable_crossbar_0(SI32_PBCFG_0);

  // // PB2 Setup
  // SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_2, 0x7FFF);

  // // PB3 Setup
  // SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_3, 0x00FF);

  // // Enable Crossbar1 signals & set properties
  // SI32_PBCFG_A_enable_crossbar_1(SI32_PBCFG_0);


  SI32_PBCFG_A_unlock_ports(SI32_PBCFG_0);

  // PB0 Setup
  SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_0, 0x0603);
  SI32_PBSTD_A_set_pins_push_pull_output(SI32_PBSTD_0, 0x1114);
  SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_0, 0x0603);

  // PB1 Setup
  SI32_PBSTD_A_set_pins_push_pull_output(SI32_PBSTD_1, 0x03A1);
  SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_1, 0xFC1C);
  SI32_PBSTD_A_write_pins_low(SI32_PBSTD_1, 0x0200 ); //Set 5V regulator off
  SI32_PBSTD_A_write_pins_high(SI32_PBSTD_1, 0x0100 ); //Set USB high power mode

  // Enable Crossbar0 signals & set properties
  SI32_PBCFG_A_enable_xbar0h_peripherals(SI32_PBCFG_0,
                                         SI32_PBCFG_A_XBAR0H_UART0EN |
                                         SI32_PBCFG_A_XBAR0H_UART1EN);
  SI32_PBCFG_A_enable_xbar0l_peripherals(SI32_PBCFG_0,
                                         SI32_PBCFG_A_XBAR0L_USART0EN |
                                         SI32_PBCFG_A_XBAR0L_SPI0EN |
                                         SI32_PBCFG_A_XBAR0L_SPI0NSSEN |
                                         SI32_PBCFG_A_XBAR0L_USART1EN |
                                         SI32_PBCFG_A_XBAR0L_USART1FCEN |
                                         SI32_PBCFG_A_XBAR0L_I2C0EN);
  SI32_PBCFG_A_enable_crossbar_0(SI32_PBCFG_0);

  // PB2 Setup
  SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_2, 0x7FFF);

  // PB3 Setup
  SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_3, 0x00FF);
  //PB3.6 is external input 1
  //PB3.7 is high voltage dc detection
  //PB3.8 is usb voltage detection
  SI32_PBSTD_A_set_pins_digital_input(SI32_PBSTD_3, 0x000001C0);
  SI32_PBSTD_A_disable_pullup_resistors( SI32_PBSTD_3 );

  SI32_PBSTD_A_disable_pullup_resistors( SI32_PBSTD_1 );

  //PB0.1 is external input 2
  SI32_PBSTD_A_set_pins_digital_input(SI32_PBSTD_0, 0x00000002);
  SI32_PBSTD_A_disable_pullup_resistors( SI32_PBSTD_0 );

  // Enable Crossbar1 signals & set properties
  SI32_PBCFG_A_enable_crossbar_1(SI32_PBCFG_0);

#else
  // Set up prinf pin
  //SI32_PBSTD_A_set_pins_push_pull_output(SI32_PBSTD_1, 0x00000008);

  SI32_PBCFG_A_enable_crossbar_1(SI32_PBCFG_0);
  SI32_PBCFG_A_enable_crossbar_0(SI32_PBCFG_0);

  // ENABLE LED DRIVERS (P2.11, P2.10)
  //SI32_PBSTD_A_set_pins_push_pull_output(SI32_PBSTD_2, 0x00000C00);

  // Enable switch sensing (P2.10, P2.11)
  //SI32_PBSTD_A_set_pins_digital_input(SI32_PBSTD_2, 0x00000300);

  // UART PINS TO PROPER CONFIG (TX = PB1.12, RX = PB1.13)
  SI32_PBSTD_A_set_pins_push_pull_output(SI32_PBSTD_1, 0x0001000);
  SI32_PBSTD_A_set_pins_digital_input(SI32_PBSTD_1, 0x00002000);
  SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_0, 0x0000FFFF);
  SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_1, 0x00000FFF);

  // BRING OUT UART
  SI32_PBCFG_A_enable_xbar0h_peripherals(SI32_PBCFG_0, SI32_PBCFG_A_XBAR0H_UART0EN);
#endif

  // Setup PBHD4
  SI32_PBCFG_A_unlock_ports(SI32_PBCFG_0);
  SI32_PBHD_A_write_pblock(SI32_PBHD_4, 0x00);

  SI32_PBHD_A_select_pin0_safe_state(SI32_PBHD_4, 0x0);
  SI32_PBHD_A_select_pin1_safe_state(SI32_PBHD_4, 0x0);
  SI32_PBHD_A_select_pin2_safe_state(SI32_PBHD_4, 0x0);
  SI32_PBHD_A_select_pin3_safe_state(SI32_PBHD_4, 0x0);
  SI32_PBHD_A_select_pin4_safe_state(SI32_PBHD_4, 0x0);
  SI32_PBHD_A_select_pin5_safe_state(SI32_PBHD_4, 0x0);

  SI32_PBHD_A_enable_bias(SI32_PBHD_4);
  SI32_PBHD_A_select_normal_power_port_mode(SI32_PBHD_4);
  SI32_PBHD_A_enable_drivers(SI32_PBHD_4);

  //Setup PB4.0/4.1 to outputs, no pullups, and low for MOSFET outputs
  //Setup PB4.3 LED0/1
  //Setup PB4.4/4.5 GPS
  //Setup PB4.2 to LOW to turn on mosfets for bat charger!
  SI32_PBHD_A_set_pins_push_pull_output( SI32_PBHD_4, 0x003F );
  SI32_PBHD_A_disable_pullup_resistors( SI32_PBHD_4 );
  SI32_PBHD_A_write_pins_low( SI32_PBHD_4, 0x3F );

  SI32_PBHD_A_set_pins_low_drive_strength(SI32_PBHD_4, 0x3F);

  //SI32_PBHD_A_select_nchannel_current_limit(SI32_PBHD_4, 0xA);
  SI32_PBHD_A_select_pchannel_current_limit(SI32_PBHD_4, 0xF);
  SI32_PBHD_A_enable_pin_current_limit( SI32_PBHD_4, 0x3F );

  SI32_PBHD_A_select_slew_rate(SI32_PBHD_4, SI32_PBHD_A_SLEW_FASTEST);

  // SI32_PBHD_A_select_pchannel_current_limit(SI32_PBHD_4, 0);
  // SI32_PBHD_A_enable_pin_current_limit( SI32_PBHD_4, 0x3F );

  // SI32_PBHD_A_write_pblock(SI32_PBHD_4, 0x0000);
  // SI32_PBHD_A_enable_drivers(SI32_PBHD_4);
  // SI32_PBHD_A_select_pchannel_current_limit(SI32_PBHD_4, 15);
  // SI32_PBHD_A_select_nchannel_current_limit(SI32_PBHD_4, 15);
  // SI32_PBHD_A_set_pins_push_pull_output(SI32_PBHD_4, 0x003F);
  // SI32_PBHD_A_set_pins_high_drive_strength(SI32_PBHD_4, 0x003F);
  // SI32_PBHD_A_enable_n_channel_drivers(SI32_PBHD_4, 0x003F);
  // SI32_PBHD_A_enable_p_channel_drivers(SI32_PBHD_4, 0x003F);
  // SI32_PBHD_A_enable_pin_current_limit(SI32_PBHD_4, 0x003F);
}


// The platform I/O functions
pio_type platform_pio_op( unsigned port, pio_type pinmask, int op )
{
  pio_type retval = 1;

  switch( op )
  {
    case PLATFORM_IO_PORT_SET_VALUE:
      if( port < 4)
        SI32_PBSTD_A_write_pins_masked( port_std[ port ], 0xFFFF, pinmask);
      else
        SI32_PBHD_A_write_pins_masked( SI32_PBHD_4, 0xFFFF, pinmask);
      break;

    case PLATFORM_IO_PIN_SET:
      if( port < 4)
        SI32_PBSTD_A_write_pins_high( port_std[ port ], pinmask );
      else
        SI32_PBHD_A_write_pins_high( SI32_PBHD_4, pinmask );
      break;

    case PLATFORM_IO_PIN_CLEAR:
      if( port < 4)
        SI32_PBSTD_A_write_pins_low( port_std[ port ], pinmask );
      else
        SI32_PBHD_A_write_pins_low( SI32_PBHD_4, pinmask );
      break;

    case PLATFORM_IO_PORT_DIR_OUTPUT:
      if( port < 4 )
        SI32_PBSTD_A_set_pins_push_pull_output( port_std[ port ], 0xFFFF );
      else
        SI32_PBHD_A_set_pins_push_pull_output( SI32_PBHD_4, 0xFFFF );
        //SI32_PBHD_A_enable_p_channel_drivers( SI32_PBHD_4, 0xFFFF );
      break;

    case PLATFORM_IO_PIN_DIR_OUTPUT:
      if( port < 4 )
        SI32_PBSTD_A_set_pins_push_pull_output( port_std[ port ], pinmask );
      else
        SI32_PBHD_A_set_pins_push_pull_output( SI32_PBHD_4, pinmask );
        //SI32_PBHD_A_enable_p_channel_drivers( SI32_PBHD_4, pinmask );
      break;

    case PLATFORM_IO_PORT_DIR_INPUT:
      if( port < 4 )
        SI32_PBSTD_A_set_pins_digital_input( port_std[ port ], 0xFFFF );
      else
        SI32_PBHD_A_set_pins_digital_input( SI32_PBHD_4, 0xFFFF );
      break;

    case PLATFORM_IO_PIN_DIR_INPUT:
      if( port < 4 )
        SI32_PBSTD_A_set_pins_digital_input( port_std[ port ], pinmask );
      else
        SI32_PBHD_A_set_pins_digital_input( SI32_PBHD_4, pinmask );
      break;

    case PLATFORM_IO_PORT_GET_VALUE:
      if( port < 4 )
        retval = SI32_PBSTD_A_read_pins( port_std[ port ] );
      else
        retval = SI32_PBHD_A_read_pins( SI32_PBHD_4 );
      break;

    case PLATFORM_IO_PIN_GET:
      if( port < 4 )
        retval = ( SI32_PBSTD_A_read_pins(port_std[ port ]) & pinmask ) ? 1 : 0;
      else
        retval = ( SI32_PBHD_A_read_pins( SI32_PBHD_4 ) & pinmask ) ? 1 : 0;
      break;

    default:
      retval = 0;
      break;
  }
  return retval;
}


// ****************************************************************************
// UART section


static SI32_UART_A_Type* const uart[] = { SI32_UART_0, SI32_UART_1 };
static SI32_USART_A_Type* const usart[] = { SI32_USART_0, SI32_USART_1 };

//For DK board, interface is On:
// PB1.12 RX
// PB1.13 TX
// PB1.14 CTS
// PB1.15 RTS


u32 platform_uart_setup( unsigned id, u32 baud, int databits, int parity, int stopbits )
{
  if( id < 2 )
  {
    SI32_USART_A_enter_full_duplex_mode( uart[ id ] );

    // Set Baud Rate
    // rate = F_APB / ( N * (R/TBAUD + 1 ) )
    SI32_USART_A_set_rx_baudrate( usart[ id ], (SystemCoreClock / (2 * baud)) - 1);
    SI32_USART_A_set_tx_baudrate( usart[ id ], (SystemCoreClock / (2 * baud)) - 1);

    // Use Asynchronous Mode
    SI32_USART_A_select_tx_asynchronous_mode ( usart[ id ] );
    SI32_USART_A_select_rx_asynchronous_mode ( usart[ id ] );

    // Set Data Bits
    SI32_USART_A_select_tx_data_length( usart[ id ], databits );
    SI32_USART_A_select_rx_data_length( usart[ id ], databits );


    SI32_USART_A_enable_tx_start_bit( usart[ id ] );
    SI32_USART_A_enable_tx_stop_bit( usart[ id ] );
    SI32_USART_A_enable_rx_start_bit( usart[ id ] );
    SI32_USART_A_enable_rx_stop_bit( usart[ id ] );

    if( stopbits == PLATFORM_UART_STOPBITS_2 )
    {
      SI32_USART_A_select_tx_stop_bits( usart[ id ], SI32_USART_A_STOP_BITS_2_BITS );
      SI32_USART_A_select_rx_stop_bits( usart[ id ], SI32_USART_A_STOP_BITS_2_BITS );
    }
    else
    {
      SI32_USART_A_select_tx_stop_bits( usart[ id ], SI32_USART_A_STOP_BITS_1_BIT );
      SI32_USART_A_select_rx_stop_bits( usart[ id ], SI32_USART_A_STOP_BITS_1_BIT );
    }

    // Set Parity
    switch( parity )
    {
      case PLATFORM_UART_PARITY_NONE:
        SI32_USART_A_disable_tx_parity_bit( usart[ id ] );
        SI32_USART_A_disable_rx_parity_bit( usart[ id ] );
        break;

      case PLATFORM_UART_PARITY_ODD:
        SI32_USART_A_enable_tx_parity_bit( usart[ id ] );
        SI32_USART_A_select_tx_parity( usart[ id ], 0 );
        SI32_USART_A_enable_rx_parity_bit( usart[ id ] );
        SI32_USART_A_select_rx_parity( usart[ id ], 0 );
        break;

      case PLATFORM_UART_PARITY_EVEN:
        SI32_USART_A_enable_tx_parity_bit( usart[ id ] );
        SI32_USART_A_select_tx_parity( usart[ id ], 1 );
        SI32_USART_A_enable_rx_parity_bit( usart[ id ] );
        SI32_USART_A_select_rx_parity( usart[ id ], 1 );
        break;
    }

    SI32_USART_A_disable_tx_signal_inversion( usart[ id ] );
    SI32_USART_A_disable_rx_signal_inversion( usart[ id ] );

    //SI32_USART_A_select_rx_fifo_threshold_2( usart[ id ] );

    // Enable RX & TX
    SI32_USART_A_enable_tx( usart[ id ] );
    SI32_USART_A_enable_rx( usart[ id ] );
  }
  else
  {
    id = id - 2;

    SI32_UART_A_enter_full_duplex_mode( uart[ id ] );

    // Set Baud Rate
    // rate = F_APB / ( N * (R/TBAUD + 1 ) )
    SI32_UART_A_set_rx_baudrate( uart[ id ], (SystemCoreClock / (2 * baud)) - 1);
    SI32_UART_A_set_tx_baudrate( uart[ id ], (SystemCoreClock / (2 * baud)) - 1);

    // Use Asynchronous Mode
    //SI32_UART_A_select_tx_asynchronous_mode ( uart[ id ] );
    //SI32_UART_A_select_rx_asynchronous_mode ( uart[ id ] );

    // Set Data Bits
    SI32_UART_A_select_tx_data_length( uart[ id ], databits );
    SI32_UART_A_select_rx_data_length( uart[ id ], databits );


    SI32_UART_A_enable_tx_start_bit( uart[ id ] );
    SI32_UART_A_enable_tx_stop_bit( uart[ id ] );
    SI32_UART_A_enable_rx_start_bit( uart[ id ] );
    SI32_UART_A_enable_rx_stop_bit( uart[ id ] );

    if( stopbits == PLATFORM_UART_STOPBITS_2 )
    {
      SI32_UART_A_select_tx_stop_bits( uart[ id ], SI32_UART_A_STOP_BITS_2_BITS );
      SI32_UART_A_select_rx_stop_bits( uart[ id ], SI32_UART_A_STOP_BITS_2_BITS );
    }
    else
    {
      SI32_UART_A_select_tx_stop_bits( uart[ id ], SI32_UART_A_STOP_BITS_1_BIT );
      SI32_UART_A_select_rx_stop_bits( uart[ id ], SI32_UART_A_STOP_BITS_1_BIT );
    }

    // Set Parity
    switch( parity )
    {
      case PLATFORM_UART_PARITY_NONE:
        SI32_UART_A_disable_tx_parity_bit( uart[ id ] );
        SI32_UART_A_disable_rx_parity_bit( uart[ id ] );
        break;

      case PLATFORM_UART_PARITY_ODD:
        SI32_UART_A_enable_tx_parity_bit( uart[ id ] );
        SI32_UART_A_select_tx_parity( uart[ id ], 0 );
        SI32_UART_A_enable_rx_parity_bit( uart[ id ] );
        SI32_UART_A_select_rx_parity( uart[ id ], 0 );
        break;

      case PLATFORM_UART_PARITY_EVEN:
        SI32_UART_A_enable_tx_parity_bit( uart[ id ] );
        SI32_UART_A_select_tx_parity( uart[ id ], 1 );
        SI32_UART_A_enable_rx_parity_bit( uart[ id ] );
        SI32_UART_A_select_rx_parity( uart[ id ], 1 );
        break;
    }

    SI32_UART_A_disable_tx_signal_inversion( uart[ id ] );
    SI32_UART_A_disable_rx_signal_inversion( uart[ id ] );

    //SI32_UART_A_select_rx_fifo_threshold_2( uart[ id ] );

    // Enable RX & TX
    SI32_UART_A_enable_tx( uart[ id ] );
    SI32_UART_A_enable_rx( uart[ id ] );
  }


  return baud; // FIXME: find a way to actually get baud
}

void platform_s_uart_send( unsigned id, u8 data )
{
  if( id < 2 )
  {
    // Block if the output buffer is full
    while (SI32_USART_A_read_tx_fifo_count(usart[ id ]) >= 4);

    // Write character to the output buffer
    SI32_USART_A_write_data_u8(usart[ id ], data);
  }
  else
  {
    id = id - 2;
    // Block if the output buffer is full
    while (SI32_UART_A_read_tx_fifo_count(uart[ id ]) >= 4);

    // Write character to the output buffer
    SI32_UART_A_write_data_u8(uart[ id ], data);
  }
}

int platform_s_uart_recv( unsigned id, timer_data_type timeout )
{
  if( id < 2 )
  {
    if( timeout == 0 )
    {
      if ( SI32_USART_A_read_rx_fifo_count( usart[ id ] ) == 0 )
        return -1;
      else
        return ( int )SI32_USART_A_read_data_u8( usart[ id ] );
    }

    // Block if input buffer is empty
    while (SI32_USART_A_read_rx_fifo_count( usart[ id ] ) == 0);

    // Read character from the input buffer
    return ( int )SI32_USART_A_read_data_u8( usart[ id ] );
  }
  else
  {
    id = id - 2;
    if( timeout == 0 )
    {
      if ( SI32_UART_A_read_rx_fifo_count( uart[ id ] ) == 0 )
        return -1;
      else
        return ( int )SI32_UART_A_read_data_u8( uart[ id ] );
    }

    // Block if input buffer is empty
    while (SI32_UART_A_read_rx_fifo_count( uart[ id ] ) == 0);

    // Read character from the input buffer
    return ( int )SI32_UART_A_read_data_u8( uart[ id ] );
  }
}

int platform_s_uart_set_flow_control( unsigned id, int type )
{
  if( id < 2 )
  {
    if( type == PLATFORM_UART_FLOW_NONE )
    {
      SI32_USART_A_disable_cts( usart[ id ] );
      SI32_USART_A_disable_rts( usart[ id ] );
    }
    if( type & PLATFORM_UART_FLOW_CTS )
    {
      SI32_USART_A_enable_cts( usart[ id ] );
    }
    if( type & PLATFORM_UART_FLOW_RTS )
    {
      SI32_USART_A_enable_rts( usart[ id ] );
    }
  }
  else
  {
    id = id - 2;
    if( type == PLATFORM_UART_FLOW_NONE )
    {
      SI32_UART_A_disable_cts( uart[ id ] );
      SI32_UART_A_disable_rts( uart[ id ] );
    }
    if( type & PLATFORM_UART_FLOW_CTS )
    {
      SI32_UART_A_enable_cts( uart[ id ] );
    }
    if( type & PLATFORM_UART_FLOW_RTS )
    {
      SI32_UART_A_enable_rts( uart[ id ] );
    }
  }

  return PLATFORM_OK;
}

// ****************************************************************************
// Timer section


/*
// Helper function: get timer clock
static u32 platform_timer_get_clock( unsigned id )
{
  return 0;
}

// Helper function: set timer clock
static u32 platform_timer_set_clock( unsigned id, u32 clock )
{
  return clock;
}

// Helper function: setup timers
static void platform_setup_timers()
{

}*/

int platform_s_timer_set_match_int( unsigned id, timer_data_type period_us, int type )
{
    return PLATFORM_TIMER_INT_OK;
}

void platform_s_timer_delay( unsigned id, timer_data_type delay_us )
{

}

timer_data_type platform_s_timer_op( unsigned id, int op, timer_data_type data )
{
  u32 res = 0;

  switch( op )
  {
    case PLATFORM_TIMER_OP_START:
      break;

    case PLATFORM_TIMER_OP_READ:
      break;

    case PLATFORM_TIMER_OP_SET_CLOCK:
      break;

    case PLATFORM_TIMER_OP_GET_CLOCK:
      break;

    case PLATFORM_TIMER_OP_GET_MAX_CNT:
      break;
  }
  return res;
}

u64 platform_timer_sys_raw_read()
{
  return SysTick->LOAD - SysTick->VAL;
}

void platform_timer_sys_disable_int()
{
  SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;
}

void platform_timer_sys_enable_int()
{
  SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
}

timer_data_type platform_timer_read_sys()
{
  return cmn_systimer_get();
}

// ****************************************************************************
// I2C support

#if NUM_I2C > 0

//#define DEBUG_I2C

static SI32_I2C_A_Type* const i2cs[] = { SI32_I2C_0, SI32_I2C_1 };

#define  I2C_WRITE          0x00           // I2C WRITE command
#define  I2C_READ           0x01           // I2C READ command

u32 platform_i2c_setup( unsigned id, u32 speed )
{
  SI32_I2C_A_set_scaler_value( i2cs[ id ], ( cmsis_get_cpu_frequency() / speed ) );

  // set SETUP time to non-zero value for repeated starts to function correctly
  SI32_I2C_A_set_extended_data_setup_time(SI32_I2C_0, 0x01);

  // ENABLE MODULE
  SI32_I2C_A_enable_module( i2cs[ id ] );

  // Return actual speed
  return cmsis_get_cpu_frequency() / SI32_I2C_A_get_scaler_value( i2cs[ id ] );
}

void platform_i2c_send_start( unsigned id )
{
  // The master write operation starts with firmware setting the STA
  // bit to generate a start condition.

  SI32_I2C_A_set_start( i2cs[ id ] );

  if( SI32_I2C_A_is_tx_interrupt_pending( i2cs[ id ] ) )
    SI32_I2C_A_clear_tx_interrupt ( i2cs[ id ] );

  if( SI32_I2C_A_is_rx_interrupt_pending( i2cs[ id ] ) )
    SI32_I2C_A_clear_rx_interrupt ( i2cs[ id ] );

  while( SI32_I2C_A_is_start_interrupt_pending( i2cs[ id ] ) == 0 );
#if defined( DEBUG_I2C )
  printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif
}

void platform_i2c_send_stop( unsigned id )
{
  if ( SI32_I2C_A_is_busy( i2cs[ id ] ) )
  {
#if defined( DEBUG_I2C )
    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif

    if( SI32_I2C_A_is_tx_interrupt_pending( i2cs[ id ] ) )
      SI32_I2C_A_clear_tx_interrupt ( i2cs[ id ] );

    if( SI32_I2C_A_is_rx_interrupt_pending( i2cs[ id ] ) )
      SI32_I2C_A_clear_rx_interrupt ( i2cs[ id ] );

    SI32_I2C_A_set_stop( i2cs[ id ] );
#if defined( DEBUG_I2C )
    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif

    while( SI32_I2C_A_is_stop_interrupt_pending( i2cs[ id ] ) == 0 );

    SI32_I2C_A_clear_stop( i2cs[ id ] );
    SI32_I2C_A_send_nack ( i2cs[ id ] );
    SI32_I2C_A_clear_stop_interrupt( i2cs[ id ] );
#if defined( DEBUG_I2C )
    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif
  }
  else
  {
#if defined( DEBUG_I2C )
    printf("send_stop: not active\n");
#endif
  }
}

int platform_i2c_send_address( unsigned id, u16 address, int direction )
{
  u8 acktmp = 0;
  // The ISR or firmware routine should then clear the start bit (STA),
  // set the targeted slave address and the R/W direction bit in the DATA
  // register, set the byte count, arm the transmission (TXARM = 1), and
  // clear the start interrupt.
  if ( SI32_I2C_A_is_busy( i2cs[ id ] ) )
  {
#if defined( DEBUG_I2C )
    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif
    while( SI32_I2C_A_is_start_interrupt_pending( i2cs[ id ] ) == 0 );

    //SI32_I2C_A_set_slave_address_7_bit( i2cs[ id ] );
    SI32_I2C_A_set_byte_count( i2cs[ id ] , 1);
    SI32_I2C_A_write_data( i2cs[ id ] , ( address << 1 ) | (direction == PLATFORM_I2C_DIRECTION_TRANSMITTER ?  I2C_WRITE : I2C_READ) );
    SI32_I2C_A_arm_tx( i2cs[ id ] );

    SI32_I2C_A_clear_start( i2cs[ id ] );
    SI32_I2C_A_clear_start_interrupt( i2cs[ id ] );
    SI32_I2C_A_clear_ack_interrupt( i2cs[ id ] );

    while( SI32_I2C_A_is_tx_interrupt_pending( i2cs[ id ] ) == 0 );

    acktmp = ( u8 )SI32_I2C_A_is_ack_received( i2cs[ id ] );


    SI32_I2C_A_clear_ack_interrupt( i2cs[ id ] );

    if( direction == PLATFORM_I2C_DIRECTION_RECEIVER )
    {
      SI32_I2C_A_clear_tx_interrupt( i2cs[ id ] );
    }
#if defined( DEBUG_I2C )
    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif
  }
  else
  {
    //#if defined( DEBUG_I2C )
    printf("send_addr: not active\n");
    //#endif
  }

  return acktmp;
}

int platform_i2c_send_byte( unsigned id, u8 data )
{
  if ( SI32_I2C_A_is_busy( i2cs[ id ] ) )
  {
#if defined( DEBUG_I2C )
    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif
    u32 tmpdata = ( u32 )data;
    SI32_I2C_A_set_byte_count( i2cs[ id ] , 1 );
    SI32_I2C_A_write_data( i2cs[ id ], tmpdata );
    SI32_I2C_A_arm_tx( i2cs[ id ] );
    SI32_I2C_A_clear_tx_interrupt( i2cs[ id ] );

    while( SI32_I2C_A_is_tx_interrupt_pending( i2cs[ id ] ) == 0 );

    SI32_I2C_A_clear_ack_interrupt( i2cs[ id ] );

#if defined( DEBUG_I2C )
    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif
    if( SI32_I2C_A_is_ack_received(SI32_I2C_0) )
      return 1;
    else
      return 0;
  }
  else
  {
#if defined( DEBUG_I2C )
    printf("send_byte: not active\n");
#endif
    return 0;
  }
}

int platform_i2c_recv_byte( unsigned id, int ack )
{
  u32 tmpdata;
  if ( SI32_I2C_A_is_busy( i2cs[ id ] ) )
  {
#if defined( DEBUG_I2C )
    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif

    SI32_I2C_A_set_byte_count( i2cs[ id ] , 1);
    SI32_I2C_A_arm_rx( i2cs[ id ] );

    if( SI32_I2C_A_is_rx_interrupt_pending( i2cs[ id ] ) )
      SI32_I2C_A_clear_rx_interrupt ( i2cs[ id ] );
    else if( SI32_I2C_A_is_tx_interrupt_pending( i2cs[ id ] ) )
      SI32_I2C_A_clear_tx_interrupt ( i2cs[ id ] );

    while( SI32_I2C_A_is_ack_interrupt_pending( i2cs[ id ] ) == 0 );

    if( ack )
      SI32_I2C_A_send_ack( i2cs[ id ] );
    else
      SI32_I2C_A_send_nack( i2cs[ id ] );

    SI32_I2C_A_clear_ack_interrupt( i2cs[ id ] );

    while( SI32_I2C_A_is_rx_interrupt_pending( i2cs[ id ] ) == 0 );

    tmpdata = SI32_I2C_A_read_data( i2cs[ id ] );
#if defined( DEBUG_I2C )
    printf("Got: %u\n",(u8)tmpdata);
    if( 0xFFFFFF00 & tmpdata )
      printf("GOT MORE THAN ONE BYTE!\n");

    printf("CONTROL = %lx\n",  i2cs[ id ]->CONTROL.u32 );
#endif

      return ( u8 )tmpdata;
  }
  else
  {
#if defined( DEBUG_I2C )
    printf("recv_byte: not active\n");
#endif
    return 0;
  }
}

#endif // NUM_I2C > 0

// ****************************************************************************
// PMU functions

void sim3_pmu_sleep( unsigned seconds )
{
  // GET CURRENT TIMER VALUE INTO SETCAP
  SI32_RTC_A_start_timer_capture(SI32_RTC_0);
  while(SI32_RTC_A_is_timer_capture_in_progress(SI32_RTC_0));

  // SET ALARM FOR now+s
  // RTC running at 16.384Khz so there are 16384 cycles/sec)
  SI32_RTC_A_write_alarm0(SI32_RTC_0, SI32_RTC_A_read_setcap(SI32_RTC_0) + (16384 * seconds));
  SI32_RTC_A_clear_alarm0_interrupt(SI32_RTC_0);

  // Enable RTC alarm interrupt
  SI32_RTC_A_enable_alarm0_interrupt(SI32_RTC_0);

  // Disable crossbar peripheral connections
  //SI32_PBCFG_A_write_xbar1(SI32_PBCFG_0,0x00000000);
  //SI32_PBCFG_A_write_xbar0h(SI32_PBCFG_0,0x00000000);
  //SI32_PBCFG_A_write_xbar0l(SI32_PBCFG_0,0x00000000);

  // Mask low priority interrupts from waking us
  __set_BASEPRI(0x40);

  SI32_DMACTRL_A_disable_module( SI32_DMACTRL_0 );
  SI32_CLKCTRL_A_exit_fast_wake_mode( SI32_CLKCTRL_0 );
  SI32_RSTSRC_A_disable_power_mode_9( SI32_RSTSRC_0 );

  // Switch VREG to low power mode
  SI32_VREG_A_disable_band_gap( SI32_VREG_0 );
  SI32_VREG_A_enter_suspend_mode( SI32_VREG_0 );
  //SI32_VREG_A_enable_vbus_invalid_interrupt( SI32_VREG_0 );

  // Disable VDD Monitor
  SI32_VMON_A_disable_vdd_supply_monitor(SI32_VMON_0);

  // Switch AHB source to LFO oscillator
  SI32_CLKCTRL_A_select_ahb_source_low_frequency_oscillator( SI32_CLKCTRL_0 );

  // Turn off all peripheral clocks
  SI32_CLKCTRL_A_disable_apb_to_all_modules( SI32_CLKCTRL_0 );

  __WFI();

  SI32_CLKCTRL_A_select_ahb_source_low_power_oscillator(SI32_CLKCTRL_0);

  // Allow all interrupts
  __set_BASEPRI(0x00);

  // Re-enable clocks used at startup
  clk_init();
  pios_init();

  SI32_VREG_A_enable_band_gap( SI32_VREG_0 );
  SI32_EXTVREG_A_enable_module( SI32_EXTVREG_0 );
}

void sim3_pmu_reboot( void )
{
  // Stop USB
  SI32_USB_A_reset_module( SI32_USB_0 );
  NVIC_DisableIRQ(USB0_IRQn);
  SI32_USB_A_disable_module(SI32_USB_0);
  SI32_USB_A_disable_internal_pull_up( SI32_USB_0 );

  SI32_RSTSRC_A_generate_software_reset( SI32_RSTSRC_0 );
}

void myPMU_enter_sleep(void)
{
  // Configure for Sleep
  SI32_PMU_A_clear_wakeup_flags(SI32_PMU_0);
  SI32_RSTSRC_A_enable_power_mode_9(SI32_RSTSRC_0);
  SCB->SCR = SCB_SCR_SLEEPDEEP_Msk; // set SLEEPDEEP
  __set_FAULTMASK(1);
  __WFI();
}
void VREG0_vbus_invalid_handler(void)
{
}
void myPB_enter_off_config()
{
  //SI32_PBSTD_A_write_pins_high(SI32_PBSTD_2, 0xC00);
  /*int i;
  for( i=0; i<4; i++)
  {
    SI32_PBSTD_A_set_pins_digital_input( port_std[ i ], 0xFFFF);
    SI32_PBSTD_A_disable_pullup_resistors( port_std[ i ] );
    SI32_PBSTD_A_write_pins_low( port_std[ i ], 0xFFFF );
  }

   SI32_PBHD_A_disable_bias( SI32_PBHD_4 );
   SI32_PBHD_A_disable_pin_current_limit( SI32_PBHD_4, 0x3F );
   SI32_PBHD_A_set_pins_digital_input( SI32_PBHD_4, 0x3F );
   SI32_PBHD_A_disable_pullup_resistors( SI32_PBHD_4 );
   SI32_PBHD_A_write_pins_low( SI32_PBHD_4, 0x3F );*/


//  SI32_PBHD_A_set_pins_push_pull_output( SI32_PBHD_4, 0x00 ); //Disable P channel drivers
//  SI32_PBHD_A_disable_p_channel_drivers( SI32_PBHD_4, 0x3F );
//  SI32_PBHD_A_disable_n_channel_drivers( SI32_PBHD_4, 0x3F );

  // all ports hi-z (analog)
  //SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_0, 0x0000FFFF);
  //SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_1, 0x0000FFFF);
  //SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_2, 0x0000FFFF);
  //SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_3, 0x0000FFFF);

  //PBHD to High impedance
  //SI32_PBHD_A_disable_drivers(SI32_PBHD_4);
  //SI32_PBHD_A_disable_bias(SI32_PBHD_4);

   // all ports hi-z (analog)
  SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_0, 0x0000FFFF);
  SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_1, 0x0000FFFF);
  SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_2, 0x0000FFFF);
  SI32_PBSTD_A_set_pins_analog(SI32_PBSTD_3, 0x0000FFFF);

  //PBHD to High impedance
  //SI32_PBHD_A_disable_drivers(SI32_PBHD_4);
  //SI32_PBHD_A_disable_bias(SI32_PBHD_4);

  //PBHD to High impedance
  //SI32_PBHD_A_set_pins_low_drive_strength(SI32_PBHD_4,0x3F);
  //SI32_PBHD_A_write_pins_low( SI32_PBHD_4, 0x3F );
  ////SI32_PBHD_A_disable_drivers(SI32_PBHD_4);
  ////SI32_PBHD_A_disable_bias(SI32_PBHD_4);
  SI32_PBCFG_A_unlock_ports(SI32_PBCFG_0);
  SI32_PBHD_A_write_pblock(SI32_PBHD_4, 0x00);

  SI32_PBCFG_A_enable_xbar0h_peripherals(SI32_PBCFG_0, 0x0000);
  SI32_PBCFG_A_enable_xbar0l_peripherals(SI32_PBCFG_0, 0x0000);
  SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_0, 0xFFFF);
  SI32_PBSTD_A_write_pbskipen(SI32_PBSTD_1, 0xFFFF);
  SI32_PBCFG_A_enable_crossbar_0(SI32_PBCFG_0);
  SI32_PBCFG_A_enable_crossbar_1(SI32_PBCFG_0);

  int i;
  for( i=0; i<4; i++)
  {
	SI32_PBSTD_A_set_pins_analog(port_std[ i ], 0x0000);
	SI32_PBSTD_A_write_pbskipen(port_std[ i ], 0xFFFF);
    SI32_PBSTD_A_set_pins_digital_input( port_std[ i ], 0xFFFF);
    SI32_PBSTD_A_disable_pullup_resistors( port_std[ i ] );
    SI32_PBSTD_A_write_pins_low( port_std[ i ], 0xFFFF );
  }
  //Set I2C pins to analog float...
  SI32_PBSTD_A_set_pins_analog( SI32_PBSTD_0, 0x0000C000);
  SI32_PBSTD_A_write_pins_low( SI32_PBSTD_0, 0xC000 );


  SI32_PBHD_A_set_pins_push_pull_output( SI32_PBHD_4, 0x00 );
  SI32_PBHD_A_set_pins_low_drive_strength(SI32_PBHD_4, 0x3F);
  SI32_PBHD_A_disable_bias( SI32_PBHD_4 );
  SI32_PBHD_A_disable_pin_current_limit( SI32_PBHD_4, 0x3F );
  SI32_PBHD_A_set_pins_digital_input( SI32_PBHD_4, 0x3F );
  SI32_PBHD_A_disable_pullup_resistors( SI32_PBHD_4 );
  SI32_PBHD_A_write_pins_low( SI32_PBHD_4, 0x3F );

  //Set the disconnects mosfets to float!
  SI32_PBHD_A_set_pins_push_pull_output( SI32_PBHD_4, 0x04 );
  SI32_PBHD_A_write_pins_high( SI32_PBHD_4, 0x04 );
}


void sim3_pmu_pm9( unsigned seconds )
{
  //u8 i;

  if(seconds != TRICK_TO_REBOOT_WITHOUT_DFU_MODE && external_power())
  {
    printf("Unit is powered, no PM9\n");
    rram_reg[0] = seconds;
    return;
  }
  if(seconds == TRICK_TO_REBOOT_WITHOUT_DFU_MODE)
    seconds = 1;

  // GET CURRENT TIMER VALUE INTO SETCAP
  //SI32_RTC_A_start_timer_capture(SI32_RTC_0);
  //while(SI32_RTC_A_is_timer_capture_in_progress(SI32_RTC_0));

  // SET ALARM FOR now+s
  // RTC running at 16.384Khz so there are 16384 cycles/sec)
  if( seconds > PIN_CHECK_INTERVAL )
  {
    rram_reg[0] = seconds - PIN_CHECK_INTERVAL;
    SI32_RTC_A_write_alarm0(SI32_RTC_0, /*SI32_RTC_A_read_setcap(SI32_RTC_0) +*/ (16384 * PIN_CHECK_INTERVAL));
  }
  else
  {
    rram_reg[0] = 0;
    SI32_RTC_A_write_alarm0(SI32_RTC_0, /*SI32_RTC_A_read_setcap(SI32_RTC_0) +*/ (16384 * seconds));
  }


  // Sleep if buttons pressed
  // Disable the SysTick timer to prevent these interrupts
  // from waking the core.
  SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;


  // Stop USB
  SI32_USB_A_reset_module( SI32_USB_0 );
  NVIC_DisableIRQ(USB0_IRQn);
  SI32_USB_A_disable_module(SI32_USB_0);
  SI32_USB_A_disable_internal_pull_up( SI32_USB_0 );

  // Enter Sleep Mode
  myPB_enter_off_config();
  VMON0_enter_power_9_mode_from_normal_power_mode();
  LDO0_enter_power_9_mode_from_normal_power_mode();
  VREG0_enter_power_9_mode_from_normal_power_mode();
  //PMU0_enter_power_9_mode_from_normal_power_mode();
  RSTSRC0_enter_power_9_mode_from_normal_power_mode();

  //Reset RTC Timer and clear any interrupts
  SI32_RTC_A_write_setcap(SI32_RTC_0, 0x0);
  SI32_RTC_A_start_timer_set(SI32_RTC_0);
  SI32_RTC_A_clear_alarm0_interrupt(SI32_RTC_0);
  SI32_RTC_A_clear_oscillator_fail_flag(SI32_RTC_0);
  while(SI32_RTC_A_is_timer_set_in_progress(SI32_RTC_0));

  //Enable reset off of RTC or Reset Pin
  SI32_PMU_A_write_wakeen(SI32_PMU_0, 0x0); //Clear all wake events
  SI32_PMU_A_enable_reset_pin_wake_event(SI32_PMU_0);
  SI32_PMU_A_enable_rtc0_alarm_wake_event(SI32_PMU_0);

  //Enable WAKE setup
  SI32_PMU_A_set_pin_wake_events( SI32_PMU_0, (1 << 10), (1 << 10) );
  SI32_RSTSRC_0->RESETEN_SET = SI32_RSTSRC_A_RESETEN_WAKEREN_MASK | 0x0000003; //replace incorrect SI32_PMU_A_enable_pin_wake_reset
  SI32_PMU_A_enable_pin_wake_event( SI32_PMU_0 );
  SI32_PMU_A_enable_pin_wake( SI32_PMU_0 );

  SI32_PMU_A_clear_wakeup_flags( SI32_PMU_0 );
  //>>HERE
  SI32_RTC_A_start_timer(SI32_RTC_0); //Start count down to wakeup
  myPMU_enter_sleep();




  // GET CURRENT TIMER VALUE INTO SETCAP
  //--SI32_RTC_A_start_timer_capture(SI32_RTC_0);
  //--while(SI32_RTC_A_is_timer_capture_in_progress(SI32_RTC_0));

  // SET ALARM FOR now+s
  // RTC running at 16.384Khz so there are 16384 cycles/sec)
  //--if( seconds > PIN_CHECK_INTERVAL )
  //--{
  //--  rram_reg[0] = seconds - PIN_CHECK_INTERVAL;
  //--  SI32_RTC_A_write_alarm0(SI32_RTC_0, SI32_RTC_A_read_setcap(SI32_RTC_0) + (16384 * PIN_CHECK_INTERVAL));
  //--}
  //--else
  //--{
  //--  rram_reg[0] = 0;
  //--  SI32_RTC_A_write_alarm0(SI32_RTC_0, SI32_RTC_A_read_setcap(SI32_RTC_0) + (16384 * seconds));
  //--}

  //--SI32_RTC_A_clear_alarm0_interrupt(SI32_RTC_0);

  // Stop USB
  //--SI32_USB_A_reset_module( SI32_USB_0 );
  //--NVIC_DisableIRQ(USB0_IRQn);
  //--SI32_USB_A_disable_module(SI32_USB_0);
  //--SI32_USB_A_disable_internal_pull_up( SI32_USB_0 );


  // Enable RTC alarm interrupt
  //SI32_RTC_A_enable_alarm0_interrupt(SI32_RTC_0);

  // Disable crossbar peripheral connections
  //SI32_PBCFG_A_write_xbar1(SI32_PBCFG_0,0x00000000);
  //SI32_PBCFG_A_write_xbar0h(SI32_PBCFG_0,0x00000000);
  //SI32_PBCFG_A_write_xbar0l(SI32_PBCFG_0,0x00000000);

  // Set pins as digital inputs, disable pullups and set pins low since
  // SI32_PBSTD_A_set_pins_digital_input sets latch high
  //--for( i=0; i<4; i++)
  //--{
  //--  SI32_PBSTD_A_set_pins_digital_input( port_std[ i ], 0xFFFF);
  //--  SI32_PBSTD_A_disable_pullup_resistors( port_std[ i ] );
  //--  SI32_PBSTD_A_write_pins_low( port_std[ i ], 0xFFFF );
  //--}

  // Attempt to minimize current on I2C pins by enabling pullups again on
  // PB0 & resetting pins as digital input (sets latch high)
  //--SI32_PBSTD_A_enable_pullup_resistors( port_std[ 0 ] );
  //--SI32_PBSTD_A_set_pins_digital_input( port_std[ 0 ], 0x6000);
  //SI32_PBSTD_A_enable_pullup_resistors( port_std[ 3 ] );

  // Prep PBHD for PM9 and set up as digital inputs as done with
  // PBSTD ports.
  //--SI32_PBHD_A_disable_bias( SI32_PBHD_4 );
  //--SI32_PBHD_A_disable_pin_current_limit( SI32_PBHD_4, 0x3F );
  //--SI32_PBHD_A_set_pins_digital_input( SI32_PBHD_4, 0x3F );
  //--SI32_PBHD_A_disable_pullup_resistors( SI32_PBHD_4 );
  //--SI32_PBHD_A_write_pins_low( SI32_PBHD_4, 0x3F );

  // Mask low priority interrupts from waking us
  //--__set_BASEPRI(0x40);

  // DISABLE all wakeup sources
  SI32_PMU_A_write_wakeen(SI32_PMU_0, 0x0);

  // ENABLE RTC_Alarm, RESET pin & Comparator 0 as wake events
  //SI32_PMU_A_enable_rtc0_alarm_wake_event( SI32_PMU_0 );
  //SI32_PMU_A_enable_reset_pin_wake_event( SI32_PMU_0 );
  //SI32_PMU_A_enable_comparator0_wake_event( SI32_PMU_0 );

  // Enable 3.6 (WAKE.10)
  //SI32_PMU_A_set_pin_wake_events( SI32_PMU_0, (1 << 10), (1 << 10) );
  //SI32_PMU_A_enable_pin_wake_event( SI32_PMU_0 );

  //SI32_DMACTRL_A_disable_module( SI32_DMACTRL_0 );

  // Switch VREG to low power mode
  //--SI32_VREG_A_disable_band_gap( SI32_VREG_0 );
  //--SI32_VREG_A_enter_suspend_mode( SI32_VREG_0 );
  //SI32_VREG_A_enable_vbus_invalid_interrupt( SI32_VREG_0 );
  //SI32_PMU_A_enable_pin_wake( SI32_PMU_0 );

  // Disable VDD Monitor
  //--SI32_VMON_A_disable_vdd_supply_monitor(SI32_VMON_0);

  // CLEAR WAKUP SOURCES
  //--SI32_PMU_A_clear_wakeup_flags( SI32_PMU_0 );

  SI32_RSTSRC_A_enable_power_mode_9(SI32_RSTSRC_0);
  SI32_RSTSRC_A_enable_rtc0_reset_source(SI32_RSTSRC_0);
  //SI32_RSTSRC_A_enable_comparator0_reset_source( SI32_RSTSRC_0 );
//  SI32_RSTSRC_0->RESETEN_SET = SI32_RSTSRC_A_RESETEN_WAKEREN_MASK | 0x0000003; //The default value has the first 2 bits set

  // Turn off all peripheral clocks
  //--SI32_CLKCTRL_A_disable_apb_to_all_modules( SI32_CLKCTRL_0 );

  //PROBLEM: If this is enabled, the PMU STATUS register does not indicate wake from PM9...
//  SI32_CLKCTRL_A_select_ahb_source_low_frequency_oscillator( SI32_CLKCTRL_0 );

  // SET DEEPSLEEP in SCR (and service all pending interrutps before sleep
  //--SCB->SCR = 0x14;

  // Switch AHB source to RTC oscillator

  //--__WFI();

  // We should never actually get here...

  //--SI32_PMU_A_clear_pmu_level_shifter_hold(SI32_PMU_0);
  //--SI32_PMU_A_clear_pin_level_shifter_hold(SI32_PMU_0);

  //--SI32_CLKCTRL_A_select_ahb_source_low_power_oscillator(SI32_CLKCTRL_0);

  // Allow all interrupts
  //--__set_BASEPRI(0x00);

  // Re-enable clocks used at startup
  //--clk_init();

  //--SI32_VREG_A_enable_band_gap( SI32_VREG_0 );
  //--SI32_EXTVREG_A_enable_module( SI32_EXTVREG_0 );
}

// ****************************************************************************
// PBHD functions

//  SI32_PBHD_A_select_low_power_port_mode(SI32_PBHD_4);  //needs to be high power if VDDHD >3.6v
//  SI32_PBHD_A_select_slew_rate(SI32_PBHD_4, SI32_PBHD_A_SLEW_FASTEST);
//  SI32_PBHD_A_set_pins_low_drive_strength(SI32_PBHD_4, 0x3F);
//  SI32_PBHD_A_enable_drivers(SI32_PBHD_4);

void sim3_pbhd_setbias( unsigned state )
{
  //  SI32_PBHD_A_enable_bias(SI32_PBHD_4)
}

void sim3_pbhd_setdrive( unsigned state )
{

}

void sim3_pbhd_setslew( unsigned state )
{

}

void sim3_pbhd_setdrivestrength( unsigned state, int pin )
{

}

// ****************************************************************************
// Flash access functions

volatile u8 flash_key_mask  = 0x00;
volatile u8 armed_flash_key = 0x00;

u8 flash_erase( u32 address, u8 verify)
{
    u32 wc;
    u32* verify_address;
    // Write the address of the Flash page to WRADDR
    SI32_FLASHCTRL_A_write_wraddr( SI32_FLASHCTRL_0, address );
    // Enter Flash Erase Mode
    SI32_FLASHCTRL_A_enter_flash_erase_mode( SI32_FLASHCTRL_0 );

    // Disable interrupts
    //hw_intp_disable();

    // Unlock the flash interface for a single access
    armed_flash_key = flash_key_mask ^ 0xA4;
    SI32_FLASHCTRL_A_write_flash_key(SI32_FLASHCTRL_0, armed_flash_key);
    armed_flash_key = flash_key_mask ^ 0xF0;
    SI32_FLASHCTRL_A_write_flash_key(SI32_FLASHCTRL_0, armed_flash_key);
    armed_flash_key = 0;

    // Write any value to initiate a page erase.
    SI32_FLASHCTRL_A_write_wrdata(SI32_FLASHCTRL_0, 0xA5);

    // Wait for flash operation to complete
    while (SI32_FLASHCTRL_A_is_flash_busy(SI32_FLASHCTRL_0));

    if( verify )
    {

        address &= ~(INTERNAL_FLASH_SECTOR_SIZE - 1); // Round down to nearest even page address
        verify_address = (u32*)address;

        for( wc = INTERNAL_FLASH_SECTOR_SIZE/4; wc != 0; wc-- )
        {
            if ( *verify_address != 0xFFFFFFFF )
                return 1;

            verify_address++;
        }
    }

    //hw_intp_enable();

    return 0;
}


u8 flash_write( u32 address, u32* data, u32 count, u8 verify )
{
    u32* tmpdata = data;
    u32* verify_address;
    u32 wc;

    // Write the address of the Flash page to WRADDR
    SI32_FLASHCTRL_A_write_wraddr( SI32_FLASHCTRL_0, address );
    // Enter flash erase mode
    SI32_FLASHCTRL_A_exit_flash_erase_mode(SI32_FLASHCTRL_0);

    // disable interrupts
    //hw_intp_disable();

    // Unlock flash interface for multiple accesses
    armed_flash_key = flash_key_mask ^ 0xA4;
    SI32_FLASHCTRL_A_write_flash_key(SI32_FLASHCTRL_0, armed_flash_key);
    armed_flash_key = flash_key_mask ^ 0xF3;
    SI32_FLASHCTRL_A_write_flash_key(SI32_FLASHCTRL_0, armed_flash_key);
    armed_flash_key = 0;

    // Write word-sized
    for( wc = count; wc != 0; wc-- )
    {
        SI32_FLASHCTRL_A_write_wrdata( SI32_FLASHCTRL_0, *data );
        SI32_FLASHCTRL_A_write_wrdata( SI32_FLASHCTRL_0, *data >> 16 );
        data++;
    }

    // Relock flash interface
    SI32_FLASHCTRL_A_write_flash_key( SI32_FLASHCTRL_0, 0x5A );

    // Wait for flash operation to complete
    while( SI32_FLASHCTRL_A_is_flash_busy(SI32_FLASHCTRL_0 ) );

    if( verify )
    {
        verify_address = (u32*)address;

        for( wc = count; wc != 0; wc-- )
        {
            if (*verify_address != *tmpdata++)
            {
                return 1;
            }

            verify_address++;
        }
    }

    // re-enable interrupts
    //hw_intp_enable();

    return 0;
}


u32 platform_s_flash_write( const void *from, u32 toaddr, u32 size )
{
  flash_key_mask = 0x01;
  return flash_write( toaddr, ( u32 * )from, (size + (4 - 1))/4, 1 );
}

int platform_flash_erase_sector( u32 sector_id )
{
  flash_key_mask = 0x01;
  return flash_erase( sector_id * INTERNAL_FLASH_SECTOR_SIZE + INTERNAL_FLASH_START_ADDRESS, 1) == 0 ? PLATFORM_OK : PLATFORM_ERR;
}

// ****************************************************************************
// USB functions

#if defined( BUILD_USB_CDC )

unsigned platform_get_console_uart( void )
{
  return console_uart_id;
}

void platform_usb_cdc_send( u8 data )
{
    usb_pcb_t *pcb = usb_pcb_get();

    if (!(pcb->flags & (1<<ENUMERATED)))
    {
        return;
    }
  
    usb_buf_write(EP_1, (U8)data);
    ep_write(EP_1);
}

int platform_usb_cdc_recv( s32 timeout )
{
  u8 data = 0;
  usb_pcb_t *pcb = usb_pcb_get();

  if (!(pcb->flags & (1<<ENUMERATED)))
      return -1;

  do {
    if( pcb->fifo[EP_3].len > 0 )
      data = usb_buf_read(EP_3);
  } while( data == 0 && timeout != 0 );

  if( data == 0 )
    return -1;
  else
    return data;
}

#endif

// ****************************************************************************
// Platform specific modules go here
#ifdef PS_LIB_TABLE_NAME


#define MIN_OPT_LEVEL 2
#include "lrodefs.h"
#ifdef ENABLE_PMU
extern const LUA_REG_TYPE pmu_map[];
#endif

#if defined( EXTRA_LIBS_INCLUDE )
#include "extra_libs.h"
#endif

#if defined( SIM3_EXTRA_LIBS_ROM ) && LUA_OPTIMIZE_MEMORY == 2
#define _EXTRAROM( name, openf, table ) extern const LUA_REG_TYPE table[];
SIM3_EXTRA_LIBS_ROM;
#endif

const LUA_REG_TYPE platform_map[] =
{
#if LUA_OPTIMIZE_MEMORY > 0
  { LSTRKEY( "pmu" ), LROVAL( pmu_map ) },
#if defined(SIM3_EXTRA_LIBS_ROM)
#undef _EXTRAROM
#define _EXTRAROM( name, openf, table ) { LSTRKEY( #name ), LROVAL( table ) },
  SIM3_EXTRA_LIBS_ROM
#endif
#endif
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_platform( lua_State *L )
{
#if LUA_OPTIMIZE_MEMORY > 0
  return 0;
#else // #if LUA_OPTIMIZE_MEMORY > 0
  //Turn on aggressive emergency garbage collection
  legc_set_mode( L, EGC_ALWAYS, 20);

  luaL_register( L, PS_LIB_TABLE_NAME, platform_map );

  // Setup the new tables inside platform table
  lua_newtable( L );
  luaL_register( L, NULL, pmu_map );
  lua_setfield( L, -2, "pmu" );

#if defined( SIM3_EXTRA_LIBS_ROM )
#undef _EXTRAROM
#define _EXTRAROM( name, openf, table ) \
  lua_newtable( L ); \
  luaL_register( L, NULL, table ); \
  lua_setfield( L, -2, #name );
#endif

  return 1;
#endif // #if LUA_OPTIMIZE_MEMORY > 0
}

#else // #ifdef ENABLE_PMU

LUALIB_API int luaopen_platform( lua_State *L )
{
  return 0;
}

#endif // #ifdef ENABLE_PMU