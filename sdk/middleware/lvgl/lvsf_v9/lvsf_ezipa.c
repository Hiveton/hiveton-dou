/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*********************
 *      INCLUDES
 *********************/
#include "lvsf_ezipa.h"

#ifdef USING_EZIPA_DEC
#include "app_mem.h"
// #include "lv_img_buf.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS &lv_ezipa_class

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void ezipa_start_timer(lv_obj_t *ezipa);
static void ezipa_stop_timer(lv_obj_t *ezipa);
static void ezipa_timer_cb(struct _lv_anim_t *a);
static void lv_ezipa_constructor(const lv_obj_class_t *class_p, lv_obj_t *ezipa);
static void lv_ezipa_destructor(const lv_obj_class_t *class_p, lv_obj_t *ezipa);
static void lv_ezipa_event(const lv_obj_class_t *class_p, lv_event_t *e);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
extern int32_t gpu_ezipa_draw(ezipa_obj_t *obj, const lv_area_t *src_area, const lv_layer_t *layer, bool next);

const lv_obj_class_t lv_ezipa_class =
{
    .constructor_cb = lv_ezipa_constructor,
    .destructor_cb = lv_ezipa_destructor,
    .event_cb = lv_ezipa_event,
    .instance_size = sizeof(lv_ezipa_ext_t),
    .base_class = &lv_obj_class,
};

static void lv_ezipa_constructor(const lv_obj_class_t *class_p, lv_obj_t *ezipa)
{
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");

    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;

    ext->loop_cnt = EZIPA_LOOP_COUNT_DEINIT;
    ext->loop_times = EZIPA_LOOP_FOREVER;
    ext->ezipa_dec = NULL;
    ext->status = LV_EZIPA_STOP;
    ext->surface = NULL;
    ext->ezipa_src = NULL;
    ext->play_end = NULL;
    rt_memset(&ext->img_dsc, 0, sizeof(ext->img_dsc));
    ext->orig_src = NULL;
    ext->prefix = NULL;
    ext->surface_prefix = NULL;
    ext->splice_src = NULL;
    ext->surface_src = NULL;
    ext->max_num = 0;
    ext->delay_play_time = 0;
    ext->delay_play_num = 0;

    lv_anim_init(&ext->anim);
    lv_anim_set_var(&ext->anim, ezipa);
    lv_anim_set_ready_cb(&ext->anim, ezipa_timer_cb);

    LV_TRACE_OBJ_CREATE("finished");
}

static void lv_ezipa_destructor(const lv_obj_class_t *class_p, lv_obj_t *ezipa)
{
    lv_anim_del(ezipa, NULL);
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;

    if (ext->ezipa_dec)
    {
        ezipa_close(ext->ezipa_dec);
        ext->ezipa_dec = NULL;
    }
    if (ext->prefix) rt_free(ext->prefix);
    if (ext->surface_prefix) rt_free(ext->surface_prefix);
    if (ext->splice_src) rt_free(ext->splice_src);
    if (ext->surface_src) rt_free(ext->surface_src);
}


lv_obj_t *lv_ezipa_create(lv_obj_t *parent)
{
#ifdef DRV_EPIC_NEW_API
    // lv_obj_t *screen = lv_obj_get_screen(parent);
    // lv_obj_set_render_async(screen, 0);
#endif
    lv_obj_t *ezipa = lv_obj_class_create_obj(MY_CLASS, parent);
    lv_obj_class_init_obj(ezipa);
    return ezipa;
}

void lv_ezipa_set_src(lv_obj_t *ezipa, const char *src)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;

    if (!ext->ezipa_dec)
    {
        ext->ezipa_dec = ezipa_open((const uint8_t *)src, EZIPA_OUTPUT_CF);
        LV_ASSERT_NULL(ext->ezipa_dec);
        lv_obj_set_size(ezipa, (lv_coord_t)ext->ezipa_dec->header.width,
                        (lv_coord_t)ext->ezipa_dec->header.height);

        ext->img_dsc.header.always_zero = 0;
        ext->img_dsc.header.w = (lv_coord_t)ext->ezipa_dec->header.width;
        ext->img_dsc.header.h = (lv_coord_t)ext->ezipa_dec->header.height;
        ext->img_dsc.data_size  = ext->ezipa_dec->pixel_size * ext->img_dsc.header.w * ext->img_dsc.header.h;
        ext->img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        ext->img_dsc.data = (uint8_t *)ext->ezipa_dec->output_buf;
        RT_ASSERT(ext->img_dsc.data);
    }
    ext->ezipa_src = (const void *)src;
    ext->status = LV_EZIPA_STOP;
}

void lv_ezipa_set_surface(lv_obj_t *ezipa, const void *src)
{

    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);

    if (!ext->surface)
    {
        ext->surface = lv_img_create(ezipa);
    }
    lv_img_set_src(ext->surface, src);
    lv_obj_align(ext->surface, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ext->surface, LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_move_foreground(ext->surface);
    ext->orig_src = src;
}

void lv_ezipa_set_zoom(lv_obj_t *ezipa, uint16_t zoom)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);
    lv_img_set_src(ext->surface, &ext->img_dsc);
    if (zoom == 0) zoom = 1;
    //rt_kprintf("%s: zoom %d\n", __func__, zoom);
    lv_img_set_zoom(ext->surface, zoom);
    lv_obj_set_size(ezipa, (lv_coord_t)ext->ezipa_dec->header.width * zoom / LV_IMG_ZOOM_NONE + 1,
                    (lv_coord_t)ext->ezipa_dec->header.height * zoom / LV_IMG_ZOOM_NONE + 1);

    //lv_img_set_src(ext->surface, ext->orig_src);
}

void lv_ezipa_set_opa(lv_obj_t *ezipa, uint16_t opa)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);
    lv_img_set_src(ext->surface, &ext->img_dsc);
    lv_obj_set_style_img_opa(ext->surface, opa, 0);
    //rt_kprintf("%s: opa %d\n", __func__, opa);
}

void lv_ezipa_set_interval(lv_obj_t *ezipa, int32_t interval)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;

    ext->interval = interval;
}

void lv_ezipa_pause(lv_obj_t *ezipa)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);
    if (ext->surface)
    {
        lv_img_set_src(ext->surface, ext->orig_src);
    }
    ezipa_stop_timer(ezipa);
    if (ext->ezipa_dec)
    {
        ezipa_close(ext->ezipa_dec);
        ext->ezipa_dec = NULL;
        ext->status = LV_EZIPA_STOP;
    }
    ext->loop_cnt = EZIPA_LOOP_COUNT_DEINIT;
}

void lv_ezipa_set_loop_times(lv_obj_t *ezipa, int times)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);
    ext->loop_times = times;
}

void lv_ezipa_set_play_end_cb(lv_obj_t *ezipa, lv_ezipa_end_cb_t cb)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);
    ext->play_end = cb;
}

void lv_ezipa_resume(lv_obj_t *ezipa)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);

    if (!ext->ezipa_dec)
    {
        ext->ezipa_dec = ezipa_open((const uint8_t *)ext->ezipa_src, EZIPA_OUTPUT_CF);
        LV_ASSERT_NULL(ext->ezipa_dec);
    }
    if (ext->status == LV_EZIPA_STOP)
    {
        ezipa_start_timer(ezipa);
        ezipa_timer_cb(&ext->anim);
    }
    if (ext->surface)
    {
        lv_img_set_src(ext->surface, &ext->img_dsc);
    }
    //rt_kprintf("%s: ext->ezipa_dec %p \n", __func__, ext->ezipa_dec);
}

void lv_ezipa_resume_with_delay(lv_obj_t *ezipa, uint16_t delay_time)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);
    ext->delay_play_time = delay_time;
    if (!ext->delay_play_time)
    {
        lv_ezipa_resume(ezipa);
    }
    else
    {
        if (!ext->ezipa_dec)
        {
            ext->ezipa_dec = ezipa_open((const uint8_t *)ext->ezipa_src, EZIPA_OUTPUT_CF);
            LV_ASSERT_NULL(ext->ezipa_dec);
        }
        if (ext->status == LV_EZIPA_STOP)
        {
            ezipa_start_timer(ezipa);
            ezipa_timer_cb(&ext->anim);
        }
    }
}

void lv_ezipa_set_select_prefix(lv_obj_t *ezipa, const void *ezipa_prefix, const void *surface_prefix, uint8_t max_num)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext);
    if (!ext->prefix)
    {
        ext->prefix = rt_malloc(strlen(ezipa_prefix) + 1);
        RT_ASSERT(ext->prefix);
    }
    else
    {
        ext->prefix = rt_realloc(ext->prefix, strlen(ezipa_prefix) + 1);
        RT_ASSERT(ext->prefix);
    }
    if (!ext->surface_prefix)
    {
        ext->surface_prefix = rt_malloc(strlen(surface_prefix) + 1);
        RT_ASSERT(ext->surface_prefix);
    }
    else
    {
        ext->surface_prefix = rt_realloc(ext->surface_prefix, strlen(surface_prefix) + 1);
        RT_ASSERT(ext->surface_prefix);
    }

    strcpy(ext->surface_prefix, surface_prefix);
    strcpy(ext->prefix, ezipa_prefix);
    ext->max_num = max_num;
    //rt_kprintf("%s:%s\n",__func__, ext->prefix);
}


void lv_ezipa_select(lv_obj_t *ezipa, uint8_t idx)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    RT_ASSERT(ext && ext->prefix);
    RT_ASSERT(idx < ext->max_num);
    if (!ext->splice_src)
    {
        ext->splice_src = rt_malloc(strlen(ext->prefix) + strlen(".bin") + 6);
        RT_ASSERT(ext->splice_src);
    }
    if (!ext->surface_src)
    {
        ext->surface_src = rt_malloc(strlen(ext->surface_prefix) + strlen(".bin") + 6);
        RT_ASSERT(ext->surface_src);
    }
    rt_snprintf(ext->splice_src, strlen(ext->prefix) + strlen(".bin") + 6, "%s%02d.bin", ext->prefix, idx);
    rt_snprintf(ext->surface_src, strlen(ext->surface_prefix) + strlen(".bin") + 6, "%s%02d.bin", ext->surface_prefix, idx);
    lv_ezipa_pause(ezipa);
    lv_ezipa_set_src(ezipa, ext->splice_src);
    lv_ezipa_set_surface(ezipa, ext->surface_src);
    lv_ezipa_resume(ezipa);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static uint32_t get_next_delay_ms(ezipa_obj_t *ezipa_dec)
{
    if (0 == ezipa_dec->next_frame.delay_den)
    {
        ezipa_dec->next_frame.delay_den = 100;
    }
    return ezipa_dec->next_frame.delay_num * 1000 / ezipa_dec->next_frame.delay_den;
}

static void ezipa_timer_cb(struct _lv_anim_t *a)
{
    lv_obj_t *ezipa = (lv_obj_t *) a->var;
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;

    ext->status = LV_EZIPA_NEXT;

    lv_obj_invalidate(ezipa);
}


static void ezipa_start_timer(lv_obj_t *ezipa)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    if (ext->interval > 0)
    {
        lv_anim_set_time(&ext->anim, ext->interval);
    }
    else
    {
        lv_anim_set_time(&ext->anim, get_next_delay_ms(ext->ezipa_dec));
    }
    lv_anim_start(&ext->anim);
}

static void ezipa_stop_timer(lv_obj_t *ezipa)
{
    lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
    lv_anim_del(ezipa, NULL);
}

static void lv_ezipa_event(const lv_obj_class_t *class_p, lv_event_t *e)
{
    LV_UNUSED(class_p);
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ezipa = lv_event_get_target(e);

    /*Ancestor events will be called during drawing*/
    lv_res_t res = lv_obj_event_base(MY_CLASS, e);
    if (res != LV_RES_OK)
    {
        return;
    }
    if (code == LV_EVENT_COVER_CHECK)
    {
        lv_cover_check_info_t *info;

        info = lv_event_get_param(e);
        info->res = LV_COVER_RES_COVER;
    }
    else if (code == LV_EVENT_DRAW_MAIN)
    {
        int32_t r;
        lv_ezipa_ext_t *ext = (lv_ezipa_ext_t *)ezipa;
#ifndef DISABLE_LVGL_V8
        lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
        const lv_area_t *clip_area = draw_ctx->clip_area;
#else
        lv_layer_t *layer = lv_event_get_layer(e);
        const lv_area_t *clip_area = &layer->_clip_area;
#endif /* DISABLE_LVGL_V8 */

        if (LV_EZIPA_STOP != ext->status)
        {
            //ignore delay time;
            if (ext->delay_play_time)
            {
                if (ext->delay_play_num * ext->anim.duration < ext->delay_play_time)
                {
                    ext->delay_play_num++;
                    /*Start next frame update timer*/
                    ezipa_start_timer(ezipa);
                    return;
                }
                else
                {
                    ext->delay_play_time = 0;
                    ext->delay_play_num = 0;
                    lv_img_set_src(ext->surface, &ext->img_dsc);
                }
            }
#ifndef DISABLE_LVGL_V8
            r = gpu_ezipa_draw(ext->ezipa_dec, &ezipa->coords, clip_area, (LV_EZIPA_NEXT == ext->status));
#else
            r = gpu_ezipa_draw(ext->ezipa_dec, &ezipa->coords, layer, (LV_EZIPA_NEXT == ext->status));
#endif
            ext->img_dsc.data = ext->ezipa_dec->output_buf;
            if (LV_EZIPA_NEXT == ext->status)
            {
                if (ext->loop_times != EZIPA_LOOP_FOREVER)
                {
                    //rt_kprintf("%s frame_num %d ,cur_play_idx %d\n", __func__, ext->ezipa_dec->header.frame_num, ext->ezipa_dec->curr_frame.seq_num);
                    if (ext->ezipa_dec->header.frame_num ==  ext->ezipa_dec->curr_frame.seq_num + 1)
                    {
                        ext->loop_cnt++;
                    }
                    if (ext->loop_cnt == ext->loop_times)
                    {
                        rt_kprintf("%s: play end ,loop_cnt=ext->loop_times=%d\n", __func__, ext->loop_cnt);
                        lv_ezipa_pause(ezipa);
                        if (ext->play_end) ext->play_end(ezipa);
                        return;
                    }
                }
                /*Start next frame update timer*/
                ezipa_start_timer(ezipa);
            }
            ext->status = LV_EZIPA_CURR;
        }
    }
}

#endif

