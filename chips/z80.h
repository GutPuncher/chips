#pragma once
/*#
    # z80.h

    Header-only Z80 CPU emulator written in C.

    Do this:
    ~~~C
    #define CHIPS_IMPL
    ~~~
    before you include this file in *one* C or C++ file to create the 
    implementation.

    Optionally provide the following macros with your own implementation
    
    ~~~C
    CHIPS_ASSERT(c)
    ~~~
        your own assert macro (default: assert(c))

    ## Emulated Pins
    ***********************************
    *           +-----------+         *
    * M1    <---|           |---> A0  *
    * MREQ  <---|           |---> A1  *
    * IORQ  <---|           |---> A2  *
    * RD    <---|           |---> ..  *
    * WR    <---|    Z80    |---> A15 *
    * HALT  <---|           |         *
    * WAIT  --->|           |<--> D0  *
    * INT   --->|           |<--> D1  *
    *           |           |<--> ... *
    *           |           |<--> D7  *
    *           +-----------+         *
    ***********************************

    ## Not Emulated
    - refresh cycles (RFSH pin)
    - interrupt mode 0
    - bus request/acknowledge (BUSRQ and BUSAK pins)
    - the RESET pin is currently not tested, call the z80_reset() 
      function instead

    ## Functions
    ~~~C
    void z80_init(z80_t* cpu, const z80_desc_t* desc)
    ~~~
        Initializes a new Z80 CPU instance. The z80_desc_t struct
        provides initialization attributes:
            ~~~C
            typedef struct {
                z80_tick_t tick_cb; // the CPU tick callback
                void* user_data;    // user data arg handed to callbacks
            } z80_desc_t;
            ~~~
        The tick_cb function will be called from inside z80_exec().

    ~~~C
    void z80_reset(z80_t* cpu)
    ~~~
        Resets the Z80 CPU instance. 

    ~~~C
    uint32_t z80_exec(z80_t* cpu, uint32_t num_ticks)
    ~~~
        Starts executing instructions until num_ticks is reached or the PC
        hits a trap, returns the number of executed ticks. The number of
        executed ticks will be greater or equal to num_ticks (unless a trap
        has been hit), because complete instructions will be executed. During
        execution the tick callback will be invoked one or multiple times
        (usually once per machine cycle, but also for 'filler ticks' or wait
        state ticks). To check if a trap has been hit, test whether the 
        z80_t.trap_id is >= 0.
        NOTE: the z80_exec() function may return in the 'middle' of an
        DD/FD extended instruction (right after the prefix byte). If this
        is the case, z80_opdone() will return false.

    ~~~C
    bool z80_opdone(z80_t* cpu)
    ~~~
        Return true if z80_exec() has returned at the end of an instruction,
        and false if the CPU is in the middle of a DD/FD prefix instruction.

    ~~~C
    void z80_set_x(z80_t* cpu, uint8_t val)
    void z80_set_xx(z80_t* cpu, uint16_t val)
    uint8_t z80_x(z80_t* cpu)
    uint16_t z80_xx(z80_t* cpu)
    ~~~
        Set and get Z80 registers and flags.

    ~~~C
    void z80_set_trap(z80_t* cpu, int trap_id, uint16_t addr)
    ~~~
        Set a trap breakpoint at a 16-bit CPU address. Up to 4 trap breakpoints
        can be set. After each instruction, the current PC will be checked
        against all valid trap points, and if there is a match, z80_exec() will
        return early, and the trap_id member of z80_t will be >= 0. This can be
        used to set debugger breakpoints, or call out into native host system
        code for other reasons (for instance replacing operating system functions
        like loading game files). 

    ~~~C
    void z80_clear_trap(z80_t* cpu, int trap_id)
    ~~~
        Clear a trap point.

    ~~~C
    bool z80_has_trap(z80_t* cpu, int trap_id)
    ~~~
        Return true if a trap has been set for the given trap_id.

    ## Macros
    ~~~C
    Z80_SET_ADDR(pins, addr)
    ~~~
        set 16-bit address bus pins in 64-bit pin mask

    ~~~C
    Z80_GET_ADDR(pins)
    ~~~
        extract 16-bit address bus value from 64-bit pin mask

    ~~~C
    Z80_SET_DATA(pins, data)
    ~~~
        set 8-bit data bus pins in 64-bit pin mask

    ~~~C
    Z80_GET_DATA(pins)
    ~~~
        extract 8-bit data bus value from 64-bit pin mask

    ~~~C
    Z80_MAKE_PINS(ctrl, addr, data)
    ~~~
        build 64-bit pin mask from control-, address- and data-pins

    ~~~C
    Z80_DAISYCHAIN_BEGIN(pins)
    ~~~
        used in tick function at start of interrupt daisy-chain block

    ~~~C
    Z80_DAISYCHAIN_END(pins)
    ~~~
        used in tick function at end of interrupt daisy-chain block

    ## The Tick Callback 

    The tick function is called for one or multiple time cycles
    and connects the Z80 to the outside world. Usually one call
    of the tick function corresponds to one machine cycle, 
    but this is not always the case. The tick functions takes
    2 arguments:

    - num_ticks: the number of time cycles for this callback invocation
    - pins: a 64-bit integer with CPU pins (address- and data-bus pins,
        and control pins)

    A simplest-possible tick callback which just performs memory read/write
    operations on a 64kByte byte array looks like this:

    ~~~C
    uint8_t mem[1<<16] = { 0 };
    uint64_t tick(int num_ticks, uint64_t pins, void* user_data) {
        if (pins & Z80_MREQ) {
            if (pins & Z80_RD) {
                Z80_SET_DATA(pins, mem[Z80_GET_ADDR(pins)]);
            }
            else if (pins & Z80_WR) {
                mem[Z80_GET_ADDR(pins)] = Z80_GET_DATA(pins);
            }
        }
        else if (pins & Z80_IORQ) {
            // FIXME: perform device I/O
        }
        return pins;
    }
    ~~~

    The tick callback inspects the pins, performs the requested actions
    (memory read/write and input/output), modifies the pin bitmask
    with requests for the CPU (inject wait states, or request an
    interrupt), and finally returns the pin bitmask back to the 
    CPU emulation.

    The following pin bitmasks are relevant for the tick callback:

    - **MREQ|RD**: This is a memory read cycle, the tick callback must 
      put the byte at the memory address indicated by the address
      bus pins A0..A15 (bits 0..15) into the data bus 
      pins (D0..D7). If the M1 pin is also set, then this
      is an opcode fetch machine cycle (4 clock ticks), otherwise
      this is a normal memory read machine cycle (3 clock ticks)
    - **MREQ|WR**: This is a memory write machine cycle, the tick
      callback must write the byte in the data bus pins (D0..D7)
      to the memory location in the address bus pins (A0..A15). 
      A memory write machine cycle is 3 clock-ticks.
    - **IORQ|RD**: This is a device-input machine cycle, the 16-bit
      port number is in the address bus pins (A0..A15), and the
      tick callback must write the input-byte to the data bus
      pins (D0..D7). An input machine cycle is 4 clock-ticks.
    - **IORQ|WR**: This is a device-output machine cycle, the data
      bus pins (D0..D7) contains the byte to be output
      at the port in the address-bus pins (A0..A15). An output
      machine cycle is 4 cycles.

    Interrupt handling requires to inspect and set additional
    pins, more on that below.

    To inject wait states, execute the additional cycles in the
    CPU tick callback, and set the number of wait states
    with the Z80_SET_WAIT() macro on the returned CPU pins.
    Up to 7 wait states can be injected per machine cycle.

    Note that not all calls to the tick callback have one
    of the above pin bit patterns set. The CPU may need
    to execute filler- or processing ticks which are
    not memory-, IO- or interrupt-handling operations.

    This may happen in the following situations:
    - opcode fetch machine cycles are always a single callback
      invocation of 4 cycles with the M1|MREQ|RD pins set, however
      in a real Z80, some opcode fetch machine cycles are 5 or 6
      cycles long, in this case, the tick callback will be called
      again without control pins set and a tick count of 1 or 2
    - some instructions require additional processing ticks which
      are not memory- or IO-operations, in this case the tick
      callback may be called for with any number of ticks, but
      without activated control pins

    ## Interrupt Handling

    The interrupt 'daisy chain protocol' is entirely implemented
    in the tick callback (usually the actual interrupt daisy chain
    handling happens in the Z80 chip-family emulators, but the
    tick callback needs to invoke their interrupt handling functions).

    An interrupt request/acknowledge cycle for (most common)
    interrupt mode 2 looks like this:

    - an interrupt is requested from inside the tick callback by
      setting the INT pin in any tick callback invocation (the 
      INT pins remains active until the end of the current instruction)
    - the CPU emulator checks the INT pin at the end of the current
      instruction, if the INT pin is active, an interrupt-request/acknowledge
      machine cycle is executed which results in additional tick
      callback invocations to perform the interrupt-acknowledge protocol
    - the interrupt-controller device with pending interrupt scans the
      pin bits for M1|IORQ during the tick callback, and if active,
      places the interrupt vector low-byte on the data bus pins
    - back in the CPU emulation, the interrupt request is completed by
      constructing the complete 16-bit interrupt vector, reading the
      address of the interrupt service routine from there, and setting
      the PC register to that address (the next executed instruction
      is then the first instruction of the interrupt service routine)

    There are 2 virtual pins for the interrupt daisy chain protocol:

    - **IEIO** (Interrupt Enable In/Out): This combines the IEI and IEO 
      pins found on Z80 chip-family members and is used to disable
      interrupts for lower-priority interrupt controllers in the
      daisy chain if a higher priority device is currently negotiating
      interrupt handling with the CPU. The IEIO pin always starts
      in active state at the start of the daisy chain, and is handed
      from one interrupt controller to the next in order of 
      daisy-chain priority. The first interrupt controller with
      active interrupt handling clears the IEIO bit, which prevent
      the 'downstream' lower-priority interrupt controllers from
      issuing interrupt requests.
    - **RETI** (Return From Interrupt): The virtual RETI pin is set
      by the CPU when it decodes a RETI instruction. This is scanned
      by the interrupt controller which is currently 'under service'
      to enable interrupt handling for lower-priority devices
      in the daisy chain. In a real Z80 system this the interrupt
      controller chips perform their own simple instruction decoding
      to detect RETI instructions.

    The CPU tick callback is the heart of emulation, for complete
    tick callback examples check the emulators and tests here:
    
    http://github.com/floooh/chips-test

    http://github.com/floooh/yakc

    ## zlib/libpng license

    Copyright (c) 2018 Andre Weissflog
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution. 
#*/
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*--- callback function typedefs ---*/
typedef uint64_t (*z80_tick_t)(int num_ticks, uint64_t pins, void* user_data);

/*--- address lines ---*/
#define Z80_A0  (1ULL<<0)
#define Z80_A1  (1ULL<<1)
#define Z80_A2  (1ULL<<2)
#define Z80_A3  (1ULL<<3)
#define Z80_A4  (1ULL<<4)
#define Z80_A5  (1ULL<<5)
#define Z80_A6  (1ULL<<6)
#define Z80_A7  (1ULL<<7)
#define Z80_A8  (1ULL<<8)
#define Z80_A9  (1ULL<<9)
#define Z80_A10 (1ULL<<10)
#define Z80_A11 (1ULL<<11)
#define Z80_A12 (1ULL<<12)
#define Z80_A13 (1ULL<<13)
#define Z80_A14 (1ULL<<14)
#define Z80_A15 (1ULL<<15)

/*--- data lines ------*/
#define Z80_D0  (1ULL<<16)
#define Z80_D1  (1ULL<<17)
#define Z80_D2  (1ULL<<18)
#define Z80_D3  (1ULL<<19)
#define Z80_D4  (1ULL<<20)
#define Z80_D5  (1ULL<<21)
#define Z80_D6  (1ULL<<22)
#define Z80_D7  (1ULL<<23)

/*--- control pins ---*/

/* system control pins */
#define  Z80_M1    (1ULL<<24)       /* machine cycle 1 */
#define  Z80_MREQ  (1ULL<<25)       /* memory request */
#define  Z80_IORQ  (1ULL<<26)       /* input/output request */
#define  Z80_RD    (1ULL<<27)       /* read */
#define  Z80_WR    (1ULL<<28)       /* write */
#define  Z80_CTRL_MASK (Z80_M1|Z80_MREQ|Z80_IORQ|Z80_RD|Z80_WR)

/* CPU control pins */
#define  Z80_HALT  (1ULL<<29)       /* halt state */
#define  Z80_INT   (1ULL<<30)       /* interrupt request */
#define  Z80_NMI   (1ULL<<31)       /* non-maskable interrupt */
#define  Z80_BUSREQ (1ULL<<32)      /* bus request */
#define  Z80_BUSACK (1ULL<<33)      /* bus acknowledge */

/* up to 7 wait states can be injected per machine cycle */
#define Z80_WAIT0   (1ULL<<34)
#define Z80_WAIT1   (1ULL<<35)
#define Z80_WAIT2   (1ULL<<36)
#define Z80_WAIT_SHIFT (34)
#define Z80_WAIT_MASK (Z80_WAIT0|Z80_WAIT1|Z80_WAIT2)

/* interrupt-related 'virtual pins', these don't exist on the Z80 */
#define Z80_IEIO    (1ULL<<37)      /* unified daisy chain 'Interrupt Enable In+Out' */
#define Z80_RETI    (1ULL<<38)      /* cpu has decoded a RETI instruction */

/* bit mask for all CPU bus pins */
#define Z80_PIN_MASK ((1ULL<<40)-1)

/*--- status indicator flags ---*/
#define Z80_CF (1<<0)           /* carry */
#define Z80_NF (1<<1)           /* add/subtract */
#define Z80_VF (1<<2)           /* parity/overflow */
#define Z80_PF Z80_VF
#define Z80_XF (1<<3)           /* undocumented bit 3 */
#define Z80_HF (1<<4)           /* half carry */
#define Z80_YF (1<<5)           /* undocumented bit 5 */
#define Z80_ZF (1<<6)           /* zero */
#define Z80_SF (1<<7)           /* sign */

#define Z80_MAX_NUM_TRAPS (4)

/* initialization attributes */
typedef struct {
    z80_tick_t tick_cb;
    void* user_data;
} z80_desc_t;

/* Z80 CPU state */
typedef struct {
    z80_tick_t tick;
    uint8_t b, c, d, e, h, l, f, a, ixh, ixl, iyh, iyl;
    uint16_t bc_, de_, hl_, fa_;
    uint16_t wz, sp, pc;
    uint8_t im, i, r;
    bool iff1, iff2, ei_pending;
    uint64_t pins;
    void* user_data;
    int trap_id;
    uint64_t trap_addr;
} z80_t;

/* initialize a new z80 instance */
void z80_init(z80_t* cpu, const z80_desc_t* desc);
/* reset an existing z80 instance */
void z80_reset(z80_t* cpu);
/* set a trap point */
void z80_set_trap(z80_t* cpu, int trap_id, uint16_t addr);
/* clear a trap point */
void z80_clear_trap(z80_t* cpu, int trap_id);
/* return true if a trap is valid */
bool z80_has_trap(z80_t* cpu, int trap_id);
/* execute instructions for at least 'ticks', but at least one, return executed ticks */
uint32_t z80_exec(z80_t* cpu, uint32_t ticks);
/* return false if z80_exec() returned in the middle of an extended intruction */
bool z80_opdone(z80_t* cpu);

/* register access functions */
void z80_set_a(z80_t* cpu, uint8_t v);
void z80_set_f(z80_t* cpu, uint8_t v);
void z80_set_l(z80_t* cpu, uint8_t v);
void z80_set_h(z80_t* cpu, uint8_t v);
void z80_set_e(z80_t* cpu, uint8_t v);
void z80_set_d(z80_t* cpu, uint8_t v);
void z80_set_c(z80_t* cpu, uint8_t v);
void z80_set_b(z80_t* cpu, uint8_t v);
void z80_set_fa(z80_t* cpu, uint16_t v);
void z80_set_af(z80_t* cpi, uint16_t v);
void z80_set_hl(z80_t* cpu, uint16_t v);
void z80_set_de(z80_t* cpu, uint16_t v);
void z80_set_bc(z80_t* cpu, uint16_t v);
void z80_set_fa_(z80_t* cpu, uint16_t v);
void z80_set_af_(z80_t* cpi, uint16_t v);
void z80_set_hl_(z80_t* cpu, uint16_t v);
void z80_set_de_(z80_t* cpu, uint16_t v);
void z80_set_bc_(z80_t* cpu, uint16_t v);
void z80_set_pc(z80_t* cpu, uint16_t v);
void z80_set_wz(z80_t* cpu, uint16_t v);
void z80_set_sp(z80_t* cpu, uint16_t v);
void z80_set_i(z80_t* cpu, uint8_t v);
void z80_set_r(z80_t* cpu, uint8_t v);
void z80_set_ix(z80_t* cpu, uint16_t v);
void z80_set_iy(z80_t* cpu, uint16_t v);
void z80_set_im(z80_t* cpu, uint8_t v);
void z80_set_iff1(z80_t* cpu, bool b);
void z80_set_iff2(z80_t* cpu, bool b);
void z80_set_ei_pending(z80_t* cpu, bool b);

uint8_t z80_a(z80_t* cpu);
uint8_t z80_f(z80_t* cpu);
uint8_t z80_l(z80_t* cpu);
uint8_t z80_h(z80_t* cpu);
uint8_t z80_e(z80_t* cpu);
uint8_t z80_d(z80_t* cpu);
uint8_t z80_c(z80_t* cpu);
uint8_t z80_b(z80_t* cpu);
uint16_t z80_fa(z80_t* cpu);
uint16_t z80_af(z80_t* cpu);
uint16_t z80_hl(z80_t* cpu);
uint16_t z80_de(z80_t* cpu);
uint16_t z80_bc(z80_t* cpu);
uint16_t z80_fa_(z80_t* cpu);
uint16_t z80_af_(z80_t* cpu);
uint16_t z80_hl_(z80_t* cpu);
uint16_t z80_de_(z80_t* cpu);
uint16_t z80_bc_(z80_t* cpu);
uint16_t z80_pc(z80_t* cpu);
uint16_t z80_wz(z80_t* cpu);
uint16_t z80_sp(z80_t* cpu);
uint16_t z80_ir(z80_t* cpu);
uint8_t z80_i(z80_t* cpu);
uint8_t z80_r(z80_t* cpu);
uint16_t z80_ix(z80_t* cpu);
uint16_t z80_iy(z80_t* cpu);
uint8_t z80_im(z80_t* cpu);
bool z80_iff1(z80_t* cpu);
bool z80_iff2(z80_t* cpu);
bool z80_ei_pending(z80_t* cpu);

/* helper macro to start interrupt handling in tick callback */
#define Z80_DAISYCHAIN_BEGIN(pins) if (pins&Z80_M1) { pins|=Z80_IEIO;
/* helper macro to end interrupt handling in tick callback */
#define Z80_DAISYCHAIN_END(pins) pins&=~Z80_RETI; }
/* return a pin mask with control-pins, address and data bus */
#define Z80_MAKE_PINS(ctrl, addr, data) ((ctrl)|(((data)<<16)&0xFF0000ULL)|((addr)&0xFFFFULL))
/* extract 16-bit address bus from 64-bit pins */
#define Z80_GET_ADDR(p) ((uint16_t)(p&0xFFFFULL))
/* merge 16-bit address bus value into 64-bit pins */
#define Z80_SET_ADDR(p,a) {p=((p&~0xFFFFULL)|((a)&0xFFFFULL));}
/* extract 8-bit data bus from 64-bit pins */
#define Z80_GET_DATA(p) ((uint8_t)((p&0xFF0000ULL)>>16))
/* merge 8-bit data bus value into 64-bit pins */
#define Z80_SET_DATA(p,d) {p=((p&~0xFF0000ULL)|(((d)<<16)&0xFF0000ULL));}
/* extract number of wait states from pin mask */
#define Z80_GET_WAIT(p) ((p&Z80_WAIT_MASK)>>Z80_WAIT_SHIFT)
/* set up to 7 wait states in pin mask */
#define Z80_SET_WAIT(p,w) {p=((p&~Z80_WAIT_MASK)|((((uint64_t)w)<<Z80_WAIT_SHIFT)&Z80_WAIT_MASK));}

#ifdef __cplusplus
} /* extern "C" */
#endif

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

/* set 16-bit address bus pins */
#define _SA(addr) pins=(pins&~0xFFFFULL)|((addr)&0xFFFFULL)
/* set 16-bit address bus and 8-bit data bus pins */
#define _SAD(addr,data) pins=(pins&~0xFFFFFFULL)|((((data)&0xFFULL)<<16)&0xFF0000ULL)|((addr)&0xFFFFULL)
/* get 8-bit data bus value from pins */
#define _GD() ((uint8_t)((pins&0xFF0000ULL)>>16))
/* invoke 'filler tick' without control pins set */
#define _T(num) pins=tick(num,(pins&~Z80_CTRL_MASK),ud);ticks+=num
/* invoke tick callback with pins mask */
#define _TM(num,mask) pins=tick(num,(pins&~(Z80_CTRL_MASK))|(mask),ud);ticks+=num
/* invoke tick callback (with wait state detecion) */
#define _TWM(num,mask) pins=tick(num,(pins&~(Z80_WAIT_MASK|Z80_CTRL_MASK))|(mask),ud);ticks+=num+Z80_GET_WAIT(pins)
/* memory read machine cycle */
#define _MR(addr,data) _SA(addr);_TWM(3,Z80_MREQ|Z80_RD);data=_GD()
/* memory write machine cycle */
#define _MW(addr,data) _SAD(addr,data);_TWM(3,Z80_MREQ|Z80_WR)
/* input machine cycle */
#define _IN(addr,data) _SA(addr);_TWM(4,Z80_IORQ|Z80_RD);data=_GD()
/* output machine cycle */
#define _OUT(addr,data) _SAD(addr,data);_TWM(4,Z80_IORQ|Z80_WR);
/* read 8-bit immediate value */
#define _IMM8(data) _MR(PC++,data);
/* read 16-bit immediate value into WZ */
#define _IMM16() {uint8_t w,z;_MR(PC++,z);_MR(PC++,w);WZ=(w<<8)|z;} 
/* helper macro to bump R register */
#define _BUMPR() R=(R&0x80)|((R+1)&0x7F);
/* a normal opcode fetch, bump R */
#define _FETCH(op) {_SA(PC++);_TWM(4,Z80_M1|Z80_MREQ|Z80_RD);op=_GD();_BUMPR();}
/* special opcode fetch for CB prefix, only bump R if not a DD/FD+CB 'double prefix' op */
#define _FETCH_CB(op) {_SA(PC++);_TWM(4,Z80_M1|Z80_MREQ|Z80_RD);op=_GD();}
/* evaluate S+Z flags */
#define _SZ(val) ((val&0xFF)?(val&Z80_SF):Z80_ZF)
/* evaluate SZYXCH flags */
#define _SZYXCH(acc,val,res) (_SZ(res)|(res&(Z80_YF|Z80_XF))|((res>>8)&Z80_CF)|((acc^val^res)&Z80_HF))
/* evaluate flags for 8-bit adds */
#define _ADD_FLAGS(acc,val,res) (_SZYXCH(acc,val,res)|((((val^acc^0x80)&(val^res))>>5)&Z80_VF))
/* evaluate flags for 8-bit subs */
#define _SUB_FLAGS(acc,val,res) (Z80_NF|_SZYXCH(acc,val,res)|((((val^acc)&(res^acc))>>5)&Z80_VF))
/* evaluate flags for 8-bit compare */
#define _CP_FLAGS(acc,val,res) (Z80_NF|(_SZ(res)|(val&(Z80_YF|Z80_XF))|((res>>8)&Z80_CF)|((acc^val^res)&Z80_HF))|((((val^acc)&(res^acc))>>5)&Z80_VF))
/* evaluate flags for LD A,I and LD A,R */
#define _SZIFF2_FLAGS(val) ((F&Z80_CF)|_SZ(val)|(val&(Z80_YF|Z80_XF))|(IFF2?Z80_PF:0))

/* register access functions */
void z80_set_a(z80_t* cpu, uint8_t v)         { cpu->a=v; }
void z80_set_f(z80_t* cpu, uint8_t v)         { cpu->f=v; }
void z80_set_l(z80_t* cpu, uint8_t v)         { cpu->l=v; }
void z80_set_h(z80_t* cpu, uint8_t v)         { cpu->h=v; }
void z80_set_e(z80_t* cpu, uint8_t v)         { cpu->e=v; }
void z80_set_d(z80_t* cpu, uint8_t v)         { cpu->d=v; }
void z80_set_c(z80_t* cpu, uint8_t v)         { cpu->c=v; }
void z80_set_b(z80_t* cpu, uint8_t v)         { cpu->b=v; }
void z80_set_af(z80_t* cpu, uint16_t v)       { cpu->a=v>>8; cpu->f=v; }
void z80_set_fa(z80_t* cpu, uint16_t v)       { cpu->f=v>>8; cpu->a=v; }
void z80_set_hl(z80_t* cpu, uint16_t v)       { cpu->h=v>>8; cpu->l=v; }
void z80_set_de(z80_t* cpu, uint16_t v)       { cpu->d=v>>8; cpu->e=v; }
void z80_set_bc(z80_t* cpu, uint16_t v)       { cpu->b=v>>8; cpu->c=v; }
void z80_set_fa_(z80_t* cpu, uint16_t v)      { cpu->fa_=v; }
void z80_set_af_(z80_t* cpu, uint16_t v)      { cpu->fa_=((v<<8)&0xFF00)|((v>>8)&0x00FF); }
void z80_set_hl_(z80_t* cpu, uint16_t v)      { cpu->hl_=v; }
void z80_set_de_(z80_t* cpu, uint16_t v)      { cpu->de_=v; }
void z80_set_bc_(z80_t* cpu, uint16_t v)      { cpu->bc_=v; }
void z80_set_iy(z80_t* cpu, uint16_t v)       { cpu->iyh=v>>8; cpu->iyl=v; }
void z80_set_ix(z80_t* cpu, uint16_t v)       { cpu->ixh=v>>8; cpu->ixl=v; }
void z80_set_wz(z80_t* cpu, uint16_t v)       { cpu->wz=v; }
void z80_set_sp(z80_t* cpu, uint16_t v)       { cpu->sp=v; }
void z80_set_pc(z80_t* cpu, uint16_t v)       { cpu->pc=v; }
void z80_set_ir(z80_t* cpu, uint16_t v)       { cpu->i=v>>8; cpu->r=v; }
void z80_set_i(z80_t* cpu, uint8_t v)         { cpu->i=v; }
void z80_set_r(z80_t* cpu, uint8_t v)         { cpu->r=v; }
void z80_set_im(z80_t* cpu, uint8_t v)        { cpu->im=v; }
void z80_set_iff1(z80_t* cpu, bool b)         { cpu->iff1=b; }
void z80_set_iff2(z80_t* cpu, bool b)         { cpu->iff2=b; }
void z80_set_ei_pending(z80_t* cpu, bool b)   { cpu->ei_pending=b; }
uint8_t z80_a(z80_t* cpu)         { return cpu->a; }
uint8_t z80_f(z80_t* cpu)         { return cpu->f; }
uint8_t z80_l(z80_t* cpu)         { return cpu->l; }
uint8_t z80_h(z80_t* cpu)         { return cpu->h; }
uint8_t z80_e(z80_t* cpu)         { return cpu->e; }
uint8_t z80_d(z80_t* cpu)         { return cpu->d; }
uint8_t z80_c(z80_t* cpu)         { return cpu->c; }
uint8_t z80_b(z80_t* cpu)         { return cpu->b; }
uint16_t z80_fa(z80_t* cpu)       { return (cpu->f<<8)|cpu->a; }
uint16_t z80_af(z80_t* cpu)       { return (cpu->a<<8)|cpu->f; }
uint16_t z80_hl(z80_t* cpu)       { return (cpu->h<<8)|cpu->l; }
uint16_t z80_de(z80_t* cpu)       { return (cpu->d<<8)|cpu->e; }
uint16_t z80_bc(z80_t* cpu)       { return (cpu->b<<8)|cpu->c; }
uint16_t z80_fa_(z80_t* cpu)      { return cpu->fa_; }
uint16_t z80_af_(z80_t* cpu)      { return (cpu->fa_<<8)|(cpu->fa_>>8); }
uint16_t z80_hl_(z80_t* cpu)      { return cpu->hl_; }
uint16_t z80_de_(z80_t* cpu)      { return cpu->de_; }
uint16_t z80_bc_(z80_t* cpu)      { return cpu->bc_; }
uint16_t z80_sp(z80_t* cpu)       { return cpu->sp; }
uint16_t z80_iy(z80_t* cpu)       { return (cpu->iyh<<8)|cpu->iyl; }
uint16_t z80_ix(z80_t* cpu)       { return (cpu->ixh<<8)|cpu->ixl; }
uint16_t z80_wz(z80_t* cpu)       { return cpu->wz; }
uint16_t z80_pc(z80_t* cpu)       { return cpu->pc; }
uint16_t z80_ir(z80_t* cpu)       { return (cpu->i<<8)|cpu->r; }
uint8_t z80_i(z80_t* cpu)         { return cpu->i; }
uint8_t z80_r(z80_t* cpu)         { return cpu->r; }
uint8_t z80_im(z80_t* cpu)        { return cpu->im; }
bool z80_iff1(z80_t* cpu)         { return cpu->iff1; }
bool z80_iff2(z80_t* cpu)         { return cpu->iff2; }
bool z80_ei_pending(z80_t* cpu)   { return cpu->ei_pending; }

void z80_init(z80_t* cpu, const z80_desc_t* desc) {
    CHIPS_ASSERT(cpu && desc);
    CHIPS_ASSERT(desc->tick_cb);
    memset(cpu, 0, sizeof(*cpu));
    z80_reset(cpu);
    cpu->trap_addr = 0xFFFFFFFFFFFFFFFF;
    cpu->tick = desc->tick_cb;
    cpu->user_data = desc->user_data;
}

void z80_reset(z80_t* cpu) {
    CHIPS_ASSERT(cpu);
    /* set AF to 0xFFFF, all other regs are undefined, set to 0xFFFF to */
    z80_set_af(cpu, 0xFFFF);
    z80_set_bc(cpu, 0xFFFF);
    z80_set_de(cpu, 0xFFFF);
    z80_set_hl(cpu, 0xFFFF);
    z80_set_af_(cpu, 0xFFFF);
    z80_set_bc_(cpu, 0xFFFF);
    z80_set_de_(cpu, 0xFFFF);
    z80_set_hl_(cpu, 0xFFFF);
    z80_set_ix(cpu, 0xFFFF);
    z80_set_iy(cpu, 0xFFFF);
    z80_set_wz(cpu, 0xFFFF);
    /* set SP to 0xFFFF, PC to 0x0000 */
    z80_set_sp(cpu, 0xFFFF);
    z80_set_pc(cpu, 0x0000);
    /* IFF1 and IFF2 are off */
    z80_set_iff1(cpu, false);
    z80_set_iff2(cpu, false);
    z80_set_ei_pending(cpu, false);
    /* IM is set to 0 */
    z80_set_im(cpu, 0);
    /* after power-on or reset, R is set to 0 (see z80-documented.pdf) */
    z80_set_ir(cpu, 0x0000);
}

void z80_set_trap(z80_t* cpu, int trap_id, uint16_t addr) {
    CHIPS_ASSERT(cpu && (trap_id >= 0) && (trap_id < Z80_MAX_NUM_TRAPS));
    cpu->trap_addr &= ~(0xFFFFULL<<(trap_id<<4));
    cpu->trap_addr |= addr<<(trap_id<<4);
}

void z80_clear_trap(z80_t* cpu, int trap_id) {
    CHIPS_ASSERT(cpu && (trap_id >= 0) && (trap_id < Z80_MAX_NUM_TRAPS));
    cpu->trap_addr |= 0xFFFFULL<<(trap_id<<4);
}

bool z80_has_trap(z80_t* cpu, int trap_id) {
    CHIPS_ASSERT(cpu && (trap_id >= 0) && (trap_id < Z80_MAX_NUM_TRAPS));
    return (cpu->trap_addr>>(trap_id<<4) & 0xFFFF) != 0xFFFF;
}

bool z80_opdone(z80_t* cpu) {
    // FIXME
    return true;
    //return 0 == (cpu->im_ir_pc_bits & (_BIT_USE_IX|_BIT_USE_IY));
}

/* sign+zero+parity lookup table */
static uint8_t _z80_szp[256] = {
  0x44,0x00,0x00,0x04,0x00,0x04,0x04,0x00,0x08,0x0c,0x0c,0x08,0x0c,0x08,0x08,0x0c,
  0x00,0x04,0x04,0x00,0x04,0x00,0x00,0x04,0x0c,0x08,0x08,0x0c,0x08,0x0c,0x0c,0x08,
  0x20,0x24,0x24,0x20,0x24,0x20,0x20,0x24,0x2c,0x28,0x28,0x2c,0x28,0x2c,0x2c,0x28,
  0x24,0x20,0x20,0x24,0x20,0x24,0x24,0x20,0x28,0x2c,0x2c,0x28,0x2c,0x28,0x28,0x2c,
  0x00,0x04,0x04,0x00,0x04,0x00,0x00,0x04,0x0c,0x08,0x08,0x0c,0x08,0x0c,0x0c,0x08,
  0x04,0x00,0x00,0x04,0x00,0x04,0x04,0x00,0x08,0x0c,0x0c,0x08,0x0c,0x08,0x08,0x0c,
  0x24,0x20,0x20,0x24,0x20,0x24,0x24,0x20,0x28,0x2c,0x2c,0x28,0x2c,0x28,0x28,0x2c,
  0x20,0x24,0x24,0x20,0x24,0x20,0x20,0x24,0x2c,0x28,0x28,0x2c,0x28,0x2c,0x2c,0x28,
  0x80,0x84,0x84,0x80,0x84,0x80,0x80,0x84,0x8c,0x88,0x88,0x8c,0x88,0x8c,0x8c,0x88,
  0x84,0x80,0x80,0x84,0x80,0x84,0x84,0x80,0x88,0x8c,0x8c,0x88,0x8c,0x88,0x88,0x8c,
  0xa4,0xa0,0xa0,0xa4,0xa0,0xa4,0xa4,0xa0,0xa8,0xac,0xac,0xa8,0xac,0xa8,0xa8,0xac,
  0xa0,0xa4,0xa4,0xa0,0xa4,0xa0,0xa0,0xa4,0xac,0xa8,0xa8,0xac,0xa8,0xac,0xac,0xa8,
  0x84,0x80,0x80,0x84,0x80,0x84,0x84,0x80,0x88,0x8c,0x8c,0x88,0x8c,0x88,0x88,0x8c,
  0x80,0x84,0x84,0x80,0x84,0x80,0x80,0x84,0x8c,0x88,0x88,0x8c,0x88,0x8c,0x8c,0x88,
  0xa0,0xa4,0xa4,0xa0,0xa4,0xa0,0xa0,0xa4,0xac,0xa8,0xa8,0xac,0xa8,0xac,0xac,0xa8,
  0xa4,0xa0,0xa0,0xa4,0xa0,0xa4,0xa4,0xa0,0xa8,0xac,0xac,0xa8,0xac,0xa8,0xa8,0xac,
};

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable: 4065) // switch statement contains 'default' but no 'case' labels
#endif
#include "_z80_decoder.h"
#ifdef _MSC_VER
#pragma warning (pop)
#endif

#undef _SA
#undef _SAD
#undef _GD
#undef _T
#undef _TM
#undef _TWM
#undef _MR
#undef _MW
#undef _IN
#undef _OUT
#undef _IMM8
#undef _IMM16
#undef _ADDR
#undef _BUMPR
#undef _FETCH
#undef _FETCH_CB
#undef _SZ
#undef _SZYXCH
#undef _ADD_FLAGS
#undef _SUB_FLAGS
#undef _CP_FLAGS
#undef _SZIFF2_FLAGS
#endif /* CHIPS_IMPL */
