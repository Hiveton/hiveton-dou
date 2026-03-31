# 图形渲染驱动层
图形渲染驱动层对上层应用提供了更友好的图像渲染接口，这里面有2种不同类型的实现接口（不能同时使用）：
1. 单步渲染模式  -- 功能简单，对图形库要求低
2. 渲染列表模式  -- 功能更多，对图形库的内存管理要求高

两种渲染模式的区别：

|   | 单步渲染模式 | 渲染列表模式 |
|--------------------------------- |:-----:|:------:|
| 支持填充、混叠、旋转、缩放         |   Y   |    Y  |
| 支持画线、圆、多边形、圆弧         |   N   |    Y  |
| 支持55x系列                       |   Y   |   N   |
| 渲染的输入buffer允许局部变量       |   Y   |   N   |

## 如何选择渲染模式
通过以下menuconfig开关选择哪种模式（打开是渲染列表模式，关闭则是单步渲染模式）：
![渲染列表模式menuconfig](../../assets/driver_epic_render_list_menuconfig.png)

## 单步渲染模式
单步渲染的接口带cbk参数的都是异步接口，需要等待cbk回调完成后，渲染才结束。


接口中常见的参数`input_layers`和`input_layer_cnt`说明：
- `input_layer_cnt` 顾名思义是`input_layers`的个数
- `input_layers`是混叠的输入层，每层的混叠顺序是`input_layers[0]`在最底下, `input_layers[1]`在`input_layers[0]`的上面，依次叠加。需要特别指出的是mask layer的mask对象是mask层底下的那一层。

*** Mask层是格式是A2/A4/A8，并且ax_mode = ALPHA_BLEND_MASK 的输入层 ***

### 1. 填充函数
  支持纯色填充，渐变色填充，带mask的纯色填充
```
rt_err_t drv_epic_fill_ext(EPIC_LayerConfigTypeDef *input_layers,
                           uint8_t input_layer_cnt,
                           EPIC_LayerConfigTypeDef *output_canvas,
                           drv_epic_cplt_cbk cbk);

rt_err_t drv_epic_fill(uint32_t dst_cf, uint8_t *dst,
                       const EPIC_AreaTypeDef *dst_area,
                       const EPIC_AreaTypeDef *fill_area,
                       uint32_t argb8888,
                       uint32_t mask_cf, const uint8_t *mask,
                       const EPIC_AreaTypeDef *mask_area,
                       drv_epic_cplt_cbk cbk);

rt_err_t drv_epic_fill_grad(EPIC_GradCfgTypeDef *param,
                            drv_epic_cplt_cbk cbk);
```

### 2. 区域复制接口

将src buffer的某一区域，复制导致dst buffer的对应区域，src和dst可以是不同格式

```
rt_err_t drv_epic_copy(const uint8_t *src, uint8_t *dst,
                       const EPIC_AreaTypeDef *src_area,
                       const EPIC_AreaTypeDef *dst_area,
                       const EPIC_AreaTypeDef *copy_area,
                       uint32_t src_cf, uint32_t dst_cf,
                       drv_epic_cplt_cbk cbk);
```

### 3. 图片混叠接口（包括平面旋转、缩放）
支持带mask的图片混叠、旋转、缩放。

举例来说，我需要将一个图片蒙上一个mask，然后混叠到一个framebuffer上，那么输入是：
- `input_layer_cnt`是3, 
- `input_layers[0]`就是背景层
- `input_layers[1]`就是图片
- `input_layers[2]`就是mask，它只对`input_layers[1]`层起mask作用。
- `output_canvas`就是输出buffer，在该例子中跟是`input_layers[0]`操作的同一个buffer，只是因为混叠区域只是图片所在的区域，所以`output_canvas`层的高宽可能会比`input_layers[0]`小。

具体可以参考HAL_EPIC层混叠对输出区域剪切的部分的解释。

```
rt_err_t drv_epic_blend(EPIC_LayerConfigTypeDef *input_layers,
                        uint8_t input_layer_cnt,
                        EPIC_LayerConfigTypeDef *output_canvas,
                        drv_epic_cplt_cbk cbk);
```


### 4. 绕x/y轴3D翻转的接口

```
rt_err_t drv_epic_transform(EPIC_LayerConfigTypeDef *input_layers,
                            uint8_t input_layer_cnt,
                            EPIC_LayerConfigTypeDef *output_canvas,
                            drv_epic_cplt_cbk cbk);
```

### 5. 连续混叠接口
```
rt_err_t drv_epic_cont_blend(EPIC_LayerConfigTypeDef *input_layers,
                             uint8_t input_layer_cnt,
                             EPIC_LayerConfigTypeDef *output_canvas);

void drv_epic_cont_blend_reset(void);
```


## 渲染列表模式
渲染列表模式是将一系列渲染操作搜集起来，然后一起渲染的一种模式，所以要求在这些渲染操作完成前，所有相关的内存引用都不能变动。

### 渲染列表API使用流程
渲染列表在执行时有2种场景：
1. 渲染+送屏
   这种场景会根据配置的渲染buffer大小，分块的去渲染区域，然后每块完成就执行指定的回调，在回调里面去刷屏幕（或者其他操作）
1. 渲染到指定buffer
   这种场景是直接将渲染结果写到指定buffer，整个完成后再调用回调。

#### 渲染+送屏
这种场景有操作示例可以参考`SiFli-SDK_1\example\rt_device\gpu\render_list_mode`示例。
```c

static void partial_done_cb(drv_epic_render_list_t rl, EPIC_LayerConfigTypeDef *p_dst, void *usr_data, uint32_t last)
{
    rt_device_t p_lcd_device = usr_data;
    lcd_flush(p_lcd_device, p_dst); //将渲染完成的buffer显示到屏幕上
}

void main(void)
{
    drv_gpu_open(); //打开驱动
    drv_epic_setup_render_buffer(&buf1, &buf2, buf_size); //设置渲染buffer(需要2个一样大小的buffer)

    /*
        渲染输出buffer的颜色格式、大小定义，由于`EPIC_MSG_RENDER_DRAW`并不会直接画到这个buffer，所以不需要给真实的地址。
        `EPIC_MSG_RENDER_DRAW`消息在每完成一小块渲染就会执行`partial_done_cb`将局部的渲染结果返回。
    */
    drv_epic_render_buf virtual_render_buf;
    virtual_render_buf.cf = EPIC_INPUT_RGB888;
    virtual_render_buf.data = (uint8_t *)0xCCCCCCCC;
    virtual_render_buf.area.x0 = 0;
    virtual_render_buf.area.y0 = 0;
    virtual_render_buf.area.x1 = LCD_HOR_RES_MAX - 1;
    virtual_render_buf.area.y1 = LCD_VER_RES_MAX - 1;


    /*申请新的列表*/
    drv_epic_render_list_t rl;
    EPIC_AreaTypeDef ow_area;
    rl = drv_epic_alloc_render_list(&virtual_render_buf, &ow_area);
    RT_ASSERT(rl != NULL);

    /*添加渲染操作：全屏的灰色矩形*/
    drv_epic_operation *o = drv_epic_alloc_op(p_buf);
    o->op = DRV_EPIC_DRAW_RECT;
    o->clip_area.x0 = 0;
    o->clip_area.y0 = 0;
    o->clip_area.x1 = LCD_HOR_RES_MAX - 1;
    o->clip_area.y1 = LCD_VER_RES_MAX - 1;
    HAL_EPIC_LayerConfigInit(&o->mask);
    o->desc.rectangle.area = o->clip_area;
    o->desc.rectangle.radius = 0;
    o->desc.rectangle.top_fillet = 0;
    o->desc.rectangle.bot_fillet = 0;
    o->desc.rectangle.argb8888 = 0xFF808080;
    drv_epic_commit_op(o);


    /* 提交渲染列表,开始渲染 */
    EPIC_MsgTypeDef msg;
    msg.id = EPIC_MSG_RENDER_DRAW;
    msg.render_list = rl;
    msg.content.rd.area.x0 = 0;  //rd.area 指定在virtual_render_buf上面需要更新的区域
    msg.content.rd.area.y0 = 0;
    msg.content.rd.area.x1 = LCD_HOR_RES_MAX - 1;
    msg.content.rd.area.y1 = LCD_VER_RES_MAX - 1;
    msg.content.rd.usr_data = NULL;
    msg.content.rd.pixel_align = pixel_align;  //像素对齐要求
    msg.content.rd.partial_done_cb = partial_done_cb;  //局部渲染完成后的回调


    drv_epic_render_msg_commit(&msg);
}
```

#### 渲染到指定buffer
将一个渲染列表的内容渲染到指定buffer中，支持渲染完成后缩放到指定buffer的尺寸。

```c
    drv_gpu_open(); //打开驱动
    drv_epic_setup_render_buffer(&buf1, &buf2, buf_size); //设置渲染buffer(需要2个一样大小的buffer)

    /*
        渲染输出buffer的颜色格式、尺寸、以及地址
        这里的尺寸可以跟实际的buffer尺寸不一致，实际buffer的真实尺寸在最后提交的时候提供。
    */
    drv_epic_render_buf virtual_render_buf;
    virtual_render_buf.cf = EPIC_INPUT_RGB888;
    virtual_render_buf.data = (uint8_t *)0x60000000;
    virtual_render_buf.area.x0 = 0;
    virtual_render_buf.area.y0 = 0;
    virtual_render_buf.area.x1 = LCD_HOR_RES_MAX - 1;
    virtual_render_buf.area.y1 = LCD_VER_RES_MAX - 1;


    /*申请新的列表*/
    drv_epic_render_list_t rl;
    EPIC_AreaTypeDef ow_area;
    rl = drv_epic_alloc_render_list(&virtual_render_buf, &ow_area);
    RT_ASSERT(rl != NULL);

    /*添加渲染操作：全屏的灰色矩形*/
    drv_epic_operation *o = drv_epic_alloc_op(p_buf);
    o->op = DRV_EPIC_DRAW_RECT;
    o->clip_area.x0 = 0;
    o->clip_area.y0 = 0;
    o->clip_area.x1 = LCD_HOR_RES_MAX - 1;
    o->clip_area.y1 = LCD_VER_RES_MAX - 1;
    HAL_EPIC_LayerConfigInit(&o->mask);
    o->desc.rectangle.area = o->clip_area;
    o->desc.rectangle.radius = 0;
    o->desc.rectangle.top_fillet = 0;
    o->desc.rectangle.bot_fillet = 0;
    o->desc.rectangle.argb8888 = 0xFF808080;
    drv_epic_commit_op(o);


    /* 提交渲染列表,开始渲染 */
    EPIC_MsgTypeDef msg;
    msg.id = EPIC_MSG_RENDER_TO_BUF;
    msg.render_list = rl;
    msg.content.r2b.dst_area.x0 = 0;  //dst_area 代表的是 virtual_render_buf 真实的大小
    msg.content.r2b.dst_area.y0 = 0;
    msg.content.r2b.dst_area.x1 = LCD_HOR_RES_MAX - 1;
    msg.content.r2b.dst_area.y1 = LCD_VER_RES_MAX - 1;
    msg.content.r2b.usr_data = NULL;
    msg.content.r2b.done_cb = done_cb;  //渲染完成后的回调


    drv_epic_render_msg_commit(&msg);
```

### 具体渲染操作的参数说明
本节主要说明结构体`drv_epic_operation`的参数

每个渲染操作公共参数的说明：
```c
    drv_epic_op_type_t  op; //渲染操作类型
    EPIC_AreaTypeDef clip_area;  //渲染限制区域
    EPIC_LayerConfigTypeDef mask;  //渲染操作的bitmap mask（可选）
```


不同渲染操作数据结构`drv_epic_operation.desc`的参数说明：
1. 矩形(DRV_EPIC_DRAW_RECT)
```c
        struct
        {
            EPIC_AreaTypeDef area; //矩形所在坐标区域

            uint16_t radius;  //圆角半径
            uint8_t top_fillet; //顶部圆角使能
            uint8_t bot_fillet; //底部圆角使能

            uint32_t argb8888;//透明度以及颜色
        } rectangle;
```
2. 图片(DRV_EPIC_DRAW_IMAGE)
```c
        struct
        {
            EPIC_LayerConfigTypeDef layer;  //图片的描述层，里面包括了旋转、缩放等描述
            uint8_t use_dest_as_bg; //如何与背景混叠的选项，可以设置固定颜色、也可以与背景混叠、或者直接原图输出
            uint8_t r; //固定背景色混叠时的背景色
            uint8_t g;
            uint8_t b;
        } blend;
```
3. 圆弧(DRV_EPIC_DRAW_ARC)
```c
        struct
        {
            int16_t center_x; //圆心坐标
            int16_t center_y;

            uint16_t start_angle; //起始角度，单位0.1度
            uint16_t end_angle;   //结束角度

            uint16_t width;    //圆弧粗细
            uint16_t radius;    //圆弧半径
            uint8_t round_start; //圆头
            uint8_t round_end;   //圆尾

            uint32_t argb8888;    //透明度以及颜色
        } arc;
```
4. 直线（DRV_EPIC_DRAW_LINE）
```c
        struct
        {
            EPIC_PointTypeDef p1; //起始点坐标
            EPIC_PointTypeDef p2; //结束点坐标

            uint16_t width;    //线宽
            int32_t dash_width; //开发中，尚不支持 
            int32_t dash_gap;   //开发中，尚不支持 
            uint32_t argb8888;  //透明度以及颜色
            uint8_t round_start; //圆头
            uint8_t round_end;   //圆尾
            uint8_t raw_end;    //是否无限长
        } line;
```
5. 边框（DRV_EPIC_DRAW_BORDER）
```c
        struct
        {
            EPIC_AreaTypeDef area; //边框所在区域

            uint16_t radius;     //圆角半径
            uint16_t width;       //线宽

            uint8_t top_side : 1;   //显示边框的上边部分
            uint8_t bot_side : 1;   //显示边框的下边部分
            uint8_t left_side : 1;  //显示边框的左边部分
            uint8_t right_side : 1; //显示边框的右边部分
            uint8_t reserved : 4;

            uint32_t argb8888;      //透明度以及颜色
        } border;
```
6. 多边形（DRV_EPIC_DRAW_POLYGON）
```c
        struct
        {
            EPIC_PointTypeDef points[DRV_EPIC_POLYGON_POINT_MAX];  //多边形的顶点坐标
            uint16_t point_cnt;   //用到的顶点数
            uint32_t argb8888;   //透明度以及颜色
        } polygon;
```
7. bitmap文字（DRV_EPIC_DRAW_LETTERS）
```c
        struct
        {
            uint32_t color_mode;  //bitmap格式
            uint8_t r;  //文字颜色
            uint8_t g;
            uint8_t b;
            uint8_t opa; //文字透明度

            uint32_t letter_num; //文字数量
            drv_epic_letter_type_t *p_letters; //每个字的坐标、数据地址
        } label;
```

## 公共函数
1. 图形渲染驱动层的打开和关闭函数
```
void drv_gpu_open(void);
void drv_gpu_close(void);
```


2. 单步模式下检查上一步是否完成
```
rt_err_t drv_gpu_check_done(rt_int32_t ms);
```

3. 非cache内存段函数

**该函数由上层实现**，用于指定哪些内存是不需要清理Dcache的（即CPU不会进行读写操作，纯硬件操作的buffer），可节省清理Dcache时间。

```
/**
 * @brief Get the specified ram block cache state
 * @param start   start address
 * @param len     ram length
 * @return        0 - if not cached  1 - cached
 */
uint8_t drv_gpu_is_cached_ram(uint32_t start, uint32_t len);
```

4. 立即返回渲染驱动是否忙闲状态
```
bool drv_epic_is_busy(void);
```