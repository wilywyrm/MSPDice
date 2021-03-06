#include <msp430.h>
#include <legacymsp430.h>
#include <stdlib.h>

//#define DOUT BIT6
//#define SCLK BIT5

// Data Output and System clock are still P1.6 and 1.5, though. That's the hardware USI
#define LATP BIT0 // the Latch Pin on the 595, 								pin 0 (port 1)
#define EN	 BIT7 // the pin connected to the 595's Output ENable 7 (port 1)

#define NUMDIGITS	4

inline void display(unsigned char newDigits[NUMDIGITS]);

// the pins the P-FETS are hooked up to															(port 1)
const unsigned char digitAn[NUMDIGITS] = {BIT1, BIT2, BIT3, BIT4};
#define DIGITS BIT1+BIT2+BIT3+BIT4 // for convenience of addressing

#define NEXT BIT6 // the pin hooked up to the NEXT button, 		pin 6 (port 2)
#define PREV BIT7 // "																					" 7 (port 2)

// 595 hooked up Q0-a, Q1-b,... Q7-DP. Inverted because 595 is sinking current, and bit order is DP, g, f,... a.
const unsigned char digitMask[11] = {0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90, 0xA1}; // last digit is d, as in d20.

// brotip: short is NOT a short. It's 16 bits.
volatile unsigned char digit[NUMDIGITS] = {0,0,0,0};
volatile unsigned char a = 0; // which digit we are refreshing
volatile unsigned char digitDot[NUMDIGITS] = {1,0,1,0}; // 0 means no flash, 1 means flash (on), 2 means flash (off)
volatile unsigned char dieType = 6; // remember the kind of dice we last used!
volatile unsigned int TARCycles = 0;
//volatile unsigned char flashCycles = 0;
volatile unsigned char value = 0;

#define PRESSTHRESHOLD 100			// a click, meaning ++ or --
#define LONGPRESSTHRESHOLD 5000	// a long press, meaning NEXT or PREV
#define HOLDTHRESHOLD  60000		// a holding down of the button, meaning a series of clicks

#define UISTEPS 4 // dX, YdX, randomize eye candy, display result
volatile unsigned char UIStep = 0;

int main(void)
{  
	WDTCTL = WDTPW + WDTHOLD; // stop WD timer

	//Calibrate DCO for 1MHz operation
	// we're looking at 
	// 1MHz MCLK
	// 500 kHz SMCLK
	// 500 kHz USI
	// 125 kHz TimerA
	BCSCTL1 = CALBC1_1MHZ;
	DCOCTL = CALDCO_1MHZ;          
  
	BCSCTL2 = DIVS_1; // 500kHz SMCLK		
  
	// initialize all the pins and their resistors
	P1DIR |= 0xFF; // I was going to add all the pin values, but they're all outputs.
	P1SEL =  0x00; // All pins on Port 1 are either GPIO or the modules they're connected to don't care.
	P1OUT |= LATP + DIGITS + EN; // make LATP, DIGITx, EN high

	P2SEL =  0x00;  // select GPIO functions
	P2DIR =  0x00;	// make the XIN/XOUT pins GPIO Inputs rather than CLK inputs
  P2REN =  NEXT + PREV; // enable internal pull resistors on button inputs
	
	P2OUT &= ~NEXT + ~PREV;	// make the button input resistors pull down
	P2IES &= ~NEXT + ~PREV;	// trigger an interrupt for the button pins on a low-to-high transition. Change this to 0xFF in the interrupt routine because we need to see when they stop pressing the button.
	P2IE	|=  NEXT + PREV;	// enable the interrupts, but hey, TODO: code the interrupt HANDLERS...
	P1OUT &= ~EN; // enable 595 output (by making the EN pin low)

	USICTL0 |= USIPE6 + USIPE5 + USIMST + USIOE; // enable SPI out, SPI SCLK, SPI Master, and data output
	USICTL1 |= USICKPH + USIIE;               // Counter interrupt, flag remains set
	USICKCTL = USIDIV_0 + USISSEL_2; // divide SMCLK by 1 to get the clock for USI
// USI will offset the byte we're shifting out by 3 bits if you are using VLO as ACLK and don't divide ACLK by 2, so 1 -> 3 and 6 -> 11? Strange. Probably a startup problem.

	//USISRL = 0xFF; // start up cleared
	USICTL0 &= ~USISWRST; // allow the USI to function
	
	//srand(seed); 
	// we'll seed using the TAR value when the user presses a button
	//CCR0 = 40000; // timer is on SMCLK/4 (125kHz)
	CCR1 = 300; // i think that's 416Hz

	//CCTL0 = CCIE;                             // CCRx interrupts enabled
	CCTL1 = CCIE; 
  
	TACTL = TASSEL_2 + MC_2 + ID_3 + TAIE;                  // SMCLK, Cont mode, div SMCLK by 4, ~= 125kHz
  // For VLO Use: TimerA doesn't like to have TASSEL_2 (SMCLK) set, so we just stuck it into ACLK, which comes from the same source (ACLK)

	__bis_SR_register(LPM1_bits + GIE);        // Enter LPM1 w/ interrupt
}

inline void display(unsigned char newDigits[NUMDIGITS])
{
	for(unsigned char i = 0; i < NUMDIGITS; i++)
		digit[i] = newDigits[i];
}

interrupt(PORT2_VECTOR) PORT2_ISR(void)
{
	srand(TAR); // seed rand() with the time, as random as we can get
	P2IE &= ~NEXT + ~PREV;
	unsigned char pressTime = 0; // number of button press equivalents we've 

	__delay_cycles(PRESSTHRESHOLD);	 // debounce
	
	switch(P2IFG)
	{
		case BIT6: // if P2.6 (NEXT)...
		{
			//__delay_cycles(PRESSTHRESHOLD);	
			while(P2IN | NEXT && pressTime * PRESSTHRESHOLD < HOLDTHRESHOLD) // if Port 2.6 is still high (button pressed)
			{
				pressTime++;
				__delay_cycles(PRESSTHRESHOLD);
			}
			if(pressTime * PRESSTHRESHOLD >= HOLDTHRESHOLD)
			{ // consider moving this to the main loop, may be too long for an ISR
				while(P2IN | NEXT)
				{
					value++;
					__delay_cycles(PRESSTHRESHOLD / 2);
				}
			} 
			else if(pressTime * PRESSTHRESHOLD >= LONGPRESSTHRESHOLD)
			{
				UIStep = (UIStep + 1) % UISTEPS; // move to the next UI step
			}
			else if(pressTime > 0)
			{
				value++;
			}
		}
		case BIT7: // if P2.7 (PREV)...
		//__delay_cycles(PRESSTHRESHOLD);			
		{
			while(P2IN | PREV && pressTime * PRESSTHRESHOLD < HOLDTHRESHOLD) // if Port 2.7 is still high (button pressed)
			{
				pressTime++;
				__delay_cycles(PRESSTHRESHOLD);
			}
			if(pressTime * PRESSTHRESHOLD >= HOLDTHRESHOLD)
			{ // consider moving this to the main loop, may be too long for an ISR
				while(P2IN | NEXT)
				{
					value--;
					__delay_cycles(PRESSTHRESHOLD / 2);
				}
			} 
			else if(pressTime * PRESSTHRESHOLD >= LONGPRESSTHRESHOLD)
			{
				UIStep = (UIStep - 1) % UISTEPS; // move to the next UI step
			}
			else if(pressTime > 0)
			{
				value--;
			}
		}
	}

	P2IFG &= ~NEXT + PREV; // clear button interrupt flags
	P2IE |= NEXT + PREV; // enable interrupts
}

// Timer A0 interrupt service routine
//#pragma vector=TIMERA0_VECTOR
//__interrupt void Timer_A (void) // so much for that, thanks a lot mspgcc
interrupt(TIMER0_A0_VECTOR) CCR0_ISR(void)
{
}

interrupt(TIMER0_A1_VECTOR) refresh(void)
{
	//unsigned int tar = TAR;
	switch(TA0IV)
	{
		case 0x02: // CCR1
			CCR1 += 300;
			//flashCycles++;
			P1OUT = DIGITS; // latch goes low (0), cathodes off
	
			USISRL = digitMask[digit[a]]; // write the byte to the USI			
			if(digitDot[a] == 1)
				USISRL &= ~BIT7; // the Decimal Point
			USICNT = 8; // USI shifts out 8 bits (that byte we just entered)

			P1OUT &= ~digitAn[a];	// cathode of selected digit on
			/*if(digitFlash[a] == 0)
			{
					P1OUT &= ~digitAn[a];	// cathode of selected digit on
			}
			else if(digitFlash[a] == 1)
			{
					if(flashCycles % 35 == 0)
						digitFlash[a]++;
					P1OUT &= ~digitAn[a];	// cathode of selected digit on
			}
			else if(digitFlash[a] == 2)
			{
					if(flashCycles % 35 == 0)
						digitFlash[a]--;
					P1OUT |= digitAn[a];	// cathode of selected digit off
			}
			*/
			a = (a + 1) % NUMDIGITS;		
			P1OUT |= LATP; // latch goes high
			break;
		case 0x0A: //rerandomize numbah
			//i = (i + 1) % 11;
			//CCR0 += 40000;
			{
			unsigned char x[NUMDIGITS];
	
			for(int i = 0; i < NUMDIGITS; i++)
			{
				x[i] = rand();
				x[i] %= 10;		 // something goes BOINK when you do rand() % 10 in one line, trust me
			}
			display(x); // i think you need to do something special for calling a function in an isr, but i dunno, and this is inline
			TARCycles++;
			}
			break;
	}
}
