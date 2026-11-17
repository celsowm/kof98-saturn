import sys
from pathlib import Path

s = Path(sys.argv[1]).read_text(errors="replace")
for name in sys.argv[2:]:
    marker = "void %s(void)" % name
    i = s.find(marker)
    if i < 0:
        print(name, "NOT FOUND")
        continue
    # find nearest preceding "routine $...: N instructions"
    j = s.rfind("routine $", 0, i)
    k = s.find("instructions", j)
    print(name, "->", s[j:k + len("instructions")])
