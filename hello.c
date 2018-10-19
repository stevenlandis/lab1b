/*
 * hello.c
 *
 *  Created on: Oct 15, 2018
 *      Author: stevenlandis
 */

#include <xil_printf.h>
#include "sevenSeg_new.h"
#include "platform.h"

volatile static int count = 0;
volatile static int run = 0;
int tempCount;
volatile static int digits[8];

// Unused Function because too slow
void setDigits(volatile int* digits, int num) {
	// digits is an array of length 8
	for (int i = 0; i < 8; i++) {
		digits[i] = num % 10;
		num /= 10;
	}
}

// ------------
//  Timer Code
// ------------
#include "xparameters.h"
#include "xtmrctr.h"
#include "xintc.h"
#include "xgpio.h" // for the button driver
#include "mb_interface.h"

// 10000 cycles is 0.0001 seconds
// which is the smallest digit on the seven segment display:
// ????.????
// Example: 1.2345 seconds displayed as 0001.2345
#define RESET_VALUE 10000*1

#define UP_BTN_MASK 1
#define DOWN_BTN_MASK 8
#define LEFT_BTN_MASK 2
#define RIGHT_BTN_MASK 4

#define STOPWATCH_MODE

XIntc sys_intc;
XTmrCtr sys_tmrctr;
XGpio sys_btns;
void timerHandler() {
	// This is the interrupt handler function
	int ControlStatusReg;
	/*
	 * Read the new Control/Status Register content.
	 */
	ControlStatusReg = XTimerCtr_ReadReg(sys_tmrctr.BaseAddress, 0, XTC_TCSR_OFFSET);

	// xil_printf("T\n");
	// XGpio_DiscreteWrite(&led,1,count);
	count+= run;
	// setDigits(digits, count);
	/*
	 * Acknowledge the interrupt by clearing the interrupt
	 * bit in the timer control status register
	 */
	XTmrCtr_WriteReg(sys_tmrctr.BaseAddress, 0, XTC_TCSR_OFFSET, ControlStatusReg |XTC_CSR_INT_OCCURED_MASK);
}

void btnHandler() {
	// XGpio *GpioPtr = (XGpio *)CallbackRef;

	u32 buttonData;

	buttonData = XGpio_DiscreteRead(&sys_btns, 1);

//	if (buttonData & UP_BTN_MASK)
//		xil_printf("Pressed Up\n");

#ifdef STOPWATCH_MODE
	//stopwatch behavior
	if (buttonData & LEFT_BTN_MASK) {
		if(run==0){
			count = 0;
			run = 1;
		}
		else{
			run = 0;
		}
	}

#else
	if (buttonData & DOWN_BTN_MASK)
		run = 0;

	if (buttonData & LEFT_BTN_MASK)
		run = 1;

	if (buttonData & RIGHT_BTN_MASK)
		count = 0;
#endif

	/* Clear the Interrupt */
	XGpio_InterruptClear(&sys_btns, 0xFFFFFFFF);
}

int startInterrupts(XInterruptHandler timerHandler, XInterruptHandler btnHandler) {
	int Status = XST_SUCCESS;

	// Initialize buttons
	Status = XGpio_Initialize(&sys_btns, XPAR_BTNS_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	// Initialize interrupt controller
	Status = XIntc_Initialize(&sys_intc, XPAR_MICROBLAZE_0_AXI_INTC_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	// Connect timer to interrupt subsystem
	Status = XIntc_Connect(
		&sys_intc,
		XPAR_MICROBLAZE_0_AXI_INTC_AXI_TIMER_0_INTERRUPT_INTR,
		timerHandler,
		&sys_tmrctr
	);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	// Enable interrupt for the timer counter
	XIntc_Enable(&sys_intc, XPAR_MICROBLAZE_0_AXI_INTC_AXI_TIMER_0_INTERRUPT_INTR);

	// Connect buttons to interrupt subsystem
	Status = XIntc_Connect(
		&sys_intc,
		XPAR_MICROBLAZE_0_AXI_INTC_BTNS_IP2INTC_IRPT_INTR,
		btnHandler,
		&sys_btns
	);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	// Enable interrupt for the button counter
	XIntc_Enable(&sys_intc, XPAR_MICROBLAZE_0_AXI_INTC_BTNS_IP2INTC_IRPT_INTR);

	// Start the interrupt controller
	Status = XIntc_Start(&sys_intc, XIN_REAL_MODE);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	XGpio_InterruptEnable(&sys_btns, 1);
	XGpio_InterruptGlobalEnable(&sys_btns);

	// Initialize the timer counter
	Status = XTmrCtr_Initialize(&sys_tmrctr, XPAR_MICROBLAZE_0_AXI_INTC_DEVICE_ID);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	/*
	 * Enable the interrupt of the timer counter so interrupts will occur
	 * and use auto reload mode such that the timer counter will reload
	 * itself automatically and continue repeatedly, without this option
	 * it would expire once only
	 */
	XTmrCtr_SetOptions(&sys_tmrctr, 0, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);

	/*
	 * Set a reset value for the timer counter such that it will expire
	 * earlier than letting it roll over from 0, the reset value is loaded
	 * into the timer counter when it is started
	 */
	XTmrCtr_SetResetValue(&sys_tmrctr, 0, 0xFFFFFFFF-RESET_VALUE);

	/*
	 * Start the timer counter such that it's incrementing by default,
	 * then wait for it to timeout a number of times
	 */
	XTmrCtr_Start(&sys_tmrctr, 0);

	// set button direction
	XGpio_SetDataDirection(&sys_btns, 1, 0xFFFFFFFF);

	/*
	 * Register the intc device driver’s handler with the Stand-alone
	 * software platform’s interrupt table
	 */
	microblaze_register_handler(
			(XInterruptHandler)XIntc_DeviceInterruptHandler,
		(void*)XPAR_MICROBLAZE_0_AXI_INTC_DEVICE_ID
	);

	/*
	 * Enable interrupts on MicroBlaze
	 */
	microblaze_enable_interrupts();

	xil_printf("Interrupts enabled!\r\n");

	return Status;
}

int main(void)
{
	init_platform();
	xil_printf("Starting\n");

	startInterrupts(timerHandler, btnHandler);

	while (1) {
		// Copy current count to tempCount
		// so extract the digits
		tempCount = count;

		// Loop through all digits on display
		for (int i = 0; i < 8; i++) {
			sevenseg_draw_digit(i,tempCount%10);
			tempCount /= 10;
		}
	}

	// Control flow never reaches here

	xil_printf("Done\n");
	return 0;
}
