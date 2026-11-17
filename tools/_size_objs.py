import subprocess
from pathlib import Path

SZ = "C:/msys64/home/celso/saturn-tools/bin/sh2eb-elf-size.exe"
d = Path("build-ng/CMakeFiles/kof_resident_bench.dir")
for f in d.rglob("*.obj"):
    if any(k in f.name for k in ("strip_renderer", "tile_cache_sdk", "asset_store_sdk", "ng_live_strips", "resident_routines")):
        out = subprocess.run([SZ, str(f)], capture_output=True, text=True).stdout.splitlines()
        if len(out) >= 2:
            print("%-28s %s" % (f.name, out[-1].split()[:3]))
