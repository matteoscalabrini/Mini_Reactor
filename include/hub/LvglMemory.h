#pragma once
#include <stddef.h>
#include "esp_heap_caps.h"
static inline void* hubLvglMalloc(size_t s)            { return heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static inline void  hubLvglFree(void* p)               { heap_caps_free(p); }
static inline void* hubLvglRealloc(void* p, size_t s)  { return heap_caps_realloc(p, s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
