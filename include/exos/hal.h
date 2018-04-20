
#pragma once
#define __EXOS_HAL__

#include <common/basic_defs.h>

#if defined(__i386__)

   #define __arch__x86__
   #include <common/arch/generic_x86/x86_utils.h>
   #include <exos/arch/i386/asm_defs.h>
   #include <exos/arch/i386/arch_utils.h>

#elif defined(__x86_64__)

   #define __arch__x86__
   #include <common/arch/generic_x86/x86_utils.h>
   #include <exos/arch/x86_64/arch_utils.h>

#else

   #error Unsupported architecture.

#endif


typedef void (*interrupt_handler)(regs *);


void setup_segmentation(void);
void setup_soft_interrupt_handling(void);
void setup_syscall_interfaces();
void set_kernel_stack(uptr stack);
