/**
 * @file lv_malloc_core_rtthread.c
 */

/*********************
 *      INCLUDES
 *********************/
#include "../lv_mem.h"
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_RTTHREAD
#include "../../stdlib/lv_mem.h"
#include <rtthread.h>
#include "mem_section.h"

void *audio_mem_malloc(uint32_t size) __attribute__((weak));
void *audio_mem_realloc(void *mem_address, unsigned int newsize) __attribute__((weak));
void audio_mem_free(void *ptr) __attribute__((weak));

#ifndef RT_USING_HEAP
    #error "lv_mem_core_rtthread: RT_USING_HEAP is required. Define it in rtconfig.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
#ifdef RT_USING_MEMHEAP
static struct rt_memheap s_lvgl_psram_heap;
static rt_bool_t s_lvgl_psram_heap_ready = RT_FALSE;
#endif

#define LVGL_PSRAM_HEAP_SIZE_MAX (1024U * 1024U)

#ifdef CONFIG_LV_MEM_SIZE
    #define LVGL_PSRAM_HEAP_SIZE_CFG ((rt_size_t)CONFIG_LV_MEM_SIZE)
#else
    #define LVGL_PSRAM_HEAP_SIZE_CFG LVGL_PSRAM_HEAP_SIZE_MAX
#endif

#if (LVGL_PSRAM_HEAP_SIZE_CFG > LVGL_PSRAM_HEAP_SIZE_MAX)
    #define LVGL_PSRAM_HEAP_SIZE LVGL_PSRAM_HEAP_SIZE_MAX
#else
    #define LVGL_PSRAM_HEAP_SIZE LVGL_PSRAM_HEAP_SIZE_CFG
#endif

#if defined(__CC_ARM) || defined(__CLANG_ARM)
L2_RET_BSS_SECT_BEGIN(lvgl_psram_heap_pool)
ALIGN(4) static uint8_t s_lvgl_psram_pool[LVGL_PSRAM_HEAP_SIZE];
L2_RET_BSS_SECT_END
#else
ALIGN(4) static uint8_t s_lvgl_psram_pool[LVGL_PSRAM_HEAP_SIZE]
    L2_RET_BSS_SECT(lvgl_psram_heap_pool);
#endif

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_mem_init(void)
{
#ifdef RT_USING_MEMHEAP
    rt_err_t err;

    if(s_lvgl_psram_heap_ready) {
        return;
    }

    err = rt_memheap_init(&s_lvgl_psram_heap,
                          "lvgl_psram",
                          s_lvgl_psram_pool,
                          sizeof(s_lvgl_psram_pool));
    if(err != RT_EOK) {
        rt_kprintf("lvgl rt mem init: memheap init failed, err=%d\n", err);
        return;
    }

    s_lvgl_psram_heap_ready = RT_TRUE;
    rt_kprintf("lvgl rt mem init: psram pool ready, size=%u\n",
               (unsigned int)sizeof(s_lvgl_psram_pool));
#endif
}

void lv_mem_deinit(void)
{
    return; /*No runtime deinit*/
}

lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes)
{
    /*Not supported*/
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    /*Not supported*/
    LV_UNUSED(pool);
    return;
}

void * lv_malloc_core(size_t size)
{
#ifdef RT_USING_MEMHEAP
    if(s_lvgl_psram_heap_ready) {
        return rt_memheap_alloc(&s_lvgl_psram_heap, size);
    }
#endif

    return rt_malloc(size);
}

void * lv_realloc_core(void * p, size_t new_size)
{
#ifdef RT_USING_MEMHEAP
    if(s_lvgl_psram_heap_ready) {
        return rt_memheap_realloc(&s_lvgl_psram_heap, p, new_size);
    }
#endif

    return rt_realloc(p, new_size);
}

void lv_free_core(void * p)
{
#ifdef RT_USING_MEMHEAP
    if(s_lvgl_psram_heap_ready) {
        rt_memheap_free(p);
        return;
    }
#endif

    rt_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    /*Not supported*/
    LV_UNUSED(mon_p);
    return;
}

lv_result_t lv_mem_test_core(void)
{
    /*Not supported*/
    return LV_RESULT_OK;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif /*LV_STDLIB_RTTHREAD*/
