#include <stdio.h>

#include "spf2shm.h"
#include "log.h"

int main(int argc, char** argv) {
    init_log("spf2tool.log", "wt");

    char shmname[256];
    sprintf(shmname, "spf2shm.%s", SHM_VER);
    spf2shm_init(shmname, 1);
    Logf("shm ver=%d, size=%ld", gsm->shm_version, gsm->shm_size);

    spf2shm_destroy();
}
