/*
 * Startup.cpp
 *
 *  Created on: 2 Aug 2020
 *      Author: David
 */

#undef __SOFTFP__			// should not be defined, but Eclipse thinks it is

#include <CoreIO.h>
#include <hri_oscctrl_e54.h>
#include <hri_nvmctrl_e54.h>

#ifndef FREQM_GCLK_ID_REF
# define FREQM_GCLK_ID_REF		(6)			// this definition is missing from the DFP
#endif

// Symbols defined by the linker
extern uint32_t _sfixed;
//extern uint32_t _efixed;
extern uint32_t _etext;
extern uint32_t _srelocate;
extern uint32_t _erelocate;
extern uint32_t _szero;
extern uint32_t _ezero;
//extern uint32_t _sstack;

#if SUPPORT_CAN
extern uint32_t _sCanMessage;
extern uint32_t _eCanMessage;
#endif

extern "C" void __libc_init_array() noexcept;

// Forward declaration
static void InitClocks() noexcept;

/**
 * \brief This is the code that gets called on processor reset.
 * To initialize the device, and call the main() routine.
 */
extern "C" [[noreturn]] void Reset_Handler() noexcept
{
	uint32_t *pSrc = &_etext;
	uint32_t *pDest = &_srelocate;

	if (pSrc != pDest)
	{
		for (; pDest < &_erelocate; )
		{
			*pDest++ = *pSrc++;
		}
	}

	// Clear the zero segment
	for (pDest = &_szero; pDest < &_ezero; )
	{
		*pDest++ = 0;
	}

#if SUPPORT_CAN
	// Clear the CAN message buffer segment
	for (pDest = &_sCanMessage; pDest < &_eCanMessage; )
	{
		*pDest++ = 0;
	}
#endif

	// Set the vector table base address
	pSrc = (uint32_t *) & _sfixed;
	SCB->VTOR = ((uint32_t) pSrc & SCB_VTOR_TBLOFF_Msk);

	static_assert(__FPU_USED);
	// Enable FPU
	SCB->CPACR |= (0xFu << 20);
	__DSB();
	__ISB();

	// Initialize the C library
	__libc_init_array();

	// Set up the standard clocks
	InitClocks();

	// Temporarily set up systick so that delayMicroseconds works
	SysTick->LOAD = ((SystemCoreClockFreq/1000) - 1) << SysTick_LOAD_RELOAD_Pos;
	SysTick->CTRL = (1 << SysTick_CTRL_ENABLE_Pos) | (1 << SysTick_CTRL_CLKSOURCE_Pos);

	// Initialise application, which includes setting up any additional clocks
	AppInit();

	// Run the application
	AppMain();

	while (1) { }
}

static void InitClocks() noexcept
{
#if 1
	hri_nvmctrl_set_CTRLA_RWS_bf(NVMCTRL, 0);				// rely on the AUTOWS bit
#else
	hri_nvmctrl_set_CTRLA_RWS_bf(NVMCTRL, 6);				// need 6 wait states @ 120MHz for EFP part numbers
	hri_nvmctrl_clear_CTRLA_AUTOWS_bit(NVMCTRL);			// clear the auto WS bit
#endif

	// We don't know which bootloader we entered from, and they use different clocks, so configure the clocks.
	// First reset the generic clock generator. This sets all clock generators to default values and the CPU clock to the 48MHz DFLL output.
	GCLK->CTRLA.reg = GCLK_CTRLA_SWRST;
	while ((GCLK->CTRLA.reg & GCLK_CTRLA_SWRST) != 0) { }

	// Disable DPLL0 so that we can reprogram it
	OSCCTRL->Dpll[0].DPLLCTRLA.bit.ENABLE = 0;
	while (OSCCTRL->Dpll[0].DPLLSYNCBUSY.bit.ENABLE) { }

	// Also disable DPLL1 in case it was used by the bootloader
	OSCCTRL->Dpll[1].DPLLCTRLA.bit.ENABLE = 0;
	while (OSCCTRL->Dpll[1].DPLLSYNCBUSY.bit.ENABLE) { }

	// Now it's safe to configure the clocks
	// Initialise 32kHz oscillator
	const uint16_t calib = hri_osc32kctrl_read_OSCULP32K_CALIB_bf(OSC32KCTRL);
	hri_osc32kctrl_write_OSCULP32K_reg(OSC32KCTRL, OSC32KCTRL_OSCULP32K_CALIB(calib));

	// Get the XOSC details from the application
	unsigned int xoscFrequency = AppGetXoscFrequency();
	const unsigned int xoscNumber = AppGetXoscNumber();
	const uint32_t xoscReadyBit = 1ul << (OSCCTRL_STATUS_XOSCRDY0_Pos + xoscNumber);	// The XOSCRDY1 bit is one position higher than the XOSCRDY0 bit

	if (xoscFrequency == 0)
	{
		// Start up the crystal oscillator with high gain to guaranteed operation, so that we can measure its frequency
		hri_oscctrl_write_XOSCCTRL_reg(OSCCTRL, xoscNumber,
				  OSCCTRL_XOSCCTRL_CFDPRESC(3)
				| OSCCTRL_XOSCCTRL_STARTUP(0)
				| (0 << OSCCTRL_XOSCCTRL_SWBEN_Pos)
				| (0 << OSCCTRL_XOSCCTRL_CFDEN_Pos)
				| (0 << OSCCTRL_XOSCCTRL_ENALC_Pos)
				| OSCCTRL_XOSCCTRL_IMULT(6)
				| OSCCTRL_XOSCCTRL_IPTAT(3)
				| (0 << OSCCTRL_XOSCCTRL_LOWBUFGAIN_Pos)
				| (0 << OSCCTRL_XOSCCTRL_ONDEMAND_Pos)
				| (0 << OSCCTRL_XOSCCTRL_RUNSTDBY_Pos)
				| (1 << OSCCTRL_XOSCCTRL_XTALEN_Pos)
				| (1 << OSCCTRL_XOSCCTRL_ENABLE_Pos));

		while ((OSCCTRL->STATUS.reg & xoscReadyBit) == 0) { }

		// Set up clocks for the frequency meter. GCLK0 is already driven from the 48MHz RC oscillator.
		MCLK->APBAMASK.reg |= MCLK_APBAMASK_FREQM;
		hri_gclk_write_PCHCTRL_reg(GCLK, FREQM_GCLK_ID_REF, 0 | GCLK_PCHCTRL_CHEN);

		// Set up GCLK 1 to be driven from the crystal oscillator
		hri_gclk_write_GENCTRL_reg(GCLK, 1,
				  GCLK_GENCTRL_DIV(1) | (0 << GCLK_GENCTRL_RUNSTDBY_Pos)
				| (0 << GCLK_GENCTRL_DIVSEL_Pos) | (0 << GCLK_GENCTRL_OE_Pos)
				| (0 << GCLK_GENCTRL_OOV_Pos) | (0 << GCLK_GENCTRL_IDC_Pos)
				| GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC(GCLK_GENCTRL_SRC_XOSC0_Val + xoscNumber));
		hri_gclk_write_PCHCTRL_reg(GCLK, FREQM_GCLK_ID_MSR, 1 | GCLK_PCHCTRL_CHEN);

		FREQM->CTRLA.reg = FREQM_CTRLA_SWRST;
		while (FREQM->SYNCBUSY.bit.SWRST) { }

		FREQM->CFGA.reg = 240;									// count for 240 cycles of the 48MHz reference clock i.e. 5us
		FREQM->CTRLA.reg = FREQM_CTRLA_ENABLE;
		while (FREQM->SYNCBUSY.bit.ENABLE) { }

		FREQM->STATUS.reg = FREQM_STATUS_OVF;					// clear overflow status
		FREQM->CTRLB.reg = FREQM_CTRLB_START;					// start counting
		while (FREQM->STATUS.bit.BUSY) { }						// wait until finished

		const uint32_t freq = FREQM->VALUE.reg & 0x00FFFFFF;	// get the number of crystal oscillator cycles in 10us
		const bool overflowed = FREQM->STATUS.bit.OVF;

		// Turn off the temporary GCLK and the frequency meter to save power
		GCLK->GENCTRL[1].reg = 0;
		FREQM->CTRLA.reg = 0;
		MCLK->APBAMASK.reg &= ~(MCLK_APBAMASK_FREQM);
		hri_gclk_write_PCHCTRL_reg(GCLK, FREQM_GCLK_ID_REF, 0);
		hri_gclk_write_PCHCTRL_reg(GCLK, FREQM_GCLK_ID_MSR, 0);

		// Expected frequencies are 12, 16 and 25MHz. We allow a +/-12% tolerance for the 48MHz oscillator so that the 12 and 16MHz bands don't overlap.
		if (!overflowed && freq >= 52 && freq <= 68)
		{
			xoscFrequency = 12;
		}
		else if (!overflowed && freq >= 70 && freq <= 90)
		{
			xoscFrequency = 16;
		}
		else if (!overflowed && freq >= 110 && freq <= 140)
		{
			xoscFrequency = 25;
		}
		else
		{
			Reset();
		}
	}

	// Initialise the XOSC
	const uint32_t imult = (xoscFrequency > 24) ? 6
							: (xoscFrequency > 16) ? 5
								: 4;						// we assume the crystal frequency is always >8MHz
	const uint32_t iptat = 3;								// we assume the crystal frequency is always >8MHz
	hri_oscctrl_write_XOSCCTRL_reg(OSCCTRL, xoscNumber,
			  OSCCTRL_XOSCCTRL_CFDPRESC(3)
			| OSCCTRL_XOSCCTRL_STARTUP(0)
			| (0 << OSCCTRL_XOSCCTRL_SWBEN_Pos)
			| (0 << OSCCTRL_XOSCCTRL_CFDEN_Pos)
			| (0 << OSCCTRL_XOSCCTRL_ENALC_Pos)
			| OSCCTRL_XOSCCTRL_IMULT(imult)
			| OSCCTRL_XOSCCTRL_IPTAT(iptat)
			| (0 << OSCCTRL_XOSCCTRL_LOWBUFGAIN_Pos)
			| (0 << OSCCTRL_XOSCCTRL_ONDEMAND_Pos)
			| (0 << OSCCTRL_XOSCCTRL_RUNSTDBY_Pos)
			| (1 << OSCCTRL_XOSCCTRL_XTALEN_Pos)
			| (1 << OSCCTRL_XOSCCTRL_ENABLE_Pos));

	while ((OSCCTRL->STATUS.reg & xoscReadyBit) == 0) { }

	// Initialise MCLK
	hri_mclk_write_CPUDIV_reg(MCLK, MCLK_CPUDIV_DIV(MCLK_CPUDIV_DIV_DIV1_Val));

	// Initialise FDPLL0
	// We can divide the crystal oscillator by any even number up to 512 to get an input in the range 32kHz to 3MHz for the DPLL
	// The errata says that at 400kHz and below we can get false unlock indications. So we try to use 1MHz or above.
	uint32_t multiplier;
	uint32_t divisor;										// must be even
	if ((xoscFrequency & 1) == 0)
	{
		divisor = xoscFrequency;							// use 1MHz
		multiplier = 120;
	}
	else if ((xoscFrequency % 5) == 0)						// e.g. 25MHz as used on Duet 3 Mini 5+
	{
		divisor = 2 * (xoscFrequency/5);					// use 2.5MHz
		multiplier = (2 * 120)/5;
	}
	else
	{
		divisor = 2 * xoscFrequency;
		multiplier = 2 * 120;
	}

	hri_oscctrl_write_DPLLRATIO_reg(OSCCTRL, 0,
			  OSCCTRL_DPLLRATIO_LDRFRAC(0)
			| OSCCTRL_DPLLRATIO_LDR(multiplier - 1));
	hri_oscctrl_write_DPLLCTRLB_reg(OSCCTRL, 0,
			  OSCCTRL_DPLLCTRLB_DIV(divisor/2 - 1)
			| (0 << OSCCTRL_DPLLCTRLB_DCOEN_Pos)
			| OSCCTRL_DPLLCTRLB_DCOFILTER(0)
			| (0 << OSCCTRL_DPLLCTRLB_LBYPASS_Pos)
			| OSCCTRL_DPLLCTRLB_LTIME(0)
			| OSCCTRL_DPLLCTRLB_REFCLK(2u + xoscNumber)		// source is XOSC0 or XOSC1
			| (0 << OSCCTRL_DPLLCTRLB_WUF_Pos)
			| OSCCTRL_DPLLCTRLB_FILTER(0));
	hri_oscctrl_write_DPLLCTRLA_reg(OSCCTRL, 0,
			  (0 << OSCCTRL_DPLLCTRLA_RUNSTDBY_Pos)
			| (1 << OSCCTRL_DPLLCTRLA_ENABLE_Pos));

	while (!(hri_oscctrl_get_DPLLSTATUS_LOCK_bit(OSCCTRL, 0) || hri_oscctrl_get_DPLLSTATUS_CLKRDY_bit(OSCCTRL, 0))) { }

	// We must initialise GCLKs 0 and 1 before we touch the DFLL:
	// - GCLK0 is the CPU clock and defaults to the DFLL
	// - GCLK1 is used as the reference when we reprogram the DFLL

	// GCLK0: from FDPLL0 direct
	hri_gclk_write_GENCTRL_reg(GCLK, 0,
			  GCLK_GENCTRL_DIV(1) | (0 << GCLK_GENCTRL_RUNSTDBY_Pos)
			| (0 << GCLK_GENCTRL_DIVSEL_Pos) | (0 << GCLK_GENCTRL_OE_Pos)
			| (0 << GCLK_GENCTRL_OOV_Pos) | (0 << GCLK_GENCTRL_IDC_Pos)
			| GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_DPLL0);

	// GCLK1: XOSC0 divided by 32 * frequency_in_MHz to give 31250Hz
	hri_gclk_write_GENCTRL_reg(GCLK, 1,
			  GCLK_GENCTRL_DIV(32 * xoscFrequency) | (0 << GCLK_GENCTRL_RUNSTDBY_Pos)
			| (0 << GCLK_GENCTRL_DIVSEL_Pos) | (0 << GCLK_GENCTRL_OE_Pos)
			| (0 << GCLK_GENCTRL_OOV_Pos) | (0 << GCLK_GENCTRL_IDC_Pos)
			| GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC(GCLK_GENCTRL_SRC_XOSC0_Val + xoscNumber));

	// Initialise DFLL48M in closed loop mode
	hri_gclk_write_PCHCTRL_reg(GCLK, OSCCTRL_GCLK_ID_DFLL48, GCLK_PCHCTRL_GEN_GCLK1_Val | GCLK_PCHCTRL_CHEN);		// set GCLK1 as DFLL reference
	hri_oscctrl_write_DFLLCTRLA_reg(OSCCTRL, 0);

	hri_oscctrl_write_DFLLMUL_reg(OSCCTRL, OSCCTRL_DFLLMUL_CSTEP(4) | OSCCTRL_DFLLMUL_FSTEP(4) | OSCCTRL_DFLLMUL_MUL(48 * 32));
	while (hri_oscctrl_get_DFLLSYNC_DFLLMUL_bit(OSCCTRL)) { }

	hri_oscctrl_write_DFLLCTRLB_reg(OSCCTRL, 0);
	while (hri_oscctrl_get_DFLLSYNC_DFLLCTRLB_bit(OSCCTRL)) { }

	hri_oscctrl_write_DFLLCTRLA_reg(OSCCTRL, (0 << OSCCTRL_DFLLCTRLA_RUNSTDBY_Pos) | OSCCTRL_DFLLCTRLA_ENABLE);
	while (hri_oscctrl_get_DFLLSYNC_ENABLE_bit(OSCCTRL)) { }

	hri_oscctrl_write_DFLLVAL_reg(OSCCTRL, hri_oscctrl_read_DFLLVAL_reg(OSCCTRL));
	while (hri_oscctrl_get_DFLLSYNC_DFLLVAL_bit(OSCCTRL)) { }

	hri_oscctrl_write_DFLLCTRLB_reg(OSCCTRL,
			  (0 << OSCCTRL_DFLLCTRLB_WAITLOCK_Pos) | (0 << OSCCTRL_DFLLCTRLB_BPLCKC_Pos)
			| (0 << OSCCTRL_DFLLCTRLB_QLDIS_Pos) | (0 << OSCCTRL_DFLLCTRLB_CCDIS_Pos)
			| (0 << OSCCTRL_DFLLCTRLB_USBCRM_Pos) | (0 << OSCCTRL_DFLLCTRLB_LLAW_Pos)
			| (0 << OSCCTRL_DFLLCTRLB_STABLE_Pos) | (1u << OSCCTRL_DFLLCTRLB_MODE_Pos));
	while (hri_oscctrl_get_DFLLSYNC_DFLLCTRLB_bit(OSCCTRL)) { }

	// Initialise the other GCLKs
	// GCLK3: FDPLL0 divided by 2, 60MHz for peripherals that need less than 120MHz
	hri_gclk_write_GENCTRL_reg(GCLK, 3,
			  GCLK_GENCTRL_DIV(2) | (0 << GCLK_GENCTRL_RUNSTDBY_Pos)
			| (0 << GCLK_GENCTRL_DIVSEL_Pos) | (0 << GCLK_GENCTRL_OE_Pos)
			| (0 << GCLK_GENCTRL_OOV_Pos) | (0 << GCLK_GENCTRL_IDC_Pos)
			| GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_DPLL0);

	// GCLK4: DFLL48M for CAN and step timer
	hri_gclk_write_GENCTRL_reg(GCLK, 4,
			  GCLK_GENCTRL_DIV(1) | (0 << GCLK_GENCTRL_RUNSTDBY_Pos)
			| (0 << GCLK_GENCTRL_DIVSEL_Pos) | (0 << GCLK_GENCTRL_OE_Pos)
			| (0 << GCLK_GENCTRL_OOV_Pos) | (0 << GCLK_GENCTRL_IDC_Pos)
			| GCLK_GENCTRL_GENEN | GCLK_GENCTRL_SRC_DFLL_Val);

	// Set up system clock frequency variable for FreeRTOS
	SystemCoreClock = SystemCoreClockFreq;
}

// End
