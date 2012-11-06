#include <msp430.h>
#include <legacymsp430.h>
#include <stdlib.h>

//#define DOUT BIT6
//#define SCLK BIT5

// Data Output and System clock are still P1.6 and 1.5, though. That's the hardware USI
#define LATP BIT0 // the Latch Pin on the 595, 								pin 0 (port 1)
#define EN	 BIT7 // the pin connected to the 595's Output ENable 7 (port 1)

#define NUMDIGITS	4
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
volatile unsigned char dieType = 6; // remember the kind of dice we last used!
//volatile unsigned char i = 0;
int main(void)
{  
  WDTCTL = WDTPW + WDTHOLD; // stop WD timer

	//Calibrate DCO for 1MHz operation
	// we're looking at 1MHz MCLK
	// 500 kHz SMCLK
	// 500 kHz USI
	// 125 kHz TimerA
  BCSCTL1 = CALBC1_1MHZ;
  DCOCTL = CALDCO_1MHZ;          
  
  BCSCTL2 = DIVS_1; // 500kHz SMCLK		
  
	// initialize all the pins and their resistors
  P1DIR |= 0xFF; // I was going to add all the pin values, but they're all outputs.
	P1SEL =  0x00; // All pins on Port 1 are either GPIO or the modules they're connected to don't care.
	
	P2SEL =  0x00;  // select GPIO functions
	P2DIR =  0x00;	// make the XIN/XOUT pins GPIO Inputs rather than CLK inputs
	
  P2REN =  NEXT + PREV; // enable internal pull resistors on button inputs
	P2IES =  0x00;	// trigger an interrupt for the button pins on a low-to-high transition. Change this to 0xFF in the interrupt routine because we need to see when they stop pressing the button.
	
	P1OUT |= LATP + DIGITS + EN; // make LATP, DIGITx, EN high
	P2OUT =  0x00;	// make the button input resistors pull down
  
	P2IE	=  0xFF;	// enable the interrupts, but hey, TODO: code the interrupt HANDLERS...
	P1OUT &= ~EN; // enable 595 output (by making the EN pin low)

  USICTL0 |= USIPE6 + USIPE5 + USIMST + USIOE; // enable SPI out, SPI SCLK, SPI Master, and data output
  USICTL1 |= USICKPH + USIIE;               // Counter interrupt, flag remains set
  USICKCTL = USIDIV_0 + USISSEL_2; // divide SMCLK by 1 to get the clock for USI
// USI will offset the byte we're shifting out by 3 bits if you are using VLO as ACLK and don't divide ACLK by 2, so 1 -> 3 and 6 -> 11? Strange. Probably a startup problem.

	//USISRL = 0xFF; // start up cleared
  USICTL0 &= ~USISWRST; // allow the USI to function

	srand(3);
	
  CCR0 = 40000; // timer is on SMCLK/2 (7kHz, 2000/(7000 cycles/s * 1s) ~= a period of .286 s.)
	CCR1 = 50000;

	CCTL0 = CCIE;                             // CCRx interrupts enabled
	CCTL1 = CCIE; 
  
  TACTL = TASSEL_2 + MC_2 + ID_3 + TAIE;                  // SMCLK, Cont mode, div SMCLK by 4, ~= 125kHz
  // For VLO Use: TimerA doesn't like to have TASSEL_2 (SMCLK) set, so we just stuck it into ACLK, which comes from the same source (ACLK)

  __bis_SR_register(LPM1_bits + GIE);        // Enter LPM1 w/ interrupt
}

// Timer A0 interrupt service routine
//#pragma vector=TIMERA0_VECTOR
//__interrupt void Timer_A (void) // so much for that, thanks a lot mspgcc
interrupt(TIMERA0_VECTOR) numChange(void)
{
	//i = (i + 1) % 11;
	CCR0 += 40000;
	for(int i = 0; i < NUMDIGITS; i++)
	{
		//digit[num] = (digit[num] + 1) % 10;
		digit[i] = rand(); // 16 bit into a char...
		digit[i] %= 10;		 // something goes BOINK when you do rand() % 10, trust me
	}
}

interrupt(TIMERA1_VECTOR) refresh(void)
{
	CCR1 += 50000;
	P1OUT = DIGITS; // latch goes low (0), cathodes off
	
	USISRL = digitMask[digit[a]]; // write the byte to the USI
  USICNT = 8; // USI shifts out 8 bits (that byte we just entered)

	P1OUT &= ~digitAn[a];	// cathode of selected digit on

	a++;
	a %= NUMDIGITS;		
	P1OUT |= LATP; // latch goes high

	__delay_cycles(3000); // this prevents the display from blending together, but, god, i need to get rid of that for efficiency
}
