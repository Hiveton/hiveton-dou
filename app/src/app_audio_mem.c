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

typedef struct
{
    uint32_t magic;
    uint32_t size;
} app_audio_mem_hdr_t;

static struct rt_memheap s_app_audio_psram_heap;
static rt_bool_t s_app_audio_psram_heap_ready = RT_FALSE;

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(app_audio_psram_heap_pool)
ALIGN(4) static uint8_t s_app_audio_psram_heap_pool[APP_AUDIO_PSRAM_HEAP_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(4) static uint8_t s_app_audio_psram_heap_pool[APP_AUDIO_PSRAM_HEAP_SIZE]
    L2_RET_BSS_SECT(app_audio_psram_heap_pool);
#endif

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
        rt_kprintf("app audio psram heap ready: %u bytes\n",
                   (unsigned int)sizeof(s_app_audio_psram_heap_pool));
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
    rt_size_t total_size = RT_ALIGN(size + sizeof(app_audio_mem_hdr_t),
                                    RT_ALIGN_SIZE);
    rt_bool_t use_psram = RT_FALSE;

    if (s_app_audio_psram_heap_ready && size >= APP_AUDIO_PSRAM_THRESHOLD)
    {
        hdr = (app_audio_mem_hdr_t *)rt_memheap_alloc(&s_app_audio_psram_heap,
                                                      total_size);
        if (hdr != RT_NULL)
        {
            use_psram = RT_TRUE;
            if (zeroed)
            {
                rt_memset(hdr, 0, total_size);
            }
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
