from pathlib import Path

p = Path("src/ng/ng_boot.c")
s = p.read_text()
# remove instrumentation declarations
s = s.replace('volatile uint32_t g_bank_log[32] __attribute__((section(".wram_l")));\n', "")
s = s.replace("volatile uint32_t g_bank_log_n;\n", "")
s = s.replace("volatile uint32_t g_wram_writes;   /* slow-path WRAM writes */\n", "")
s = s.replace("static void bank_log(uint32_t kind, uint32_t pc, uint32_t val);\n", "")
# remove bank_log calls
s = s.replace("if (a == 0x200000u) bank_log(3, ng_cpu.pc, rv);\n            ", "")
s = s.replace("bank_log(1, ng_cpu.pc, v); ", "")
s = s.replace("bank_log(2, ng_cpu.pc, v); ", "")
s = s.replace("    if (a >= 0x100000 && a < 0x200000) g_wram_writes++;\n", "")
# remove PC trace declarations + definition
s = s.replace('volatile uint32_t g_pc_log[128] __attribute__((section(".wram_l")));\n', "")
s = s.replace("volatile uint32_t g_pc_log_w;\n", "")
start = s.find("static void pc_trace(uint32_t pc, uint32_t d0, uint32_t sp) {")
if start >= 0:
    end = s.find("\n}\n", start) + 3
    s = s[:start] + s[end:]
s = s.replace("        pc_trace(ng_cpu.pc, ng_cpu.d[0], ng_cpu.a[7]);\n", "")
# remove bank_log definition
start = s.find("/* Slow-path WRAM instrumentation (declared above with g_pc_log). */")
if start >= 0:
    end = s.find("\n}\n", start) + 3
    s = s[:start] + s[end:]
p.write_text(s)
print("cleaned; remaining refs:", [l for l in s.splitlines() if "bank_log" in l or "pc_log" in l or "g_wram_writes" in l][:5])
