#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "log.h"
#include "spf2shm.h"

struct Spf2Shm* gsm = NULL;
int is_creator = 0;
char shmname[256];

int spf2shm_init(const char* name, int create_exclusive) {
    strcpy(shmname, name);
    Logf("init shm:%s", name);
    int fd;
    is_creator = create_exclusive;
    if (create_exclusive) {
        if (1) {
            char shmpath[256];
            sprintf(shmpath, "/dev/shm/%s", name);
            if (access(shmpath, F_OK) == 0) {
                if (unlink(shmpath) != 0) {
                    fprintf(stderr, "err: cannot remove %s", shmpath);
                    exit(1);
                }
            }
        }
        fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    }
    else {
        fd = shm_open(name, O_RDWR, S_IRUSR | S_IWUSR);
    }
    if (fd == -1) {
        fprintf(stderr, "err: spf2shm_init(%s): %d, %s\n", name, errno, strerror(errno));
        exit(1);
    }
    if (ftruncate(fd, sizeof(struct Spf2Shm)) == -1) {
        fprintf(stderr, "err: spf2shm_init ftruncate(%s): %d\n", name, errno);
        exit(1);
    }
    gsm = (struct Spf2Shm*)mmap(NULL, sizeof(struct Spf2Shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (gsm == MAP_FAILED) {
        fprintf(stderr, "err: mmap: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    if (create_exclusive) {
        memset(gsm, 0x00, sizeof(struct Spf2Shm));
        gsm->shm_version = atoi(SHM_VER);
        gsm->shm_size = sizeof(struct Spf2Shm);
    }
    else {
        if (gsm->shm_size != sizeof(struct Spf2Shm)) {
            Logf("err: my shm size=%ld, theirs size=%ld", sizeof(struct Spf2Shm), gsm->shm_size);
            fprintf(stderr, "err: my shm size=%ld, theirs size=%ld\n", sizeof(struct Spf2Shm), gsm->shm_size);
            exit(EXIT_FAILURE);
        }
    }
    return 1;
}

int spf2shm_destroy() {
    munmap(gsm, sizeof(struct Spf2Shm));
    if (is_creator) {
        shm_unlink(shmname);
    }
    return 1;
}

struct SymbolInfo* alloc_symbol() {
    struct SymbolInfo* ret = NULL;
    if (gsm->symbol_cnt < MAX_SYMBOLS) {
        ++gsm->symbol_cnt;
        ret = &gsm->symbols[gsm->symbol_cnt - 1];
        // Logf("alloc symbol: %d", gsm->symbol_cnt - 1);
    }
    return ret;
}

struct SymbolRootInfo* alloc_symbol_root() {
    struct SymbolRootInfo* ret = NULL;
    if (gsm->symbol_group_cnt < MAX_SYMBOL_ROOTS) {
        ++gsm->symbol_group_cnt;
        ret = &gsm->roots[gsm->symbol_group_cnt - 1];
        // Logf("alloc symbol root: %d", gsm->symbol_group_cnt - 1);
    }
    return ret;
}

void add_event(uint32_t midx, int64_t fm) {
    gsm->events.data[gsm->events.writepos].midx = midx;
    gsm->events.data[gsm->events.writepos].field_map = fm;
    gsm->events.writepos = (gsm->events.writepos + 1) % MaxEventQueueEntries;
}
