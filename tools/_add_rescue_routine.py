import sys
from pathlib import Path

src_path = Path(sys.argv[1])           # page file containing the routine
rescue_path = Path(sys.argv[2])        # rescue page to append to
name = sys.argv[3]

src = src_path.read_text(errors="replace")
marker = "void %s(void) {" % name
i = src.find(marker)
if i < 0:
    raise SystemExit("not found: " + marker)
# include preceding routine comment
j = src.rfind("/* routine", 0, i)
if j < 0 or i - j > 120:
    j = i
b = src.find("{", i)
depth = 0
end = None
for k in range(b, len(src)):
    if src[k] == "{":
        depth += 1
    elif src[k] == "}":
        depth -= 1
        if depth == 0:
            end = k + 1
            break
body = src[j:end]
rescue = rescue_path.read_text(errors="replace")
if marker in rescue:
    print("already present")
else:
    rescue_path.write_text(rescue + "\n\n" + body + "\n")
    print("appended %s (%d bytes) from %s" % (name, len(body), src_path.name))
