/* mount_cdm.c — minimal mount helper for CDM testing
 * Uses existing RAID API. Does NOT modify any project source.
 * Usage: mount_cdm <drive_letter> [timeout_seconds]
 *   Mounts volume, prints result to stdout, keeps mounted for timeout
 *   seconds (default 0 = indefinite), then unmounts and exits.
 *   Pass timeout 0 and run check_cdm.exe in another window.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "raid_service.h"
#include "device_manager.h"
#include "volume_manager.h"
#include "config.h"

int main(int argc, char* argv[]) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: mount_cdm <drive_letter> [timeout_seconds]\n");
        return 1;
    }
    char letter = argv[1][0];
    if (letter >= 'a' && letter <= 'z') letter -= 32;

    int timeout = 0;
    if (argc >= 3) timeout = atoi(argv[2]);

    raid_init();
    RC rc = raid_scan();
    if (rc != RC_OK) { printf("FAIL raid_scan=%d\n", rc); raid_cleanup(); return 1; }

    uint32_t count = device_get_count();
    uint32_t sel[4], sel_n = 0;
    for (uint32_t i = 0; i < count && sel_n < 4; i++) {
        DISK_INFO* d = device_get(i);
        if (d->healthy && d->drive_letter[0] != 'C') sel[sel_n++] = i;
    }
    if (sel_n < 2) { printf("FAIL not enough disks (%u)\n", sel_n); raid_cleanup(); return 1; }

    device_select(sel, sel_n);

    char* args[4]; int argc_i = 0;
    char bufs[4][32];
    for (uint32_t i = 0; i < sel_n; i++) {
        snprintf(bufs[i], 32, "%u:512", sel[i]);
        args[argc_i++] = bufs[i];
    }
    rc = raid_init_pools(argc_i, args);
    if (rc != RC_OK) { printf("FAIL raid_init_pools=%d\n", rc); raid_cleanup(); return 1; }

    rc = raid_create();
    if (rc != RC_OK) { printf("FAIL raid_create=%d\n", rc); raid_cleanup(); return 1; }

    rc = raid_mount(letter);
    if (rc != RC_OK) { printf("FAIL raid_mount=%d\n", rc); raid_cleanup(); return 1; }

    printf("MOUNTED %c:\n", letter);

    if (timeout > 0) {
        Sleep(timeout * 1000);
    } else {
        printf("WAITING\n");
        while (1) {
            FILE* f = fopen("mount_cdm_stop.txt", "r");
            if (f) { fclose(f); remove("mount_cdm_stop.txt"); break; }
            Sleep(1000);
        }
    }

    raid_unmount();
    raid_destroy();
    raid_cleanup();
    printf("UNMOUNTED\n");
    return 0;
}
