/* Force-included into Musashi translation units to count instructions.
 * Compiled with: -include ng_m68k_force.h
 * Keeps the counting out of ng_boot.c's public interface and the command line.
 */
#ifndef NG_M68K_FORCE_H
#define NG_M68K_FORCE_H

#define M68K_INSTRUCTION_HOOK 2                 /* M68K_OPT_SPECIFY_HANDLER */
#define M68K_INSTRUCTION_CALLBACK(pc) ng_insn_hook(pc)

#endif