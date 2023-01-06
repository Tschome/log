#include "common.h"

void *realloc(void *ptr, size_t size)
{
    void *ret;
    if (size > atomic_load_explicit(&max_alloc_size, memory_order_relaxed))
        return NULL;

#if HAVE_ALIGNED_MALLOC
    ret = _aligned_realloc(ptr, size + !size, ALIGN);
#else
    ret = realloc(ptr, size + !size);
#endif
#if CONFIG_MEMORY_POISONING
    if (ret && !ptr)
        memset(ret, FF_MEMORY_POISON, size);
#endif
    return ret;
}

void *memdup(const void *p, size_t size)
{
    void *ptr = NULL;
    if (p) {
        ptr = malloc(size);
        if (ptr)
            memcpy(ptr, p, size);
    }
    return ptr;
}

void freep(void *arg)
{
    void *val;

    memcpy(&val, arg, sizeof(val));
    memcpy(arg, &(void *){ NULL }, sizeof(val));
    free(val);
}

