#pragma once
#include "common.h"

bool config_save(APP_CONFIG* cfg);
bool config_load(APP_CONFIG* cfg);
bool config_get_path(wchar_t* path, uint32_t max_len);
void config_defaults(APP_CONFIG* cfg);