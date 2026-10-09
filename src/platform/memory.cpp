#include "memory.hpp"
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(_WIN32)
#include <malloc.h>
#endif

namespace hive {
void release_unused_memory() {
#if defined(__APPLE__)
    malloc_zone_pressure_relief(nullptr,0);
#elif defined(_WIN32)
    _heapmin();
#endif
}
}
