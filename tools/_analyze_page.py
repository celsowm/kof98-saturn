import re, sys
from pathlib import Path
s = Path(sys.argv[1]).read_text(errors='replace')
big = []
for m in re.finditer(r'routine \$([0-9A-Fa-f]+): (\d+) instructions', s):
    v = int(m.group(2))
    if v > 800:
        big.append((v, m.group(1)))
big.sort(reverse=True)
print('routines >800 insns:', len(big))
for v, a in big[:8]:
    print('  0x%s = %d insns' % (a, v))
