#pragma once
#include "common.h"
#include "cmd_handler.h"

bool daemon_start(APP_STATE* state);
bool service_install(void);
bool service_uninstall(void);
void WINAPI service_main(DWORD argc, char* argv[]);
