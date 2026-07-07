#pragma once
#include "common.h"

RC raid_info(void);
RC raid_status(void);
RC raid_map(void);
RC raid_check(void);
RC raid_metadata(int argc, char* argv[]);
RC raid_planner(void);
RC raid_events(void);
