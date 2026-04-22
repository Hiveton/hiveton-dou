/*
 * Dedicated PSRAM allocator for network/TLS/WebSocket/HTTP buffers.
 */

#include "network_mem.h"

#include <rtthread.h>
#include <string.h>
#include "mem_section.h"

#define NETWORK_PSRAM_HEAP_SIZE (1024U * 1024U)
#define NETWORK_MEM_MAGIC       0x4E45544DU
#define NETWORK_MEM_LOG_LIMIT   3U

typedef struct
{
    uint32_t magic;
    uint32_t size;
} network_mem_hdr_t;

static struct rt_memheap s_network_psram_heap;
static rt_bool_t s_network_psram_heap_ready = RT_FALSE;
static uint32_t s_network_pool_alloc_count;
static uint32_t s_network_pool_free_count;
static uint32_t s_network_pool_fail_count;
static uint32_t s_network_pool_active_bytes;
static uint32_t s_network_pool_high_water_bytes;
static uint32_t s_network_pool_max_fail_size;

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(network_psram_heap_pool)
ALIGN(4) static uint8_t s_network_psram_heap_pool[NETWORK_PSRAM_HEAP_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(4) static uint8_t s_network_psram_heap_pool[NETWORK_PSRAM_HEAP_SIZE]
    L2_RET_BSS_SECT(network_psram_heap_pool);
#endif

static void network_mem_ensure_ready(void)
{
    if (s_network_psram_heap_ready)
    {
        return;
    }

    rt_memheap_init(&s_network_psram_heap, "net_psram",
                    s_network_psram_heap_pool,
                    sizeof(s_network_psram_heap_pool));
    s_network_psram_heap_ready = RT_TRUE;
}

static rt_bool_t network_mem_total_size(uint32_t size, rt_size_t *total_size)
{
    if (size > UINT32_MAX - sizeof(network_mem_hdr_t) - RT_ALIGN_SIZE)
    {
        return RT_FALSE;
    }

    *total_size = RT_ALIGN(size + sizeof(network_mem_hdr_t), RT_ALIGN_SIZE);
    return RT_TRUE;
}

static rt_bool_t network_mem_should_log_fail(uint32_t fail_count)
{
    return (fail_count <= NETWORK_MEM_LOG_LIMIT) ||
           ((fail_count & (fail_count - 1U)) == 0U);
}

static void network_mem_record_alloc(rt_size_t total_size)
{
    uint32_t bytes = (uint32_t)total_size;

    s_network_pool_alloc_count++;
    s_network_pool_active_bytes += bytes;
    if (s_network_pool_active_bytes > s_network_pool_high_water_bytes)
    {
        s_network_pool_high_water_bytes = s_network_pool_active_bytes;
    }
}

static void network_mem_record_free(rt_size_t total_size)
{
    uint32_t bytes = (uint32_t)total_size;

    s_network_pool_free_count++;
    if (s_network_pool_active_bytes >= bytes)
    {
        s_network_pool_active_bytes -= bytes;
    }
    else
    {
        s_network_pool_active_bytes = 0;
    }
}

void *network_mem_malloc(uint32_t size)
{
    rt_size_t total_size;
    network_mem_hdr_t *hdr;

    if (size == 0U || !network_mem_total_size(size, &total_size))
    {
        return RT_NULL;
    }

    network_mem_ensure_ready();
    hdr = (network_mem_hdr_t *)rt_memheap_alloc(&s_network_psram_heap,
                                                total_size);
    if (hdr == RT_NULL)
    {
        s_network_pool_fail_count++;
        if (size > s_network_pool_max_fail_size)
        {
            s_network_pool_max_fail_size = size;
        }
        if (network_mem_should_log_fail(s_network_pool_fail_count))
        {
            rt_kprintf("network_mem: alloc failed size=%u active=%u high=%u fail=%u max_fail=%u\n",
                       size, s_network_pool_active_bytes,
                       s_network_pool_high_water_bytes,
                       s_network_pool_fail_count,
                       s_network_pool_max_fail_size);
        }
        return RT_NULL;
    }

    hdr->magic = NETWORK_MEM_MAGIC;
    hdr->size = (uint32_t)total_size;
    network_mem_record_alloc(total_size);
    return (void *)(hdr + 1);
}

void *network_mem_calloc(uint32_t count, uint32_t size)
{
    uint32_t total;
    void *ptr;

    if (count == 0U || size == 0U)
    {
        return RT_NULL;
    }

    if (count > UINT32_MAX / size)
    {
        return RT_NULL;
    }

    total = count * size;
    ptr = network_mem_malloc(total);
    if (ptr != RT_NULL)
    {
        memset(ptr, 0, total);
    }

    return ptr;
}

void *network_mem_realloc(void *ptr, uint32_t newsize)
{
    network_mem_hdr_t *old_hdr;
    void *new_ptr;
    uint32_t old_payload_size;
    uint32_t copy_size;

    if (ptr == RT_NULL)
    {
        return network_mem_malloc(newsize);
    }

    if (newsize == 0U)
    {
        network_mem_free(ptr);
        return RT_NULL;
    }

    old_hdr = ((network_mem_hdr_t *)ptr) - 1;
    if (old_hdr->magic != NETWORK_MEM_MAGIC)
    {
        return RT_NULL;
    }

    old_payload_size = old_hdr->size > sizeof(network_mem_hdr_t)
                           ? old_hdr->size - sizeof(network_mem_hdr_t)
                           : 0U;
    if (newsize <= old_payload_size)
    {
        return ptr;
    }

    new_ptr = network_mem_malloc(newsize);
    if (new_ptr == RT_NULL)
    {
        return RT_NULL;
    }

    copy_size = old_payload_size < newsize ? old_payload_size : newsize;
    if (copy_size > 0U)
    {
        memcpy(new_ptr, ptr, copy_size);
    }
    network_mem_free(ptr);

    return new_ptr;
}

void network_mem_free(void *ptr)
{
    network_mem_hdr_t *hdr;
    rt_size_t total_size;

    if (ptr == RT_NULL)
    {
        return;
    }

    hdr = ((network_mem_hdr_t *)ptr) - 1;
    if (hdr->magic != NETWORK_MEM_MAGIC)
    {
        rt_kprintf("network_mem: invalid free %p\n", ptr);
        return;
    }

    total_size = hdr->size;
    hdr->magic = 0U;
    network_mem_record_free(total_size);
    rt_memheap_free(hdr);
}

char *network_mem_strdup(const char *str)
{
    size_t len;
    char *copy;

    if (str == RT_NULL)
    {
        return RT_NULL;
    }

    len = strlen(str) + 1U;
    if (len > UINT32_MAX)
    {
        return RT_NULL;
    }

    copy = (char *)network_mem_malloc((uint32_t)len);
    if (copy != RT_NULL)
    {
        memcpy(copy, str, len);
    }

    return copy;
}
