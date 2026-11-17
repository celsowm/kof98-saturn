from pathlib import Path

s = Path("local/ymir/wram_recomp/wram_recomp_00.c").read_text()
i = s.find("void wram_r_100000(void) {")
j = s.find("\n/* routine", i + 10)
body = s[i:j if j > 0 else i + 3000]
# add switch cases for every guest entry point in this routine
old = "    case 0x100000u: goto L_100000;\n"
new = ("    case 0x100000u: goto L_100000;\n"
       "    case 0x100004u: goto L_100004;\n"
       "    case 0x100006u: goto L_100006;\n"
       "    case 0x10000Au: goto L_10000A;\n"
       "    case 0x10000Eu: goto L_10000E;\n")
body = body.replace(old, new).replace("wram_r_100000", "game_r_100000")
# substitute into the 1024 rescue page
p = Path("local/ng_recomp_game1024/cart_rescue_00.c")
t = p.read_text()
k = t.find("void game_r_100000(void) {")
b = t.find("{", k)
depth = 0
end = None
for n in range(b, len(t)):
    if t[n] == "{":
        depth += 1
    elif t[n] == "}":
        depth -= 1
        if depth == 0:
            end = n + 1
            break
p.write_text(t[:k] + body.strip() + t[end:])
Path("local/ymir/wram_routine.txt").write_text(body.strip())
print("WRAM routine now has 5 dispatch entries")
