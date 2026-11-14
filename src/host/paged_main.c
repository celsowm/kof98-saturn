/* Private host comparison executable, not a Saturn application. */
#include "game.h"
#include "kof98/paged_dispatch.h"

extern const kof_paged_dispatch kof_generated_dispatch;

int main(int argc, char **argv)
{
    /* The adapted exec_init validates this contract before executing code. */
    return ng_main(argc, argv, &k_kof98, NULL, kof_generated_dispatch.count);
}
