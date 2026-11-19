# Ghidra headless script: import the KOF98 flat 68000 image and export
# decompiled C for selected addresses. Run via analyzeHeadless with the
# -import parameter set by tools/run_ghidra_decomp.ps1.
# @category KOF98
import sys
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import TaskMonitor

OUT = getScriptArgs()[0]
ADDRS = [int(a, 0) for a in getScriptArgs()[1:]]

program = currentProgram
monitor = ConsoleTaskMonitor()
decomp = DecompInterface()
decomp.openProgram(program)

with open(OUT, "w", encoding="utf-8") as f:
    for addr in ADDRS:
        func = getFunctionAt(toAddr(addr))
        if func is None:
            f.write("==== %06X : NO FUNCTION ====\n" % addr)
            continue
        res = decomp.decompileFunction(func, 120, monitor)
        f.write("==== %06X : %s ====\n" % (addr, func.getName()))
        if res.decompileCompleted():
            f.write(res.getDecompiledFunction().getC())
        else:
            f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
        f.write("\n")
print("wrote", OUT)
