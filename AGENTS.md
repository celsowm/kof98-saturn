# Contributor instructions

Read docs/PORTING_PLAN.md before changing the architecture. Keep the host oracle and Saturn backend separated. Report evidence rather than guessing playable performance. Do not commit game or BIOS ROMs, generated recompilation, local traces or game-derived asset content. Keep all of these under ignored local/build directories and do not upload them to CI artifacts. Host tests must work without ROMs. Preserve credits and license notices for any imported open-source code. Use LibSaturn as an installed SDK, not a copy. Prioritize Phase A feasibility and compact dispatch before graphics, avoid scope creep. Build/tests should be reproducible and distinguish host, emulator, and real hardware evidence. Do not silently mask dispatch misses or resource overflows.

The 4 MiB RAM expansion cartridge is mandatory. Do not spend effort on stock RAM or 1 MiB compatibility. Keep separate WRAM-H/L, cart-bank, VRAM and Sound RAM budgets; the cart is data-only in the baseline.

Read docs/CROSS_PORT_ENGINEERING.md as a normative companion. Use its X0–X8 work packages and measurement/acceptance rules. Keep game-specific code out of LibSaturn and do not modify the reference ports as part of this project.
