/* check_cdm.c — simulate CrystalDiskMark API call sequence
 * Does NOT modify any project source. Reports first failure.
 */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <string.h>

static int g_step = 0;
static int g_failed = 0;

static void check(int ok, const char* api, const char* detail) {
    g_step++;
    if (ok) {
        printf("  PASS [%02d] %s : %s\n", g_step, api, detail);
    } else {
        printf("  FAIL [%02d] %s : %s\n", g_step, api, detail);
        g_failed = g_step;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: check_cdm <drive_letter>\n");
        return 1;
    }
    char letter = argv[1][0];
    if (letter >= 'a' && letter <= 'z') letter -= 32;
    if (letter < 'A' || letter > 'Z') {
        fprintf(stderr, "Invalid drive letter: %c\n", letter);
        return 1;
    }

    char root[8];    snprintf(root, sizeof(root), "%c:\\", letter);
    char volpath[8]; snprintf(volpath, sizeof(volpath), "\\\\.\\%c:", letter);
    char testfile[64]; snprintf(testfile, sizeof(testfile), "%c:\\cdm_test_XXXX.tmp", letter);

    printf("CrystalDiskMark API Simulation for %c:\n", letter);
    printf("=========================================\n\n");

    /* 1. Check drive exists */
    DWORD drives = GetLogicalDrives();
    int bit = letter - 'A';
    check((drives & (1u << bit)) != 0,
          "GetLogicalDrives", "drive exists in bitmask");
    if (g_failed) goto done;

    /* 2. GetVolumeInformation */
    {
        char fsname[64] = {0};
        DWORD flags = 0, maxc = 0;
        BOOL ok = GetVolumeInformationA(root, NULL, 0, NULL, &maxc, &flags, fsname, sizeof(fsname));
        check(ok, "GetVolumeInformationA",
              ok ? fsname : "FAILED");
        if (g_failed) goto done;
    }

    /* 3. GetDiskFreeSpaceEx */
    {
        ULARGE_INTEGER free_avail, total, total_free;
        BOOL ok = GetDiskFreeSpaceExA(root, &free_avail, &total, &total_free);
        check(ok, "GetDiskFreeSpaceExA",
              ok ? "OK" : "FAILED");
        if (g_failed) goto done;
        printf("         Total=%llu MB Free=%llu MB\n",
               total.QuadPart / (1024*1024),
               free_avail.QuadPart / (1024*1024));
    }

    /* 4. CreateFile (create new test file) */
    HANDLE hFile = CreateFileA(testfile, GENERIC_READ | GENERIC_WRITE,
                               0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    check(hFile != INVALID_HANDLE_VALUE,
          "CreateFile (CREATE_ALWAYS)", hFile != INVALID_HANDLE_VALUE ? "OK" : "FAILED");
    if (g_failed) goto done;

    /* 5. WriteFile */
    {
        char buf[65536];
        memset(buf, 0xAB, sizeof(buf));
        DWORD written = 0;
        BOOL ok = WriteFile(hFile, buf, sizeof(buf), &written, NULL);
        check(ok && written == sizeof(buf), "WriteFile (64KB)",
              ok ? "OK" : "FAILED");
        if (g_failed) { CloseHandle(hFile); goto done; }
    }

    /* 6. FlushFileBuffers */
    {
        BOOL ok = FlushFileBuffers(hFile);
        check(ok, "FlushFileBuffers", ok ? "OK" : "FAILED");
        if (g_failed) { CloseHandle(hFile); goto done; }
    }

    /* 7. GetFileSizeEx */
    {
        LARGE_INTEGER size;
        BOOL ok = GetFileSizeEx(hFile, &size);
        check(ok, "GetFileSizeEx",
              ok ? "OK" : "FAILED");
        if (g_failed) { CloseHandle(hFile); goto done; }
        printf("         File size = %lld bytes\n", size.QuadPart);
    }

    /* 8. CloseHandle */
    {
        BOOL ok = CloseHandle(hFile);
        check(ok, "CloseHandle", ok ? "OK" : "FAILED");
        if (g_failed) goto done;
    }

    /* 9. DeleteFile */
    {
        BOOL ok = DeleteFileA(testfile);
        check(ok, "DeleteFileA", ok ? "OK" : "FAILED");
        if (g_failed) goto done;
    }

    /* 10. DeviceIoControl: IOCTL_DISK_GET_LENGTH_INFO */
    {
        HANDLE hVol = CreateFileA(volpath, 0,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);
        check(hVol != INVALID_HANDLE_VALUE,
              "CreateFile (volume handle)", hVol != INVALID_HANDLE_VALUE ? "OK" : "FAILED");
        if (g_failed) goto done;

        GET_LENGTH_INFORMATION gli;
        DWORD ret = 0;
        BOOL ok = DeviceIoControl(hVol, IOCTL_DISK_GET_LENGTH_INFO,
                                   NULL, 0, &gli, sizeof(gli), &ret, NULL);
        check(ok, "IOCTL_DISK_GET_LENGTH_INFO", ok ? "OK" : "FAILED");
        if (!g_failed) {
            printf("         Volume length = %llu bytes (%llu MB)\n",
                   gli.Length.QuadPart, gli.Length.QuadPart / (1024*1024));
        }

        /* 11. IOCTL_STORAGE_QUERY_PROPERTY */
        {
            STORAGE_PROPERTY_QUERY spq;
            memset(&spq, 0, sizeof(spq));
            spq.PropertyId = StorageDeviceProperty;
            spq.QueryType = PropertyStandardQuery;

            char buf[1024];
            DWORD ret2 = 0;
            ok = DeviceIoControl(hVol, IOCTL_STORAGE_QUERY_PROPERTY,
                                  &spq, sizeof(spq), buf, sizeof(buf), &ret2, NULL);
            check(ok, "IOCTL_STORAGE_QUERY_PROPERTY", ok ? "OK" : "FAILED");
            if (ok && ret2 >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
                STORAGE_DEVICE_DESCRIPTOR* sdd = (STORAGE_DEVICE_DESCRIPTOR*)buf;
                printf("         DeviceType=0x%02x BusType=0x%02x\n",
                       sdd->DeviceType, sdd->BusType);
            }
        }

        CloseHandle(hVol);
    }

done:
    printf("\n=========================================\n");
    if (g_failed == 0)
        printf("RESULT: ALL PASSED\n");
    else
        printf("RESULT: FAILED at step %d\n", g_failed);
    return g_failed;
}
