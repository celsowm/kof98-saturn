# KOF98 Saturn

Experimental native Sega Saturn port of The King of Fighters '98, using [KOF98 Recomp](https://github.com/danilopaulinodasilva/kof98recomp) and [LibSaturn](https://github.com/celsowm/libsaturn).

**Status: feasibility research — not playable.** The required baseline is NTSC 320×224 with original game timing and a **mandatory 4 MiB RAM expansion cartridge**. Stock RAM and 1 MiB cartridges are out of scope. The first gate is code size and execution speed, not graphics.

- [Engineering plan](docs/PORTING_PLAN.md)
- [Cross-port study: Tekken3 and Ikemen Saturn](docs/CROSS_PORT_ENGINEERING.md)
- [Contributor instructions](AGENTS.md)

## ROM-free host tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/dispatch_budget.py
```

The sparse and paged dispatchers have ROM-free tests and an optional source-level desktop integration. See [X0 dispatch experiments](docs/X0_DISPATCH_REPORT.md) for synthetic host checks and target caveats. No ROMs are necessary for the synthetic tests. Build the eventual game locally with legally obtained AES kof98h and Neo Geo BIOS dumps. No SNK ROMs, game assets, generated game code, traces, BIOS or binaries are distributed here.

## Credits

Danilo P. da Silva (KOF98 Recomp and toolkit improvements), sp00nznet (neogeorecomp), LibSaturn contributors, and SNK (original game and intellectual property). Unofficial fan research; not affiliated with SNK or Sega.

The [offline dispatch packer](docs/X1_GENERATED_DISPATCH.md) converts locally generated upstream tables to immutable paged C arrays. See the [host paged oracle build](docs/HOST_PAGED_ORACLE.md) for the new local integration; real ROM and Saturn verification remain pending.

Experimental [exact suffix sharing](docs/X2_SUFFIX_DEDUP.md) is available with `-DKOF_DEDUP_SUFFIX=ON` in a separate local host build. It is off by default until real-game parity and SH-2 size/timing are measured.

An independent [bounded CFG sharing candidate](docs/X2_CFG_DEDUP.md) is available with `-DKOF_DEDUP_CFG=ON`. Select one dedup strategy per build; both are experimental.
