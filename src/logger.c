#include "common.h"
#include <stdarg.h>

static LOG_LEVEL g_min_level = LOG_LEVEL_INFO;
static FILE* g_log_file = NULL;
static bool g_timestamp = false;
static CRITICAL_SECTION g_log_lock;
static bool g_log_init_done = false;

void log_init(void) {
    if (g_log_init_done) return;
    InitializeCriticalSection(&g_log_lock);
    g_min_level = LOG_LEVEL_INFO;
    g_log_file = NULL;
    g_timestamp = true;
    g_log_init_done = true;
}

void log_cleanup(void) {
    if (g_log_file) { fclose(g_log_file); g_log_file = NULL; }
    if (g_log_init_done) { DeleteCriticalSection(&g_log_lock); g_log_init_done = false; }
}

void log_set_level(LOG_LEVEL min_level) { g_min_level = min_level; }

static const char* level_label(LOG_LEVEL level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_OK:    return "OK";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "?";
    }
}

static const char* level_prefix(LOG_LEVEL level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "[DEBUG] ";
        case LOG_LEVEL_INFO:  return "[INFO]  ";
        case LOG_LEVEL_OK:    return "[OK]    ";
        case LOG_LEVEL_WARN:  return "[WARN]  ";
        case LOG_LEVEL_ERROR: return "[ERROR] ";
        default:              return "[?]     ";
    }
}

void log_printf(LOG_LEVEL level, const char* fmt, ...) {
    if (level < g_min_level) return;

    char time_buf[32] = "";
    if (g_timestamp) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(time_buf, 32, "[%02u:%02u:%02u] ", st.wHour, st.wMinute, st.wSecond);
    }

    va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);

    EnterCriticalSection(&g_log_lock);

    printf("%s%s", time_buf, level_prefix(level));
    vprintf(fmt, args);
    printf("\n");

    if (g_log_file) {
        fprintf(g_log_file, "%s[%-5s] ", time_buf, level_label(level));
        vfprintf(g_log_file, fmt, args2);
        fputc('\n', g_log_file);
        fflush(g_log_file);
    }

    va_end(args2);
    va_end(args);
    LeaveCriticalSection(&g_log_lock);
}
