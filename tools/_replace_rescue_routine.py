from pathlib import Path

rescue = Path("local/ng_recomp_game256/cart_rescue_00.c")
new = Path("local/ymir/wram_routine.txt").read_text()
s = rescue.read_text()
name = "void game_r_100000(void) {"
i = s.find(name)
if i < 0:
    raise SystemExit("old routine not found")
# find end of old routine by brace matching
b = s.find("{", i)
depth = 0
end = None
for k in range(b, len(s)):
    if s[k] == "{":
        depth += 1
    elif s[k] == "}":
        depth -= 1
        if depth == 0:
            end = k + 1
            break
s = s[:i] + new.strip() + s[end:]
rescue.write_text(s)
print("replaced game_r_100000 with WRAM-derived recompilation")
