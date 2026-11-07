#include "util.h"

#include <esp_heap_caps.h>

void *ps_alloc(size_t bytes)
{
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    return p;
}

void ps_free(void *p)
{
    if (p) heap_caps_free(p);
}
