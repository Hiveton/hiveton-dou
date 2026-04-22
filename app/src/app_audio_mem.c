/*
 * SPDX-FileCopyrightText: 2024-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <string.h>
#include "audio_mem.h"
#include "mem_section.h"

#define APP_AUDIO_PSRAM_HEAP_SIZE (1024 * 1024)
#define APP_AUDIO_PSRAM_THRESHOLD 256U
#define APP_AUDIO_MEM_MAGIC_PSRAM 0x5053524DU
#define APP_AUDIO_MEM_MAGIC_SYS   0x5359524DU
#define APP_AUDIO_MEM_LOG_FAIL_VERBOSE_LIMIT 3U

typedef struct
{
    uint32_t magic;
    uint32_t size;
} app_audio_mem_hdr_t;

static struct rt_memheap s_app_audio_psram_heap;
static rt_bool_t s_app_audio_psram_heap_ready = RT_FALSE;
static uint32_t s_app_audio_pool_alloc_count;
static uint32_t s_app_audio_pool_free_count;
static uint32_t s_app_audio_pool_fail_count;
static uint32_t s_app_audio_pool_active_bytes;
static uint32_t s_app_audio_pool_high_water_bytes;
static uint32_t s_app_audio_pool_max_fail_size;

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(app_audio_psram_heap_pool)
ALIGN(4) static uint8_t s_app_audio_psram_heap_pool[APP_AUDIO_PSRAM_HEAP_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(4) static uint8_t s_app_audio_psram_heap_pool[APP_AUDIO_PSRAM_HEAP_SIZE]
    L2_RET_BSS_SECT(app_audio_psram_heap_pool);
#endif

static rt_bool_t app_audio_mem_calc_total_size(uint32_t size,
                                               rt_size_t *total_size)
{
    if (size > UINT32_MAX - sizeof(app_audio_mem_hdr_t) - RT_ALIGN_SIZE)
    {
        return RT_FALSE;
    }

    *total_size = RT_ALIGN(size + sizeof(app_audio_mem_hdr_t), RT_ALIGN_SIZE);
    return RT_TRUE;
}

static rt_bool_t app_audio_mem_should_log_pool_fail(uint32_t fail_count)
{
    return (fail_count <= APP_AUDIO_MEM_LOG_FAIL_VERBOSE_LIMIT) ||
           ((fail_count & (fail_count - 1U)) == 0U);
}

static void app_audio_mem_record_pool_alloc(rt_size_t total_size)
{
    uint32_t bytes = (uint32_t)total_size;

    s_app_audio_pool_alloc_count++;
    s_app_audio_pool_active_bytes += bytes;
    if (s_app_audio_pool_active_bytes > s_app_audio_pool_high_water_bytes)
    {
        s_app_audio_pool_high_water_bytes = s_app_audio_pool_active_bytes;
    }
}

static void app_audio_mem_record_pool_free(rt_size_t total_size)
{
    uint32_t bytes = (uint32_t)total_size;

    s_app_audio_pool_free_count++;
    if (s_app_audio_pool_active_bytes >= bytes)
    {
        s_app_audio_pool_active_bytes -= bytes;
    }
    else
    {
        s_app_audio_pool_active_bytes = 0;
    }
}

static void app_audio_mem_record_pool_fail(uint32_t size,
                                           rt_size_t total_size,
                                           const char *reason)
{
    s_app_audio_pool_fail_count++;
    if (size > s_app_audio_pool_max_fail_size)
    {
        s_app_audio_pool_max_fail_size = size;
    }

    if (!app_audio_mem_should_log_pool_fail(s_app_audio_pool_fail_count))
    {
        return;
    }

    rt_kprintf("app audio psram alloc fail: req=%u total=%u reason=%s "
               "fail=%u max_fail=%u alloc=%u free=%u active=%u high=%u\n",
               (unsigned int)size,
               (unsigned int)total_size,
               reason,
               (unsigned int)s_app_audio_pool_fail_count,
               (unsigned int)s_app_audio_pool_max_fail_size,
               (unsigned int)s_app_audio_pool_alloc_count,
               (unsigned int)s_app_audio_pool_free_count,
               (unsigned int)s_app_audio_pool_active_bytes,
               (unsigned int)s_app_audio_pool_high_water_bytes);
}

static int app_audio_mem_init(void)
{
    rt_err_t err;

    err = rt_memheap_init(&s_app_audio_psram_heap,
                          "app_audio_psram",
                          s_app_audio_psram_heap_pool,
                          sizeof(s_app_audio_psram_heap_pool));
    if (err == RT_EOK)
    {
        s_app_audio_psram_heap_ready = RT_TRUE;
        rt_kprintf("app audio psram heap ready: %u bytes, fallback < %u bytes\n",
                   (unsigned int)sizeof(s_app_audio_psram_heap_pool),
                   (unsigned int)APP_AUDIO_PSRAM_THRESHOLD);
    }
    else
    {
        rt_kprintf("app audio psram heap init failed: %d\n", err);
    }

    return err;
}
INIT_PREV_EXPORT(app_audio_mem_init);

static void *app_audio_mem_alloc_internal(uint32_t size, rt_bool_t zeroed)
{
    app_audio_mem_hdr_t *hdr = RT_NULL;
    rt_size_t total_size;
    rt_bool_t use_psram = RT_FALSE;

    if (!app_audio_mem_calc_total_size(size, &total_size))
    {
        if (size >= APP_AUDIO_PSRAM_THRESHOLD)
        {
            app_audio_mem_record_pool_fail(size, 0, "oversize");
        }
        return RT_NULL;
    }

    if (size >= APP_AUDIO_PSRAM_THRESHOLD)
    {
        if (!s_app_audio_psram_heap_ready)
        {
            app_audio_mem_record_pool_fail(size, total_size, "not-ready");
            return RT_NULL;
        }

        hdr = (app_audio_mem_hdr_t *)rt_memheap_alloc(&s_app_audio_psram_heap,
                                                      total_size);
        if (hdr != RT_NULL)
        {
            use_psram = RT_TRUE;
            app_audio_mem_record_pool_alloc(total_size);
            if (zeroed)
            {
                rt_memset(hdr, 0, total_size);
            }
        }
        else
        {
            app_audio_mem_record_pool_fail(size, total_size, "empty");
            return RT_NULL;
        }
    }

    if (hdr == RT_NULL)
    {
        hdr = zeroed
                  ? (app_audio_mem_hdr_t *)rt_calloc(1, total_size)
                  : (app_audio_mem_hdr_t *)rt_malloc(total_size);
    }

    if (hdr == RT_NULL)
    {
        return RT_NULL;
    }

    hdr->magic = use_psram ? APP_AUDIO_MEM_MAGIC_PSRAM : APP_AUDIO_MEM_MAGIC_SYS;
    hdr->size = size;

    return (void *)(hdr + 1);
}

void *audio_mem_malloc(uint32_t size)
{
    return app_audio_mem_alloc_internal(size, RT_FALSE);
}

void *audio_mem_calloc(uint32_t count, uint32_t size)
{
    uint32_t total_size;

    if (count == 0 || size == 0)
    {
        total_size = 0;
    }
    else
    {
        if (count > UINT32_MAX / size)
        {
            return RT_NULL;
        }
        total_size = count * size;
    }

    return app_audio_mem_alloc_internal(total_size, RT_TRUE);
}

void audio_mem_free(void *ptr)
{
    app_audio_mem_hdr_t *hdr;

    if (ptr == RT_NULL)
    {
        return;
    }

    hdr = ((app_audio_mem_hdr_t *)ptr) - 1;
    if (hdr->magic == APP_AUDIO_MEM_MAGIC_PSRAM)
    {
        rt_size_t total_size;

        if (app_audio_mem_calc_total_size(hdr->size, &total_size))
        {
            app_audio_mem_record_pool_free(total_size);
        }
        rt_memheap_free(hdr);
        return;
    }

    rt_free(hdr);
}

void *audio_mem_realloc(void *mem_address, unsigned int newsize)
{
    app_audio_mem_hdr_t *hdr;
    void *new_ptr;
    rt_size_t copy_size;

    if (mem_address == RT_NULL)
    {
        return audio_mem_malloc(newsize);
    }

    if (newsize == 0)
    {
        audio_mem_free(mem_address);
        return RT_NULL;
    }

    hdr = ((app_audio_mem_hdr_t *)mem_address) - 1;
    copy_size = hdr->size < newsize ? hdr->size : newsize;

    new_ptr = audio_mem_malloc(newsize);
    if (new_ptr == RT_NULL)
    {
        return RT_NULL;
    }

    rt_memcpy(new_ptr, mem_address, copy_size);
    audio_mem_free(mem_address);
    return new_ptr;
}
