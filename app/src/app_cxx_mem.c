/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <stddef.h>
#include "audio_mem.h"

extern int rt_in_system_heap(void *ptr);

void *cxx_mem_allocate(size_t size)
{
    void *ptr;

    ptr = audio_mem_malloc((uint32_t)size);
    if (ptr != RT_NULL)
    {
        return ptr;
    }

    rt_kprintf("cxx alloc fallback system heap: size=%u\n",
               (unsigned int)size);
    return rt_malloc((rt_size_t)size);
}

void cxx_mem_free(void *ptr)
{
    if (ptr == RT_NULL)
    {
        return;
    }

    if (rt_in_system_heap(ptr))
    {
        rt_free(ptr);
        return;
    }

    audio_mem_free(ptr);
}
