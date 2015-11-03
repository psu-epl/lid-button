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

#define LED_B          1
#define LED_R          2
#define LED_G          3
#define PACKET_LENGTH 32

static volatile bool ready_flag; 
int B = 0;
int R = 0;
int G = 0;
int pos = 1;
int clients = 1;
int sync = 1;
uint8_t rssi;
uint8_t packet_data[PACKET_LENGTH];
APP_PWM_INSTANCE(PWM_B,0);
APP_PWM_INSTANCE(PWM_G,2);

void pwm_ready_callback(uint32_t pwm_id)    
{
  ready_flag = true;
}

void timer_init()
{
	NRF_TIMER1->MODE = TIMER_MODE_MODE_Timer;
	NRF_TIMER1->TASKS_CLEAR = 1;
	NRF_TIMER1->PRESCALER = 9;
	NRF_TIMER1->BITMODE = TIMER_BITMODE_BITMODE_16Bit;
	NRF_TIMER1->CC[1] = 15000; //31000 is ~ 1 sec
	NRF_TIMER2->INTENSET = (TIMER_INTENSET_COMPARE1_Enabled << TIMER_INTENSET_COMPARE1_Pos);
	NVIC_EnableIRQ(TIMER1_IRQn);
	NRF_TIMER1->TASKS_START = 1;
	SEGGER_RTT_printf(0, "Syncing to existing network\n");
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

void mesh(int tblu, int tred, int tgrn, int tsig)
{
	
	
	// Do all math in HSV and convert back to RGB/PWM
	int rblu = app_pwm_channel_duty_get(&PWM_B,0);
	int rred = app_pwm_channel_duty_get(&PWM_B,1);
	int rgrn = app_pwm_channel_duty_get(&PWM_G,0);	
	//if(abs(tsig) < 55)
	color((rblu+tblu)/2,(rred+tred)/2,(rgrn+tgrn)/2);	
}
void clock_setup(void)
{
  // start 16Mhz external oscillator
  // required for radio peripheral
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  NRF_CLOCK->TASKS_HFCLKSTART = 1;

  // wait for 16Mhz oscillator to start up
  while( NRF_CLOCK->EVENTS_HFCLKSTARTED == 0 );
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
	//color(B,R,G);
}

bool radio_receive( uint8_t * packet_buffer )
{
  // For this example the function will block until a packet is received
  // The return value will be true if the packet was valid, false otherwise
  
  // Make sure radio is in disabled state
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while( NRF_RADIO->EVENTS_DISABLED == 0 );

  // set pointer to packet data written to using radio peripherals DMA
  NRF_RADIO->PACKETPTR = (uint32_t)packet_buffer;
	NRF_RADIO->SHORTS |= RADIO_SHORTS_ADDRESS_RSSISTART_Msk;
  // The following tasks can be simplified using shorts but for this example we
  // are doing this explicitly for educational value.  Refer to fig 22 in the 
  // reference manual.
	
  // turn on receiver
  NRF_RADIO->EVENTS_READY = 0;
  NRF_RADIO->TASKS_RXEN = 1;
  while( NRF_RADIO->EVENTS_READY == 0 ); // wait for radio to startup ~132us

  // start listening for packet
  NRF_RADIO->EVENTS_END = 0;
	NRF_RADIO->EVENTS_RSSIEND = 0;
	NRF_RADIO->TASKS_RSSISTART = 1;
  NRF_RADIO->TASKS_START = 1;
  while( NRF_RADIO->EVENTS_END == 0 ); // wait for radio to finish decoding a packet
	rssi = (NRF_RADIO->RSSISAMPLE & RADIO_RSSISAMPLE_RSSISAMPLE_Msk);


  // disable radio
	NRF_RADIO->EVENTS_RSSIEND = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while( NRF_RADIO->EVENTS_DISABLED == 0 );

  // Since the packet received could be good or bad we need to check the crc
  if( NRF_RADIO->CRCSTATUS != 0 )
  {
    if(packet_data[3] > clients)
			clients = packet_data[3];
		SEGGER_RTT_printf(0, "%d clients found on network\n",clients);
    return true;
  }
  else
  {
    // packet data is invalid
    return false;
  }
}


void TIMER1_IRQHandler(int sec)
{
	if(sec > 0)
	{
		//SEGGER_RTT_printf(0, "%d\n",sec);
		NRF_TIMER1->TASKS_CLEAR = 1;
		NRF_TIMER1->TASKS_START = 1;
	}
	else
	{
		NRF_TIMER1->TASKS_CLEAR = 1;
		sync = 0;
	}
}

void net_sync (uint8_t * packet_buffer, int sec)
{
	// Make sure radio is in disabled state
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while( NRF_RADIO->EVENTS_DISABLED == 0 );
	
	// set pointer to packet data written to using radio peripherals DMA
	NRF_RADIO->PACKETPTR = (uint32_t)packet_buffer;
	NRF_RADIO->SHORTS |= RADIO_SHORTS_ADDRESS_RSSISTART_Msk;
	
	// turn on receiver
  NRF_RADIO->EVENTS_READY = 0;
  NRF_RADIO->TASKS_RXEN = 1;
  while( NRF_RADIO->EVENTS_READY == 0 ); // wait for radio to startup ~132us
	
	while (sync == 1)
	{
		NRF_TIMER1->TASKS_CAPTURE[0] = 1;
		//SEGGER_RTT_printf(0, "Timer[0]: %d Timer[1]: %d\n",NRF_TIMER1->CC[0],NRF_TIMER1->CC[1]);
		// start listening for packet
		NRF_RADIO->EVENTS_END = 0;
		NRF_RADIO->EVENTS_RSSIEND = 0;
		NRF_RADIO->TASKS_RSSISTART = 1;
		NRF_RADIO->TASKS_START = 1;
		while( NRF_RADIO->EVENTS_END == 0 );
		
		if( NRF_RADIO->CRCSTATUS != 0 )
		{
    //SEGGER_RTT_printf(0, "Target Position: %d\n",packet_data[3]);
		if(packet_data[3]>= pos){
			pos++;
			clients = pos;
		}
		//SEGGER_RTT_printf(0, "My position is %d\n",pos);
		SEGGER_RTT_printf(0, "%d clients found on network\n",clients);
		}
		if(NRF_TIMER1->CC[0] > NRF_TIMER1->CC[1])
		{
			TIMER1_IRQHandler(sec);
			--sec;
		}

	}
	// disable radio
	NRF_RADIO->EVENTS_RSSIEND = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while( NRF_RADIO->EVENTS_DISABLED == 0 );
}

int main(void)
{
	int fail = 0;
	SEGGER_RTT_printf(0, "Starting LED\n");
	led_init();
	SEGGER_RTT_printf(0, "Done!\n");
	SEGGER_RTT_printf(0, "Booting up at ");
	clock_setup();
	radio_setup(1);
	SEGGER_RTT_printf(0, "Done!\n");
	SEGGER_RTT_printf(0, "Starting Timers\n");
	timer_init();
	SEGGER_RTT_printf(0, "Done!\n");
	SEGGER_RTT_printf(0, "Scanning as position %d\n",pos);
	color(60,0,0);
	net_sync(packet_data,3);
	SEGGER_RTT_printf(0, "%d clients found on network\n",clients);
	SEGGER_RTT_printf(0, "Broadcasting as position %d\n",pos);
	int cnt = 1;
	
	while(sync == 0)
	{
		packet_data[0] = app_pwm_channel_duty_get(&PWM_B,0);
		packet_data[1] = app_pwm_channel_duty_get(&PWM_B,1);
		packet_data[2] = app_pwm_channel_duty_get(&PWM_G,0);
		packet_data[3] = pos;
		
		if (clients > 1){
			while (cnt <= clients){
				if(pos == cnt){
					SEGGER_RTT_printf(0, "Transmit as Position %d\n",pos);
					radio_transmit(packet_data);
					cnt++;
				}
				else{
					SEGGER_RTT_printf(0, "Expecting packet from client %d\n",cnt);
					if(radio_receive(packet_data)){
						SEGGER_RTT_printf(0, "Packet received from client %d\n",packet_data[3]);
						if (packet_data[3] == cnt){
							SEGGER_RTT_printf(0, "Packet:%d Cnt:%d\n",packet_data[3],cnt);
							mesh(packet_data[0],packet_data[1],packet_data[2],rssi); //no rssi readings
							cnt++;
						}
						else{
							fail++;
							if(fail == 5)
								pos = cnt;
							SEGGER_RTT_printf(0, "My Position is now %d\n",cnt);
						}
					}
				}
			}
		}
		else{
			SEGGER_RTT_printf(0, "Tx Solo\n");
			radio_transmit(packet_data);
			net_sync(packet_data,1); //sync pos 1
		}
	}
}
