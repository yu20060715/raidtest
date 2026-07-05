#pragma once
#include "common.h"
#include "stripe_engine.h"

bool fuse_mount_volume(STRIPE_VOLUME* vol, char drive_letter);
bool fuse_unmount_volume(char drive_letter);