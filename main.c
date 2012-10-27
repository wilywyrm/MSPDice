#include <msp430.h>
#include <legacymsp430.h>

//#define DOUT BIT6
//#define SCLK BIT5

// Data Output and System clock are still P1.6 and 1.5, though. That's the hardware USI
#define LATP BIT0 // the Latch Pin on the 595, 								pin 0 (port 1)
#define EN	 BIT7 // the pin connected to the 595's Output ENable 7 (port 1)

// the pins the P-FETS are hooked up to															(port 1)
const unsigned char digit[4] = {BIT1, BIT2, BIT3, BIT4};
volatile unsigned char a = 0;
/*#define DIGIT1 BIT1
#define DIGIT2 BIT2
#define DIGIT3 BIT3
#define DIGIT4 BIT4*/
#define DIGITS BIT1+BIT2+BIT3+BIT4 // for convenience of addressing

#define NEXT BIT6 // the pin hooked up to the NEXT button, 		pin 6 (port 2)
#define PREV BIT7 // "																					" 7 (port 2)

// 595 hooked up Q0-a, Q1-b,... Q7-DP. Inverted because 595 is sinking current, and bit order is DP, g, f,... a.
const unsigned char digitMask[11] = {0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90, 0xA1}; // last digit is d, as in d20.

//volatile unsigned char show = 0; // brotip: short is NOT a short. It's 16 bits.
volatile unsigned char i = 0;
volatile unsigned char dieType = 6; // remember the kind of dice we last used!

int main(void)
{  
  WDTCTL = WDTPW + WDTHOLD; // stop WD timer

	BCSCTL3 |= LFXT1S_2;                      // LFXT1 = VLO
  IFG1 &= ~OFIFG;                           // Clear OSCFault flag
  __bis_SR_register(SCG1 + SCG0);           // Stop DCO
  
  BCSCTL2 = SELM_2 + SELS;									// MCLK = VLO, SMCLK = VLO
  
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
  USICKCTL = USIDIV_1 + USISSEL_1; // divide clock source by 2 to get the clock for USI and select SMCLK as the source for USI clock
// USI will offset the byte we're shifting out by 3 bits if you don't divide ACLK by 2, so 1 -> 3 and 6 -> 11? Strange. Probably a startup problem.

	USISRL = 0xFF; // start up cleared
  USICTL0 &= ~USISWRST; // allow the USI to function
	
  CCTL0 = CCIE;                             // CCR0 interrupt enabled
  CCR0 = 3000; // timer is on ACLK/2 (7kHz, 2000/(7000 cycles/s * 1s) ~= a period of .286 s.)
  
  TACTL = TASSEL_1 + MC_1 + ID_1;                  // ACLK, Up mode, div ACLK by 2, ~= 7kHz
  // TimerA doesn't like to have TASSEL_2 (SMCLK) set, so we just stuck it into ACLK, which comes from the same source (ACLK)

  __bis_SR_register(LPM3_bits + GIE);        // Enter LPM0 w/ interrupt
}

// Timer A0 interrupt service routine
//#pragma vector=TIMERA0_VECTOR
//__interrupt void Timer_A (void) // so much for that, thanks a lot mspgcc
interrupt(TIMERA0_VECTOR) Timer_A(void)
{    
  P1OUT &= ~LATP; // latch goes low
  USISRL = digitMask[i]; // write the byte to the USI
	
	i++;
	i %= 11;
  
	P1OUT |= DIGITS;	

  USICNT = 8; // USI shifts out 8 bits (that byte we just entered)

	//__delay_cycles(50);
	P1OUT &= ~digit[a];
	
	a++;
	a %= 4;	

  P1OUT |= LATP; // latch goes high and previous instructions give time for USI to shift out
}
