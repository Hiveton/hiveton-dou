/*
 * Dedicated PSRAM allocator for Bluetooth/BLE stack buffers.
 *
 * The SDK default bt_mem_alloc/bt_mem_free symbols are weak aliases to
 * rt_malloc/rt_free. On this product the normal RT heap is too small once UI,
 * network and services are active, so BLE Core can fail during mem_env_config
 * while allocating env/msg/nt/log buffers. Providing strong symbols here moves
 * those allocations to PSRAM.
 */

#include <rtthread.h>
#include <stdint.h>
#include <string.h>
#include "mem_section.h"

#define BT_PSRAM_HEAP_SIZE (1024U * 1024U)
#define BT_MEM_MAGIC       0x42544D45U
#define BT_MEM_LOG_LIMIT   3U

typedef struct
{
    uint32_t magic;
    uint32_t size;
} bt_mem_hdr_t;

static struct rt_memheap s_bt_psram_heap;
static rt_bool_t s_bt_psram_heap_ready = RT_FALSE;
static uint32_t s_bt_pool_alloc_count;
static uint32_t s_bt_pool_free_count;
static uint32_t s_bt_pool_fail_count;
static uint32_t s_bt_pool_active_bytes;
static uint32_t s_bt_pool_high_water_bytes;
static uint32_t s_bt_pool_max_fail_size;

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(bt_psram_heap_pool)
ALIGN(4) static uint8_t s_bt_psram_heap_pool[BT_PSRAM_HEAP_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(4) static uint8_t s_bt_psram_heap_pool[BT_PSRAM_HEAP_SIZE]
    L2_RET_BSS_SECT(bt_psram_heap_pool);
#endif

static void bt_mem_ensure_ready(void)
{
    if (s_bt_psram_heap_ready)
    {
        return;
    }

    rt_memheap_init(&s_bt_psram_heap, "bt_psram",
                    s_bt_psram_heap_pool,
                    sizeof(s_bt_psram_heap_pool));
    s_bt_psram_heap_ready = RT_TRUE;
}

static rt_bool_t bt_mem_total_size(rt_size_t size, rt_size_t *total_size)
{
    if (size == 0U)
    {
        return RT_FALSE;
    }

    if (size > UINT32_MAX - sizeof(bt_mem_hdr_t) - RT_ALIGN_SIZE)
    {
        return RT_FALSE;
    }

    *total_size = RT_ALIGN(size + sizeof(bt_mem_hdr_t), RT_ALIGN_SIZE);
    return RT_TRUE;
}

static rt_bool_t bt_mem_should_log_fail(uint32_t fail_count)
{
    return (fail_count <= BT_MEM_LOG_LIMIT) ||
           ((fail_count & (fail_count - 1U)) == 0U);
}

static void bt_mem_record_alloc(rt_size_t total_size)
{
    uint32_t bytes = (uint32_t)total_size;

    s_bt_pool_alloc_count++;
    s_bt_pool_active_bytes += bytes;
    if (s_bt_pool_active_bytes > s_bt_pool_high_water_bytes)
    {
        s_bt_pool_high_water_bytes = s_bt_pool_active_bytes;
    }
}

static void bt_mem_record_free(rt_size_t total_size)
{
    uint32_t bytes = (uint32_t)total_size;

    s_bt_pool_free_count++;
    if (s_bt_pool_active_bytes >= bytes)
    {
        s_bt_pool_active_bytes -= bytes;
    }
    else
    {
        s_bt_pool_active_bytes = 0U;
    }
}

void *bt_mem_alloc(rt_size_t size)
{
    rt_size_t total_size;
    bt_mem_hdr_t *hdr;

    if (!bt_mem_total_size(size, &total_size))
    {
        return RT_NULL;
    }

    bt_mem_ensure_ready();
    hdr = (bt_mem_hdr_t *)rt_memheap_alloc(&s_bt_psram_heap, total_size);
    if (hdr == RT_NULL)
    {
        s_bt_pool_fail_count++;
        if (size > s_bt_pool_max_fail_size)
        {
            s_bt_pool_max_fail_size = (uint32_t)size;
        }
        if (bt_mem_should_log_fail(s_bt_pool_fail_count))
        {
            rt_kprintf("bt_mem: alloc failed size=%u active=%u high=%u fail=%u max_fail=%u\n",
                       (uint32_t)size,
                       s_bt_pool_active_bytes,
                       s_bt_pool_high_water_bytes,
                       s_bt_pool_fail_count,
                       s_bt_pool_max_fail_size);
        }
        return RT_NULL;
    }

    hdr->magic = BT_MEM_MAGIC;
    hdr->size = (uint32_t)total_size;
    bt_mem_record_alloc(total_size);
    return (void *)(hdr + 1);
}

void *bt_mem_calloc(rt_size_t count, rt_size_t size)
{
    rt_size_t total;
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
    ptr = bt_mem_alloc(total);
    if (ptr != RT_NULL)
    {
        memset(ptr, 0, total);
    }

    return ptr;
}

void bt_mem_free(void *ptr)
{
    bt_mem_hdr_t *hdr;
    rt_size_t total_size;

    if (ptr == RT_NULL)
    {
        return;
    }

    hdr = ((bt_mem_hdr_t *)ptr) - 1;
    if (hdr->magic != BT_MEM_MAGIC)
    {
        rt_kprintf("bt_mem: invalid free %p\n", ptr);
        return;
    }

    total_size = hdr->size;
    hdr->magic = 0U;
    bt_mem_record_free(total_size);
    rt_memheap_free(hdr);
}
