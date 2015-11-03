#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "boards.h"
#include "app_pwm.h"
#include "app_timer.h"
#include <stdlib.h>
#include "SEGGER_RTT.h"

#define LED_B		1
#define LED_R		2
#define LED_G		3
#define PACKET_LENGTH 32

static volatile bool ready_flag; 
int B = 0;
int R = 0;
int G = 0;
int pos = 0;
uint8_t rssi;
uint8_t packet_data[PACKET_LENGTH];
APP_PWM_INSTANCE(PWM_B,0);
APP_PWM_INSTANCE(PWM_R,1);
APP_PWM_INSTANCE(PWM_G,2);

#define LED_LIST {LED_B, LED_R, LED_G}

void pwm_ready_callback(uint32_t pwm_id)    
{
  ready_flag = true;
}

void color(int blu,int red, int grn)
{
	int clr = 0;
	int tblu, tred, tgrn;
	while(clr == 0)
	{
		app_pwm_channel_duty_set(&PWM_B, 0, blu);
		app_pwm_channel_duty_set(&PWM_G, 0, grn);
		app_pwm_channel_duty_set(&PWM_B, 1, red);
		tblu = app_pwm_channel_duty_get(&PWM_B,0);
		tred = app_pwm_channel_duty_get(&PWM_B,1);
		tgrn = app_pwm_channel_duty_get(&PWM_G,0);
		if(tblu == blu && tred == red && tgrn == grn)
			clr = 1;
	}
	SEGGER_RTT_printf(0, "Color is set to Blue:%d Red:%d Green:%d\n",blu,red,grn);
}

void led_init()
{
	app_pwm_config_t pwm_cfg_B = APP_PWM_DEFAULT_CONFIG_2CH(5000, LED_B, LED_R);
	app_pwm_config_t pwm_cfg_G = APP_PWM_DEFAULT_CONFIG_1CH(5000, LED_G);
	app_pwm_init(&PWM_B, &pwm_cfg_B,pwm_ready_callback);
	app_pwm_init(&PWM_G, &pwm_cfg_G,pwm_ready_callback);
	nrf_gpio_range_cfg_output(1,3);
	app_pwm_enable(&PWM_B);
	app_pwm_enable(&PWM_G);
	color(100,0,0);
	nrf_delay_ms(150);
	color(0,0,100);
	nrf_delay_ms(150);
	color(0,100,0);
	nrf_delay_ms(150);
	color(0,0,0);
}

void blink(int blu,int red, int grn, int cnt, int spd)
{
	while(cnt > 0)
	{
		color(blu,red,grn);
		nrf_delay_ms(spd);
		color(0,0,0);
		nrf_delay_ms(spd);
		cnt--;
	}
		
}

void clock_setup(void)
{
  // start 16Mhz external oscillator
  // required for radio peripheral
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
	SEGGER_RTT_printf(0, "1st step");
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  SEGGER_RTT_printf(0, "2nd step");
  // wait for 16Mhz oscillator to start up
  while( NRF_CLOCK->EVENTS_HFCLKSTARTED == 0 );
	SEGGER_RTT_printf(0, "3rd step");
}

void radio_setup(uint8_t channel)
{
  NRF_RADIO->POWER        = 1; // enable radio peripheral subsystem

  NRF_RADIO->INTENCLR     = 0xffffffff; // clear all radio interrupts
  
  // Shorts provide a way to automatically trigger a task in response to an event
  // causing a state transision to happen in the radio.  Refer to the state diagram 
  // (fig 22) in the reference manual.  For this example we will not use shorts.
  NRF_RADIO->SHORTS       = 0;

  NRF_RADIO->TXPOWER      = 0; // 0 dBm
  NRF_RADIO->MODE         = RADIO_MODE_MODE_Nrf_2Mbit;

  // Set packet parameters for S0, LENGTH, and S1
  // for custom protocols with fixed packet lengths these are not needed
  NRF_RADIO->PCNF0        = 0;

  // Set packet parameters for length and endianess
  // in this example it is a staticly defined packet length with a 3 byte 
  // address (2 base, 1 prefix)
  NRF_RADIO->PCNF1        = (PACKET_LENGTH << RADIO_PCNF1_MAXLEN_Pos) |
                            (PACKET_LENGTH << RADIO_PCNF1_STATLEN_Pos) |
                            (2 << RADIO_PCNF1_BALEN_Pos);

  // Set the base address and prefix for the radio.  In this example all devices 
  // will have the same address allowing packet transmissions to be broadcast to 
  // multiple devices.  If you were employing a point to point protocol these 
  // addresses may be unique.
  //
  // NOTE: it is recomended to not use 0 or 0xffffffff since they have no bit 
  // transitions making it harder for the radio to distinquish valid packets from 
  // noise.  It is also not recomended to use 0xaa or 0x55 since they can be seen as a continuation of the preamble and cause problems.  This may not be explicitly 
  // discussed in the nrf51 docs but is mentioned in previous nrf chip docs that 
  // use the same on air protocol structure.
  NRF_RADIO->BASE0        = 0x4c494c49; // ascii LILI
  NRF_RADIO->BASE1        = 0x4c494c49;
  NRF_RADIO->PREFIX0      = 0x44444444; // ascii DDDD, combined make LID ;)
  NRF_RADIO->PREFIX1      = 0x44444444;
  NRF_RADIO->TXADDRESS    = 0; // select which address we are using for TX
  NRF_RADIO->RXADDRESSES  = 0x00000001; // only use first address for RX

  // setup crc polynomial and size
  NRF_RADIO->CRCCNF       = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos);
  NRF_RADIO->CRCPOLY      = 0x1021; // x^12^x^5+1
  NRF_RADIO->CRCINIT      = 0xffff;

  NRF_RADIO->FREQUENCY    = channel; // 2400 Mhz + channel in 1Mhz steps
	SEGGER_RTT_printf(0, "240%d Mhz\n", channel);
}

void radio_transmit( uint8_t * packet_buffer )
{
  //Get current LED data
	B = app_pwm_channel_duty_get(&PWM_B,0);
	R = app_pwm_channel_duty_get(&PWM_B,1);
	G = app_pwm_channel_duty_get(&PWM_G,0);
	// For this example the function will block until the packet has completed transmitting
  // Make sure radio is in disabled state
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while( NRF_RADIO->EVENTS_DISABLED == 0 );

  // set pointer to packet data read using radio peripherals DMA
  NRF_RADIO->PACKETPTR = (uint32_t)packet_buffer;

  // The following tasks can be simplified using shorts but for this example we
  // are doing this explicitly for educational value.  Refer to fig 22 in the 
  // reference manual.

  // turn on transmitter
  NRF_RADIO->EVENTS_READY = 0;
  NRF_RADIO->TASKS_TXEN = 1;
  while( NRF_RADIO->EVENTS_READY == 0 ); // wait for radio to startup ~132us

  // start packet transmission
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->TASKS_START = 1;
  while( NRF_RADIO->EVENTS_END == 0 ); // wait for radio to finish sending packet

  // disable radio
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while( NRF_RADIO->EVENTS_DISABLED == 0 );
	//Return to previous color
	color(B,R,G);
}

int main(void)
{
	SEGGER_RTT_printf(0, "Starting LED\n");
	led_init();
	color(100,0,0);
	nrf_delay_ms(1000);
	color(0,100,0);
	nrf_delay_ms(1000);
	color(0,0,100);
  nrf_delay_ms(1000);
	SEGGER_RTT_printf(0, "Starting Clock\n ");
	clock_setup();
	SEGGER_RTT_printf(0, "Clock is Enabled\n ");
	radio_setup(1);
	SEGGER_RTT_printf(0, " Radio set up Done!\n");
	color(100,0,0);
	nrf_delay_ms(500);
	
}