# KOF98 Saturn

An experimental native Sega Saturn port of The King of Fighters '98, using the static 68000 recompilation work of [KOF98 Recomp](https://github.com/danilopaulinodasilva/kof98recomp) and the [LibSaturn](https://github.com/celsowm/libsaturn) SDK.

**Status: feasibility research. Not playable.** The first objective is to establish that the original game logic fits and runs within SH-2 memory and timing budgets. No emulator or hardware performance claim is made yet.

The target is NTSC 320×224, original game timing, initially with a 4 MiB RAM expansion cartridge. The disc is built locally from legally obtained AES `kof98h` and Neo Geo BIOS dumps. No SNK ROMs, derived source, traces, graphics, audio, BIOS or game binaries are distributed here.

See [the porting plan](docs/PORTING_PLAN.md) for the engineering roadmap.

## Credits

- Danilo P. da Silva — KOF98 Recomp and Neo Geo toolkit improvements.
- sp00nznet — upstream neogeorecomp static recompilation toolkit.
- LibSaturn contributors — native Saturn SDK.
- SNK — original game and intellectual property.

Unofficial fan research project; not affiliated with SNK or Sega.
