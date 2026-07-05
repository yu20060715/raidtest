#pragma once
#include "common.h"

bool pool_file_create(DISK_INFO* disk, uint64_t size_bytes);
bool pool_file_open(DISK_INFO* disk);
void pool_file_close(DISK_INFO* disk);
bool pool_file_delete(DISK_INFO* disk);
bool pool_dir_delete(DISK_INFO* disk);