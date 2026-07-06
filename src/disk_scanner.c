#include "disk_scanner.h"

#define IOCTL_STORAGE_GET_LENGTH 0x002D4440
#ifndef IOCTL_DISK_GET_LENGTH_INFO
#define IOCTL_DISK_GET_LENGTH_INFO 0x0007405C
#endif

static DISK_TYPE detect_disk_type(const wchar_t* model) {
    if (wcsstr(model, L"NVMe") || wcsstr(model, L"nvme") || wcsstr(model, L"P3P") || wcsstr(model, L"FA200") || wcsstr(model, L"PM9A") || wcsstr(model, L"980") || wcsstr(model, L"990")) return DISK_TYPE_NVME_SSD;
    if (wcsstr(model, L"SSD") || wcsstr(model, L"ssd")) return DISK_TYPE_SATA_SSD;
    if (wcsstr(model, L"HDD") || wcsstr(model, L"hdd")) return DISK_TYPE_HDD;
    return DISK_TYPE_UNKNOWN;
}

bool disk_scan_all(DISK_INFO*** out_disks, uint32_t* out_count) {
    if (!out_disks || !out_count) return false;
    wchar_t logical_drives[256];
    DWORD len = GetLogicalDriveStringsW(256, logical_drives);
    if (len == 0 || len > 256) return false;

    DISK_INFO* disks = NULL;
    uint32_t count = 0;

    for (wchar_t* d = logical_drives; *d; d += wcslen(d) + 1) {
        if (d[0] == 'A' || d[0] == 'B') continue;
        UINT dt = GetDriveTypeW(d);
        if (dt != DRIVE_FIXED && dt != DRIVE_REMOVABLE) continue;

        wchar_t vol_path[64];
        StringCchPrintfW(vol_path, 64, L"\\\\.\\%c:", d[0]);
        HANDLE h = CreateFileW(vol_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        STORAGE_PROPERTY_QUERY query = {StorageDeviceProperty, PropertyStandardQuery};
        STORAGE_DEVICE_DESCRIPTOR* desc = NULL;
        char buf[4096];
        DWORD bytes = 0;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buf, 4096, &bytes, NULL)) {
            desc = (STORAGE_DEVICE_DESCRIPTOR*)buf;
        }

        void* new_disks = realloc(disks, (count + 1) * sizeof(DISK_INFO));
        if (!new_disks) { CloseHandle(h); continue; }
        disks = (DISK_INFO*)new_disks;
        DISK_INFO* disk = &disks[count];
        memset(disk, 0, sizeof(DISK_INFO));
        wcscpy_s(disk->device_path, MAX_DRIVE_PATH, vol_path);
        disk->drive_letter[0] = d[0]; disk->drive_letter[1] = L':'; disk->drive_letter[2] = L'\\'; disk->drive_letter[3] = 0;
        disk->handle = INVALID_HANDLE_VALUE;

        if (desc && desc->ProductIdOffset) {
            char product_id[64] = {0};
            strncpy_s(product_id, 63, buf + desc->ProductIdOffset, 63);
            for (char* p = product_id; *p; p++) if (*p == ' ') *p = '_';
            uint32_t si = (uint32_t)strlen(product_id);
            while (si > 0 && product_id[si - 1] == '_') product_id[--si] = 0;
            if (desc->VendorIdOffset) {
                char vendor[64] = {0};
                strncpy_s(vendor, 63, buf + desc->VendorIdOffset, 63);
                for (char* p = vendor; *p; p++) if (*p == ' ') *p = '_';
                uint32_t si2 = (uint32_t)strlen(vendor);
                while (si2 > 0 && vendor[si2 - 1] == '_') vendor[--si2] = 0;
                char model_a[MAX_MODEL_LEN];
                StringCchPrintfA(model_a, MAX_MODEL_LEN, "%s %s", vendor, product_id);
                mbstowcs(disk->model, model_a, MAX_MODEL_LEN - 1);
            } else {
                mbstowcs(disk->model, product_id, MAX_MODEL_LEN - 1);
            }
            if (desc->SerialNumberOffset)
                strncpy_s(disk->serial_number, MAX_SERIAL_LEN, buf + desc->SerialNumberOffset, _TRUNCATE);
        } else {
            StringCchPrintfW(disk->model, MAX_MODEL_LEN, L"Drive %c:", d[0]);
        }

        disk->type = detect_disk_type(disk->model);

        GET_LENGTH_INFORMATION gli;
        if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &gli, sizeof(gli), &bytes, NULL))
            disk->total_bytes = gli.Length.QuadPart;
        else if (DeviceIoControl(h, IOCTL_STORAGE_GET_LENGTH, NULL, 0, &gli, sizeof(gli), &bytes, NULL))
            disk->total_bytes = gli.Length.QuadPart;
        if (disk->total_bytes == 0) {
            ULARGE_INTEGER free_bytes, total_bytes;
            wchar_t root[4] = { (wchar_t)d[0], L':', L'\\', 0 };
            if (GetDiskFreeSpaceExW(root, &free_bytes, &total_bytes, NULL))
                disk->total_bytes = total_bytes.QuadPart;
        }

        STORAGE_ADAPTER_DESCRIPTOR adapter;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &adapter, sizeof(adapter), &bytes, NULL)) {
            // Use adapter info for sector size
        }
        disk->sector_size = 512;

        ULONG dev_num;
        DWORD ret_bytes;
        if (DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &dev_num, sizeof(dev_num), &ret_bytes, NULL))
            (void)dev_num;

        CloseHandle(h);
        count++;
    }

    *out_disks = (DISK_INFO**)malloc(count * sizeof(DISK_INFO*));
    if (!*out_disks) { free(disks); return false; }
    for (uint32_t i = 0; i < count; i++) {
        (*out_disks)[i] = (DISK_INFO*)malloc(sizeof(DISK_INFO));
        if (!(*out_disks)[i]) {
            for (uint32_t j = 0; j < i; j++) free((*out_disks)[j]);
            free(*out_disks); *out_disks = NULL;
            free(disks); return false;
        }
        memcpy((*out_disks)[i], &disks[i], sizeof(DISK_INFO));
    }
    free(disks);
    *out_count = count;
    return count > 0;
}

void disk_scan_free(DISK_INFO** disks, uint32_t count) {
    if (!disks) return;
    for (uint32_t i = 0; i < count; i++) free(disks[i]);
    free(disks);
}

bool disk_resolve_speed(DISK_INFO* disk) {
    if (!disk) return false;
    switch (disk->type) {
        case DISK_TYPE_NVME_SSD: disk->read_speed_mbs = 3500; disk->write_speed_mbs = 3000; return true;
        case DISK_TYPE_SATA_SSD: disk->read_speed_mbs = 550; disk->write_speed_mbs = 500; return true;
        case DISK_TYPE_HDD: disk->read_speed_mbs = 200; disk->write_speed_mbs = 150; return true;
        default: disk->read_speed_mbs = 100; disk->write_speed_mbs = 100; return true;
    }
}

bool disk_select(DISK_INFO** disks, uint32_t count, uint32_t* indices, uint32_t sel_count) {
    if (!disks || !indices || sel_count < MIN_DISKS || sel_count > MAX_DISKS) return false;
    for (uint32_t i = 0; i < count; i++) if (disks[i]) disks[i]->selected = false;
    for (uint32_t i = 0; i < sel_count; i++) {
        if (indices[i] >= count || !disks[indices[i]]) return false;
        disks[indices[i]]->selected = true;
    }
    return true;
}

bool disk_map_drive(const char* drive_letter, DISK_INFO* disk) {
    if (!drive_letter || !disk) return false;
    if (drive_letter[0] < 'A' || drive_letter[0] > 'Z') return false;
    wchar_t dl = (wchar_t)drive_letter[0];
    disk->drive_letter[0] = dl;
    disk->drive_letter[1] = L':';
    disk->drive_letter[2] = L'\\';
    disk->drive_letter[3] = 0;
    wchar_t root[64];
    StringCchPrintfW(root, 64, L"%c:\\RAIDTEST\\", dl);
    wcscpy_s(disk->drive_letter, MAX_DRIVE_PATH, root);
    CreateDirectoryW(root, NULL);
    wchar_t pool_path[MAX_DRIVE_PATH];
    StringCchPrintfW(pool_path, MAX_DRIVE_PATH, L"%s%s", root, POOL_FILENAME);
    wcscpy_s(disk->file_path, MAX_DRIVE_PATH, pool_path);
    wchar_t model_ext[256];
    StringCchPrintfW(model_ext, 256, L"%s (%c:)", disk->model, dl);
    wcscpy_s(disk->model, MAX_MODEL_LEN, model_ext);
    return true;
}

void disk_print_list(DISK_INFO** disks, uint32_t count) {
        printf("\n  %-3s %-28s %-12s %-10s %-10s %-8s %-8s\n",
               "ID", "Model", "Type", "Size", "Bench R", "Bench W", "Drive");
        printf("  %s\n", "------------------------------------------------------------------------------------------");
        for (uint32_t i = 0; i < count; i++) {
            DISK_INFO* d = disks[i];
            double gb = (double)d->total_bytes / (1024.0 * 1024.0 * 1024.0);
            char model_a[MAX_MODEL_LEN] = {0};
            wcstombs(model_a, d->model, MAX_MODEL_LEN - 1);
            char drive_a[32] = {0};
            wcstombs(drive_a, d->drive_letter, 31);
            uint32_t br = d->benchmarked ? d->bench_read_mbs : d->read_speed_mbs;
            uint32_t bw = d->benchmarked ? d->bench_write_mbs : d->write_speed_mbs;
            printf("  [%02u] %-26s %-12s %5.0f GB  %5u     %5u   %s%s\n",
               i, model_a, disk_type_str(d->type), gb, br, bw, drive_a, drive_a[0] ? "" : " (no drive)");
    }
    printf("\n");
}