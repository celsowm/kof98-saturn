from pathlib import Path

p = Path("src/ng/ng_boot.c")
s = p.read_text()
s = s.replace('volatile uint32_t g_trace[256] __attribute__((section(".wram_l")));\n', "")
s = s.replace("volatile uint32_t g_trace_n;\n", "")
start = s.find("        /* Trace dispatches in the 0x800-0x8FF window")
if start >= 0:
    end = s.find("        }\n", start) + len("        }\n")
    s = s[:start] + s[end:]
p.write_text(s)
print("trace refs:", [l for l in s.splitlines() if "g_trace" in l])
