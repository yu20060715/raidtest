#pragma once
#include "common.h"
#include "cmd_handler.h"
#include "device_manager.h"
#include "metadata_manager.h"
#include "volume_manager.h"
#include "planner_engine.h"
#include "raid_query.h"

/* ---- Lifecycle ---- */
RC raid_init(void);
void raid_cleanup(void);

/* ---- Scan / Discover ---- */
RC raid_scan(void);
RC raid_select(int argc, char* argv[]);
RC raid_mapdrive(uint32_t disk_id, const char* drive_letter);
RC raid_bench(int argc, char* argv[]);

/* ---- Init / Create ---- */
RC raid_init_pools(int argc, char* argv[]);
RC raid_create(void);
RC raid_mirror(void);
RC raid_expand(int argc, char* argv[]);
RC raid_rebuild(int argc, char* argv[]);

/* ---- Mount / Load / Destroy ---- */
RC raid_mount(char drive_letter);
RC raid_unmount(void);
RC raid_load(const wchar_t* drive_root);
RC raid_destroy(void);
RC raid_purge(void);

/* ---- Cache ---- */
RC raid_cache(int argc, char* argv[]);

/* ---- Info / Check / Simulate ---- */
RC raid_info(void);
RC raid_status(void);
RC raid_map(void);
RC raid_test(void);
RC raid_random(int argc, char** args);
RC raid_benchfs(int argc, char* argv[]);
RC raid_benchraw(int argc, char* argv[]);
RC raid_check(void);
RC raid_simulate(int argc, char* argv[]);
RC raid_metadata(int argc, char* argv[]);
RC raid_planner(void);
RC raid_events(void);

/* ---- Config ---- */
RC raid_config_save(void);
RC raid_config_load(void);

/* ---- Wizard / Quick ---- */
RC raid_wizard(void);
RC raid_quick(void);
