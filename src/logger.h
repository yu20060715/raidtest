#pragma once
#include <stdio.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_OK,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE
} LOG_LEVEL;

void log_init(void);
void log_cleanup(void);
void log_set_level(LOG_LEVEL min_level);
void log_printf(LOG_LEVEL level, const char* fmt, ...);
