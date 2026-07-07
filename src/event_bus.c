#include "event_bus.h"

#define MAX_SUBSCRIBERS_PER_EVENT 16

typedef struct {
    event_callback cb;
    void* userdata;
    bool active;
} Subscriber;

static Subscriber g_subs[EVENT_COUNT][MAX_SUBSCRIBERS_PER_EVENT];
static CRITICAL_SECTION g_eb_cs;
static volatile LONG g_eb_inited = 0;

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
    if (InterlockedExchange(&g_eb_inited, 1)) return;
    InitializeCriticalSection(&g_eb_cs);
    memset(g_subs, 0, sizeof(g_subs));
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
            g_subs[type][i].cb = NULL;
            g_subs[type][i].userdata = NULL;
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
            s.cb(type, data, s.userdata);
        }
    }
    LeaveCriticalSection(&g_eb_cs);
}
