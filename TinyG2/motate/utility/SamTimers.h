/*
utility/SamTimers.hpp - Library for the Arduino-compatible Motate system
http://tinkerin.gs/

Copyright (c) 2013 Robert Giseburt

This file is part of the Motate Library.

This file ("the software") is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2 as published by the
Free Software Foundation. You should have received a copy of the GNU General Public
License, version 2 along with the software. If not, see <http://www.gnu.org/licenses/>.

As a special exception, you may use this file as part of a software library without
restriction. Specifically, if other files instantiate templates or use macros or
inline functions from this file, or you compile this file and link it with  other
files to produce an executable, this file does not by itself cause the resulting
executable to be covered by the GNU General Public License. This exception does not
however invalidate any other reasons why the executable file might be covered by the
GNU General Public License.

THE SOFTWARE IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY
WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SAMTIMERS_H_ONCE
#define SAMTIMERS_H_ONCE

#include "sam.h"

/* Sam hardware timers have three channels each. Each channel is actually an
* independent timer, so we have a little nomenclature clash.
*
* Sam Timer != Motate::Timer!!!
*
* A Sam Timer CHANNEL is actually the portion that a Motate::Timer controls
* direcly. Each SAM CHANNEL has two Motate:Timers (A and B).
* (Actually, the Quadrature Decoder and Block Control can mix them up some,
* but we ignore that.)
* So, for the Sam, we have to maintain the same interface, and treat each
* channel as an independent timer.
*/

namespace Motate {
	enum TimerMode {
		/* InputCapture mode (WAVE = 0) */
		kTimerInputCapture         = 0,
		/* InputCapture mode (WAVE = 0), counts up to RC */
		kTimerInputCaptureToMatch  = 0 | TC_CMR_CPCTRG,

		/* Waveform select, Up to 0xFFFFFFFF */
		kTimerUp            = TC_CMR_WAVE | TC_CMR_WAVSEL_UP,
		/* Waveform select, Up to TOP (RC) */
		kTimerUpToMatch     = TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC,
		/* Waveform select, Up to 0xFFFFFFFF, then Down */
		kTimerUpDown        = TC_CMR_WAVE | TC_CMR_WAVSEL_UPDOWN,
		/* Waveform select, Up to TOP (RC), then Down */
		kTimerUpDownToMatch = TC_CMR_WAVE | TC_CMR_WAVSEL_UPDOWN_RC,
	};

	enum TimerChannelOutputOptions {
		kOutputDisconnected = 0,
		kToggleOnMatch      = 1,
		kClearOnMatch       = 2,
		kSetOnMatch         = 3,
	};

	enum TimerChannelInterruptOptions {
		kInterruptsOff       = 0,
		kInterruptOnMatchA   = 1<<1,
		kInterruptOnMatchB   = 1<<2,
		/* Interrupt on overflow could be a match C as well. */
		kInterruptOnOverflow = 1<<3,
	};

	enum TimerErrorCodes {
		kFrequencyUnattainable = -1,
	};

    typedef const uint8_t timer_number;
    
	template <uint8_t timerNum>
	struct Timer {

		// NOTE: Notice! The *pointers* are const, not the *values*.
		static Tc * const tc();
		static TcChannel * const tcChan();
		static const uint32_t peripheralId(); // ID_TC0 .. ID_TC8
		static const IRQn_Type tcIRQ();

		/********************************************************************
		**                          WARNING                                **
		** WARNING: Sam channels (tcChan) DO NOT map to Motate Channels!?! **
		**                          WARNING           (u been warned)      **
		*********************************************************************/

		Timer() { init(); };

		void init() {
			/* Unlock this thing */
			unlock();
		}

		void unlock() {
			tc()->TC_WPMR = TC_WPMR_WPKEY(0x54494D);
		}

		/* WHOA!! Only do this if you know what you're doing!! */
		void lock() {
			tc()->TC_WPMR = TC_WPMR_WPEN | TC_WPMR_WPKEY(0x54494D);
		}

		void enablePeripheralClock() {
			if (peripheralId() < 32) {
				uint32_t id_mask = 1u << ( peripheralId() );
				if ((PMC->PMC_PCSR0 & id_mask) != id_mask) {
					PMC->PMC_PCER0 = id_mask;
				}
			} else {
				uint32_t id_mask = 1u << ( peripheralId() - 32 );
				if ((PMC->PMC_PCSR1 & id_mask) != id_mask) {
					PMC->PMC_PCER1 = id_mask;
				}
			}
		};

		void disablePeripheralClock() {
			if (peripheralId() < 32) {
				uint32_t id_mask = 1u << ( peripheralId() );
				if ((PMC->PMC_PCSR0 & id_mask) == id_mask) {
					PMC->PMC_PCDR0 = id_mask;
				}
			} else {
				uint32_t id_mask = 1u << ( peripheralId() - 32 );
				if ((PMC->PMC_PCSR1 & id_mask) == id_mask) {
					PMC->PMC_PCDR1 = id_mask;
				}
			}
		};

		// Set the mode and frequency.
		// Returns: The actual frequency that was used, or kFrequencyUnattainable
		int32_t setModeAndFrequency(const TimerMode mode, const uint32_t freq) {
			/* Prepare to be able to make changes: */
			/*   Disable TC clock */
			tcChan()->TC_CCR = TC_CCR_CLKDIS ;
			/*   Disable interrupts */
			tcChan()->TC_IDR = 0xFFFFFFFF ;
			/*   Clear status register */
			tcChan()->TC_SR ;

			enablePeripheralClock();

			/* Setup clock "prescaler" */
			/* Divisors: TC1: 2, TC2: 8, TC3: 32, TC4: 128, TC5: ???! */
			/* For now, we don't support TC5. */

			// Grab the SystemCoreClock value, in case it's volatile.
			uint32_t masterClock = SystemCoreClock;

			// Store the divisor temporarily, to avoid looking it up again...
			uint32_t divisor = 2; // sane default of 2

			// TC1 = MCK/2
			if (freq > ((masterClock / 2) / 0x10000) && freq < (masterClock / 2)) {
				/*  Set mode */
				tcChan()->TC_CMR = mode | TC_CMR_TCCLKS_TIMER_CLOCK1;
				divisor = 2;

			// TC2 = MCK/8
			} else if (freq > ((masterClock / 8) / 0x10000) && freq < (masterClock / 8)) {
						/*  Set mode */
				tcChan()->TC_CMR = mode | TC_CMR_TCCLKS_TIMER_CLOCK2;
				divisor = 8;

			// TC3 = MCK/32
			} else if (freq > ((masterClock / 32) / 0x10000) && freq < (masterClock / 32)) {
						/*  Set mode */
				tcChan()->TC_CMR = mode | TC_CMR_TCCLKS_TIMER_CLOCK3;
				divisor = 32;

			// TC4 = MCK/128
			} else if (freq > ((masterClock / 128) / 0x10000) && freq < (masterClock / 128)) {
						/*  Set mode */
				tcChan()->TC_CMR = mode | TC_CMR_TCCLKS_TIMER_CLOCK4;
				divisor = 128;

			// Nothing fit! Hmm...
			} else {
				// PUNT! For now, just guess TC1.
				/*  Set mode */
				tcChan()->TC_CMR = mode | TC_CMR_TCCLKS_TIMER_CLOCK1;

				return kFrequencyUnattainable;
			}

			//TODO: Add ability to select external clocks... -RG

			// Extra mile, set the actual frequency, but only if we're going to RC.
			if (mode == kTimerInputCaptureToMatch
				|| mode == kTimerUpToMatch
			|| mode == kTimerUpDownToMatch) {

				int32_t newTop = masterClock/(divisor*freq);
				setTop(newTop);

				// Determine and return the new frequency.
				return masterClock/(divisor*newTop);
			}

			// Optimization -- we can't use RC for much when we're not using it,
			//  so, instead of looking up if we're using it or not, just set it to
			//  0xFFFF when we're not using it.
			setTop(0xFFFF);

			// Determine and return the new frequency.
			return masterClock/(divisor*0xFFFF);
		};

		// Set the TOP value for modes that use it.
		// WARNING: No sanity checking is done to verify that you are, indeed, in a mode that uses it.
		void setTop(const uint32_t topValue) {
			tcChan()->TC_RC = topValue;
		};

		// Here we want to get what the TOP value is. Is the mode is one that resets on RC, then RC is the TOP.
		// Otherwise, TOP is 0xFFFF. In order to see if TOP is RC, we need to look at the CPCTRG (RC Compare
		// Trigger Enable) bit of the CMR (Channel Mode Register). Note that this bit position is the same for
		// waveform or Capture mode, even though the Datasheet seems to obfuscate this fact.
		uint32_t getTopValue() {
			return tcChan()->TC_CMR & TC_CMR_CPCTRG ? tcChan()->TC_RC : 0xFFFF;
		};

		// Return the current value of the counter. This is a fleeting thing...
		uint32_t getValue() {
			return tcChan()->TC_CV;
		}

		void start() {
			tcChan()->TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
		};

		void stop() {
			tcChan()->TC_CCR = TC_CCR_CLKDIS;
		};

		// Channel-specific functions. These are Motate channels, but they happen to line-up.
		// Motate channel A = Sam channel A.
		// Motate channel B = Sam channel B.

		// Specify the duty cycle as a value from 0.0 .. 1.0;
		void setDutyCycleA(const float ratio) {
			tcChan()->TC_RA = getTopValue() * ratio;
		};

		void setDutyCycleB(const float ratio) {
				tcChan()->TC_RB = getTopValue() * ratio;
		};

		// Specify channel A/B duty cycle as a integer value from 0 .. TOP.
		// TOP in this case is either RC_RC or 0xFFFF.
		void setDutyCycleA(const uint32_t absolute) {
			tcChan()->TC_RA = absolute;
		};

		void setDutyCycleB(const uint32_t absolute) {
			tcChan()->TC_RB = absolute;
		};

		void setInterrupts(const uint32_t interrupts) {
			if (interrupts != kInterruptsOff) {
				tcChan()->TC_IDR = 0xFFFFFFFF;
				NVIC_EnableIRQ(tcIRQ());

				if (interrupts | kInterruptOnOverflow) {
					// Check to see if we're overflowing on C. See getTopValue() description.
					if (tcChan()->TC_CMR & TC_CMR_CPCTRG) {
						tcChan()->TC_IER = TC_IER_CPCS; // RC Compare
					} else {
						tcChan()->TC_IER = TC_IER_COVFS; // Counter Overflow
					}
				}
				if (interrupts | kInterruptOnMatchA) {
					tcChan()->TC_IER = TC_IER_CPAS; // RA Compare
				}
				if (interrupts | kInterruptOnMatchB) {
					tcChan()->TC_IER = TC_IER_CPBS; // RB Compare
				}

			} else {
				tcChan()->TC_IDR = 0xFFFFFFFF;
				NVIC_DisableIRQ(tcIRQ());
			}
		}

		// Placeholder for user code.
		static void interrupt() __attribute__ ((weak));
	};

} // namespace Motate

#define MOTATE_TIMER_INTERRUPT(number) template<> void Timer<number>::interrupt()

/** THIS IS OLD INFO, AND NO LONGER RELEVANT TO THIS PROJECT, BUT IT WAS HARD TO COME BY: **/

/*****
 Ok, here we get ugly: We need the *mangled* names for the specialized interrupt functions,
 so that we can use weak references from C functions TCn_Handler to the C++ Timer<n>::interrupt(),
 so that we get clean linkage to user-provided functions, and no errors if those functions don't exist.
 
 So, to get the mangled names (which will only for for GCC, btw), I do this in a bash shell (ignore any errors after the g++ line):
 
 cat <<END >> temp.cpp
 #include <inttypes.h>
 namespace Motate {
 template <uint8_t timerNum>
 struct Timer {
 static void interrupt();
 };
 template<> void Timer<0>::interrupt() {};
 template<> void Timer<1>::interrupt() {};
 template<> void Timer<2>::interrupt() {};
 template<> void Timer<3>::interrupt() {};
 template<> void Timer<4>::interrupt() {};
 template<> void Timer<5>::interrupt() {};
 template<> void Timer<6>::interrupt() {};
 template<> void Timer<7>::interrupt() {};
 template<> void Timer<8>::interrupt() {};
 }
 END
 arm-none-eabi-g++ temp.cpp -o temp.o -mthumb -nostartfiles -mcpu=cortex-m3
 arm-none-eabi-nm temp.o | grep Motate
 rm temp.o temp.cpp
 
 
 You should get output like this:
 
 00008000 T _ZN6Motate5TimerILh0EE9interruptEv
 0000800c T _ZN6Motate5TimerILh1EE9interruptEv
 00008018 T _ZN6Motate5TimerILh2EE9interruptEv
 00008024 T _ZN6Motate5TimerILh3EE9interruptEv
 00008030 T _ZN6Motate5TimerILh4EE9interruptEv
 0000803c T _ZN6Motate5TimerILh5EE9interruptEv
 00008048 T _ZN6Motate5TimerILh6EE9interruptEv
 00008054 T _ZN6Motate5TimerILh7EE9interruptEv
 00008060 T _ZN6Motate5TimerILh8EE9interruptEv
 
 Ignore the hex number and T at the beginning, and the rest is the mangled names you need for below.
 I broke the string into three parts to clearly show the part that is changing.
 */


#endif /* end of include guard: SAMTIMERS_H_ONCE */