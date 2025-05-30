/*!
    \file  systick.c
    \brief the systick configuration file

    \version 2019-6-5, V1.0.0, firmware for GD32VF103
*/

/*
    Copyright (c) 2019, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

#include "gd32vf103.h"
#include "systick.h"

/*!
    \brief      delay a time in milliseconds
    \param[in]  count: count in milliseconds
    \param[out] none
    \retval     none
*/
// Optimized delay function with early exit for better performance
void delay_1ms(uint32_t count)
{
    if (count == 0) return; // Early exit for zero delay
    
    uint64_t start_mtime, delta_mtime;
    uint64_t target_ticks = (SystemCoreClock/4000) * count;

    // Don't start measuring until we see an mtime tick
    uint64_t tmp = get_timer_value();
    do {
        start_mtime = get_timer_value();
    } while (start_mtime == tmp);

    do {
        delta_mtime = get_timer_value() - start_mtime;
    } while(delta_mtime < target_ticks);
}


static volatile uint32_t system_ticks = 0;

// Initialize a timer for timing
void systick_timer_init(void) {
    rcu_periph_clock_enable(RCU_TIMER1);
    
    timer_parameter_struct timer_initpara;
    timer_deinit(TIMER1);
    
    // Configure timer for 1ms ticks (assuming 108MHz system clock)
    timer_initpara.prescaler = 107;  // 108MHz / 108 = 1MHz
    timer_initpara.alignedmode = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection = TIMER_COUNTER_UP;
    timer_initpara.period = 999;  // 1MHz / 1000 = 1ms
    timer_initpara.clockdivision = TIMER_CKDIV_DIV1;
    timer_initpara.prescaler = 107;  // 108MHz / 108 = 1MHz
    timer_initpara.alignedmode = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection = TIMER_COUNTER_UP;
    eclic_irq_enable(TIMER1_IRQn, 1, 1); // Added priority argument
    timer_initpara.clockdivision = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;  // Add repetition counter field
    timer_init(TIMER1, &timer_initpara);
    
    timer_interrupt_enable(TIMER1, TIMER_INT_UP);
    eclic_irq_enable(TIMER1_IRQn, 1, 1); // Replaced with correct function and added priority argument
    timer_enable(TIMER1);
}

// Timer interrupt handler
void TIMER1_IRQHandler(void) {
    if (timer_interrupt_flag_get(TIMER1, TIMER_INT_FLAG_UP)) {
        timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
        system_ticks++;
    }
}

uint32_t get_tick_count(void) {
    return system_ticks;
}