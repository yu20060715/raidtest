#pragma once
#include "common.h"

typedef enum {
    EVENT_DISK_FOUND,
    EVENT_DISK_REMOVED,
    EVENT_DISK_BENCHED,
    EVENT_VOLUME_CREATED,
    EVENT_VOLUME_LOADED,
    EVENT_VOLUME_DESTROYED,
    EVENT_MOUNT,
    EVENT_UNMOUNT,
    EVENT_REBUILD,
    EVENT_EXPAND,
    EVENT_METADATA_UPDATED,
    EVENT_CACHE_CHANGED,
    EVENT_ERROR,
    EVENT_COUNT
} EVENT_TYPE;

typedef void (*event_callback)(EVENT_TYPE type, const char* data, void* userdata);

void event_bus_init(void);
void event_bus_subscribe(EVENT_TYPE type, event_callback cb, void* userdata);
void event_bus_unsubscribe(EVENT_TYPE type, event_callback cb);
void event_bus_publish(EVENT_TYPE type, const char* data);
void event_bus_flush_to_file(void);
void event_bus_display(void);

const char* event_type_str(EVENT_TYPE type);
