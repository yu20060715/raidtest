#include "metadata_manager.h"
#include "event_bus.h"

bool metadata_read(const wchar_t* drive_root, SUPERBLOCK* sb_out) {
    return superblock_read_raw(drive_root, sb_out);
}

bool metadata_write(STRIPE_VOLUME* vol) {
    bool ok = superblock_write(vol);
    if (ok) event_bus_publish(EVENT_METADATA_UPDATED, "superblock written");
    return ok;
}

bool metadata_validate(const SUPERBLOCK* sb) {
    if (!sb || sb->magic != SUPERBLOCK_MAGIC) return false;
    if (sb->version < 1 || sb->version > SUPERBLOCK_VERSION) return false;
    if (sb->disk_count < MIN_DISKS || sb->disk_count > MAX_DISKS) return false;
    if (sb->stripe_unit == 0) return false;
    return true;
}

bool metadata_upgrade(SUPERBLOCK* sb) {
    if (!sb) return false;
    if (sb->version == SUPERBLOCK_VERSION) return true;
    return false; /* raw upgrade not supported — superblock_read() handles it */
}

void metadata_dump(const SUPERBLOCK* sb, char* out, size_t out_size) {
    superblock_format_str(sb, out, out_size);
}

bool metadata_restore(const SUPERBLOCK* sb, DISK_INFO** physical_disks, uint32_t physical_count,
                      STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count_out) {
    return superblock_restore(sb, physical_disks, physical_count, vol, disks_out, disk_count_out);
}

bool metadata_load_volume(const wchar_t* drive_root, DISK_INFO** physical_disks, uint32_t physical_count,
                          STRIPE_VOLUME* vol, DISK_INFO* disks_out, uint32_t* disk_count_out) {
    return superblock_read(drive_root, vol, disks_out, disk_count_out,
                           physical_disks, physical_count);
}
