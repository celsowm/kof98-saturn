#!/usr/bin/env python3
"""Analytical allocation comparison; no ROM files or performance claims."""
import argparse

def estimate(entries: int, banks: int, pointer_bytes: int) -> dict:
    if entries < 0 or not 1 <= banks <= 8 or pointer_bytes not in (4, 8):
        raise ValueError("invalid entry count, bank count or pointer width")
    dense = ((1 + banks) * 0x80000 + 0x10000) * pointer_bytes
    # uint32_t key followed by properly aligned native pointer.
    stride = ((4 + pointer_bytes - 1) // pointer_bytes + 1) * pointer_bytes
    return {"dense_bytes": dense, "sparse_bytes": entries * stride,
            "entry_stride": stride}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--entries", type=int, default=57972)
    p.add_argument("--banks", type=int, default=4)
    p.add_argument("--pointer-bytes", type=int, choices=(4, 8), default=4)
    a = p.parse_args()
    try:
        b = estimate(a.entries, a.banks, a.pointer_bytes)
    except ValueError as exc:
        p.error(str(exc))
    for name, size in b.items():
        print(f"{name}: {size:,}")
    print("Excludes executable, metadata, runtime, stacks and alignment between arrays.")

if __name__ == "__main__":
    main()
