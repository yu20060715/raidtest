#pragma once
#include "common.h"
#include "cmd_handler.h"

void cleanup_cache(APP_STATE* state);
void cleanup_volume(APP_STATE* state);
void cleanup_pool_files(APP_STATE* state);
void cleanup_disks(APP_STATE* state);
void cleanup_session(APP_STATE* state);
void cleanup_pool_session(APP_STATE* state);
void cleanup_bench_dirs(void);
void cleanup_all(void);
