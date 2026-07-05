#include "event_bus.h"

#define MAX_SUBSCRIBERS_PER_EVENT 16

typedef struct {
    event_callback cb;
    void* userdata;
    bool active;
} Subscriber;

static Subscriber g_subs[EVENT_COUNT][MAX_SUBSCRIBERS_PER_EVENT];
static CRITICAL_SECTION g_eb_cs;
static bool g_eb_inited = false;

static const char* g_names[EVENT_COUNT] = {
    "DISK_FOUND", "DISK_REMOVED", "DISK_BENCHED",
    "VOLUME_CREATED", "VOLUME_LOADED", "VOLUME_DESTROYED",
    "MOUNT", "UNMOUNT", "REBUILD", "EXPAND",
    "METADATA_UPDATED", "CACHE_CHANGED", "ERROR"
};

const char* event_type_str(EVENT_TYPE type) {
    if (type >= 0 && type < EVENT_COUNT) return g_names[type];
    return "UNKNOWN";
}

void event_bus_init(void) {
    if (g_eb_inited) return;
    InitializeCriticalSection(&g_eb_cs);
    memset(g_subs, 0, sizeof(g_subs));
    g_eb_inited = true;
}

void event_bus_subscribe(EVENT_TYPE type, event_callback cb, void* userdata) {
    if (!g_eb_inited || !cb || type >= EVENT_COUNT) return;
    EnterCriticalSection(&g_eb_cs);
    for (int i = 0; i < MAX_SUBSCRIBERS_PER_EVENT; i++) {
        if (!g_subs[type][i].active) {
            g_subs[type][i].cb = cb;
            g_subs[type][i].userdata = userdata;
            g_subs[type][i].active = true;
            break;
        }
    }
    LeaveCriticalSection(&g_eb_cs);
}

void event_bus_unsubscribe(EVENT_TYPE type, event_callback cb) {
    if (!g_eb_inited || !cb || type >= EVENT_COUNT) return;
    EnterCriticalSection(&g_eb_cs);
    for (int i = 0; i < MAX_SUBSCRIBERS_PER_EVENT; i++) {
        if (g_subs[type][i].active && g_subs[type][i].cb == cb) {
            g_subs[type][i].active = false;
            break;
        }
    }
    LeaveCriticalSection(&g_eb_cs);
}

void event_bus_publish(EVENT_TYPE type, const char* data) {
    if (!g_eb_inited || type >= EVENT_COUNT) return;
    EnterCriticalSection(&g_eb_cs);
    for (int i = 0; i < MAX_SUBSCRIBERS_PER_EVENT; i++) {
        if (g_subs[type][i].active) {
            Subscriber s = g_subs[type][i];
            LeaveCriticalSection(&g_eb_cs);
            s.cb(type, data, s.userdata);
            EnterCriticalSection(&g_eb_cs);
        }
    }
    LeaveCriticalSection(&g_eb_cs);
}

void event_bus_display(void) {
    if (!g_eb_inited) return;
    LOG_INFO("Event bus has %d event types with subscribers", EVENT_COUNT);
}

void event_bus_flush_to_file(void) {
    /* No-op: file logging is handled by the subscriber in raid_service */
}
