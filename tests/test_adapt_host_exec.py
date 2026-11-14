import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from adapt_host_exec import adapt

# Synthetic structural fixture: not upstream source, no game bytes.
FIXTURE = """#include "ng_internal.h"
/* Entry tables for the regions */
static int s_tab_fixed, s_tab_bank, s_tab_bios;
static int s_interp_only;
static size_t s_nmiss;
/* The table slot for test */
static void *slot_in(void) { return 0; }
/* ---- interrupts ---- */
static void note_miss(uint32_t a) {
    slot(a);
}
/* Where interpretation starts */
void exec_report(void) {
    (void)total;
}
int exec_dump_misses(const char *path) {
    return 0;
}
"""

class TestAdapt(unittest.TestCase):
    def test_replacement(self):
        result=adapt(FIXTURE)
        self.assertIn('kof_paged_lookup(&kof_generated_dispatch',result)
        self.assertIn('nfuncs != kof_generated_dispatch.count',result)
        self.assertIn('s_miss_overflow',result)
        self.assertNotIn('s_tab_fixed',result)
        self.assertNotIn('slot_in(',result)
        self.assertIn('/* ---- interrupts ---- */',result)
    def test_drift_and_duplicate_anchor(self):
        for bad in (FIXTURE.replace('/* The table slot for','/* changed'), FIXTURE+'\nstatic size_t s_nmiss;', FIXTURE+'\nvoid *unexpected = s_tab_fixed;'):
            with self.assertRaises(ValueError): adapt(bad)

if __name__=='__main__': unittest.main()
