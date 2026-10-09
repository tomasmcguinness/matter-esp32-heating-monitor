#pragma once
// Host stand-in for ESP-IDF's esp_heap_caps.h: there is one heap.
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT   0
#define heap_caps_calloc(n, size, caps) calloc((n), (size))
