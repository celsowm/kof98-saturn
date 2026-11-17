/* ng_musashi_conf_boot.h - Musashi config for the native 68000 Neo Geo boot.
 * A plain 68000, autovectored interrupts, with the per-instruction hook enabled
 * so the boot can report exactly how many 68000 instructions executed on the
 * target. Memory callbacks only (no other callbacks). */
#ifndef NG_MUSASHI_CONF_BOOT_H
#define NG_MUSASHI_CONF_BOOT_H

#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1
#define M68K_OPT_SPECIFY_HANDLER 2

#define M68K_COMPILE_FOR_MAME      M68K_OPT_OFF
#define M68K_EMULATE_010           M68K_OPT_OFF
#define M68K_EMULATE_EC020         M68K_OPT_OFF
#define M68K_EMULATE_020           M68K_OPT_OFF
#define M68K_EMULATE_030           M68K_OPT_OFF
#define M68K_EMULATE_040           M68K_OPT_OFF
#define M68K_SEPARATE_READS        M68K_OPT_ON
#define M68K_SIMULATE_PD_WRITES    M68K_OPT_OFF
#define M68K_EMULATE_INT_ACK       M68K_OPT_SPECIFY_HANDLER
unsigned int ng_int_ack_handler(unsigned int level);
#define M68K_INT_ACK_CALLBACK(A)   ng_int_ack_handler(A)
#define M68K_EMULATE_BKPT_ACK      M68K_OPT_OFF
#define M68K_EMULATE_TRACE         M68K_OPT_OFF
#define M68K_EMULATE_RESET         M68K_OPT_OFF
#define M68K_CMPILD_HAS_CALLBACK   M68K_OPT_OFF
#define M68K_RTE_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_TAS_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_ILLG_HAS_CALLBACK     M68K_OPT_OFF
#define M68K_TRAP_HAS_CALLBACK     M68K_OPT_OFF
#define M68K_EMULATE_FC            M68K_OPT_OFF
#define M68K_MONITOR_PC            M68K_OPT_OFF
#define M68K_INSTRUCTION_HOOK      M68K_OPT_SPECIFY_HANDLER
void ng_insn_hook(unsigned int pc);
#define M68K_INSTRUCTION_CALLBACK(pc) ng_insn_hook(pc)
#define M68K_EMULATE_PREFETCH      M68K_OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR M68K_OPT_OFF
#define M68K_LOG_ENABLE            M68K_OPT_OFF
#define M68K_LOG_1010_1111         M68K_OPT_OFF
#define M68K_LOG_TRAP              M68K_OPT_OFF
#define M68K_LOG_FILEHANDLE        0
#define M68K_EMULATE_PMMU          M68K_OPT_OFF
#define M68K_USE_64_BIT            M68K_OPT_ON

#endif