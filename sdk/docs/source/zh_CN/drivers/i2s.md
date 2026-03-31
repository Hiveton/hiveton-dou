# I2S Audio设备

- 输入和输出采样率设置
- 输入和输出通道数设置
- 每个采样的位深设置
- 主从模式设置

音频驱动程序包括两层：用于 I2S 的硬件访问层 (HAL) 和用于 RT-Thread 的适配层。<br>
硬件访问层提供用于访问 I2S 外设寄存器的基本 API。 有关详细信息，请参阅 I2S 的 API 文档。<br>
适配层提供对 RT-Thread 驱动框架的支持。 用户可以使用 RT-Thread POSIX 驱动程序接口进行音频编程。 请参阅 RT-Thread 驱动程序的 API 文档。<br>
主要功能包括： <br>
- 麦克风设备和扬声器设备支持
- 用于音频捕获和播放的 DMA
- 音频捕获转储工具支持并保存在 PC 中
- 有的芯片有多路I2S硬件，58x芯片其中I2S1只用来输入，具体需要看对应芯片的手册。 

## 驱动配置

在{menuselection}`On-Chip Peripheral RTOS Drivers --> Enable I2S Audio Driver`菜单中选择要使用的I2S设备

下面宏开关表示使能了I2S_MIC和I2S_CODEC两个设备
```c
#define BSP_USING_DMA 1
#define RT_USING_AUDIO 1
#define BSP_USING_I2S 1
#define BSP_ENABLE_I2S_MIC 1
#define BSP_ENABLE_I2S_CODEC 1
```

## 设备名称
- `i2s<x>`，
其中x为设备编号，如`i2s1`、`i2s2`，与操作的外设编号对应
见audio_config.h里的定义，可以看到设备名为i2s2, 实际对应的硬件为i2s1(hwp_i2s1),
对只有1个i2s的芯片，或实际使用的是I2s1, 注意这里hwp_i2s2要改为hwp_i2s1
如果配置BF0_MIC_CONFIG， 使用的i2s作为mic使用，用的drv_i2s_mic.c
如果使用BSP_ENABLE_I2S_CODEC，用的drv_i2s_audio.c一般播放数据用。
注意 rt_device_find("i2s1")或rt_device_find("i2s2")要与下面的.name对应。
具体要看看配置用的下面哪一个。

struct i2s_audio_cfg_t bf0_i2s_audio_obj[] = {
  ...
}

```c
#ifdef BSP_USING_I2S
#ifdef BSP_ENABLE_I2S_MIC
#ifndef BF0_MIC_CONFIG
#define BF0_MIC_CONFIG \
    {                                           \
       .name                    = "i2s1",       \
       .dma_handle              = MIC_DMA_INSTANCE, \
       .dma_request             = I2S1_RX_DMA_REQUEST,  \
       .is_record               = 1,            \
       .i2s_handle              = hwp_i2s1,      \
       .reqdma_tx               = I2S1_TX_DMA_REQUEST, \
       .hdma_tx                 = MIC_TX_DMA_INSTANCE, \
    }
#endif /* BF0_MIC_CONFIG */
#endif /* BSP_ENABLE_I2S_MIC */

#ifdef BSP_ENABLE_I2S_CODEC
#ifndef BF0_I2S2_CONFIG
#define BF0_I2S2_CONFIG \
    {                                           \
       .name                    = "i2s2",       \
       .dma_handle              = I2S_RX_DMA_INSTANCE, \
       .dma_request             = I2S2_RX_DMA_REQUEST,  \
       .is_record               = 1,            \
       .i2s_handle              = hwp_i2s2,      \
       .reqdma_tx               = I2S2_TX_DMA_REQUEST, \
       .hdma_tx                 = I2S_TX_DMA_INSTANCE, \
    }
#endif /* BF0_I2S2_CONFIG */
#endif /* BSP_ENABLE_I2S_CODEC */

```
## 使用流程
### 1. 打开I2s设备
```c
    /*name用i2s2还是i2s1， 看前面的audio_config.h里的配置*/
    rt_device_t i2s = rt_device_find("i2s2");
    if (i2s)
    {
        rt_device_open(i2s, RT_DEVICE_FLAG_RDWR);
    }
```
### 2. 配置参数
```c
    struct rt_audio_caps caps;
    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type = AUDIO_DSP_PARAM;

    /* 1 单声道， 2为双声道*/
    caps.udata.config.channels = 2);
    /*1个采样多少位*/
    caps.udata.config.samplefmt = 16;
    /*采用率*/
    caps.udata.config.samplerate = 44100;
    rt_device_control(i2s, AUDIO_CTL_CONFIGURE, &caps);

    caps.main_type = AUDIO_TYPE_INPUT;      // for I2S2, configure RX will configure RX+TX
    caps.sub_type = AUDIO_DSP_MODE;
    /*mode setting, 0---master mode; 1--- slave mode*/
    caps.udata.value = 0;
    rt_device_control(i2s, AUDIO_CTL_CONFIGURE, &caps);
```
I2S默认是直接输出到外设的，还有个高级用法，是I2S输入来自芯片的audprc模块，
这个不常用，目前还没有这样用, 如果需要的话，得下面这样设置，
如果不知道什么是audprc->i2s,忽略下面这段代码，
```c
/*输入直接输出到外部I2s设备，这里不需要*/
rt_uint32_t inter = 1;
rt_device_control(i2s, AUDIO_CTL_SETINPUT, (void *)inter);
```
### 3. 收或发的回调函数设置
```c
static rt_err_t audio_rx_ind(rt_device_t dev, rt_size_t size)
{
    LOG_I("audio_rx_ind %d\n", size);
    /*这里可以通知接收线程去rt_device_read(i2s)获取数据*/
    return RT_EOK;
}
rt_err_t audio_tx_done(rt_device_t dev, void *buffer)
{
    LOG_I("audio_tx_done \n");
    return RT_EOK;
}

    rt_device_set_rx_indicate(i2s, audio_rx_ind);
    rt_device_set_tx_complete(i2s, audio_tx_done);
```
### 4. 开始收或发
```c
    int stream = 0;
    //select one for stream
#if playback_only
    stream = AUDIO_STREAM_REPLAY; /*playback only*/
#else if capture_only
    stream = AUDIO_STREAM_RECORD; /*record only*/
#else if capture_and_playback   
    stream = AUDIO_STREAM_RXandTX; /*record & playback*/
#endif
    rt_device_control(i2s, AUDIO_CTL_START, &stream);
    
    rt_thread_mdelay(xxxx);

    rt_device_close(i2s);
```
### 5. 配置dma ping-pong buffer大小
```c
#define AUDIO_DATA_SIZE 640
ALIGN(4) static uint8_t audio_data[AUDIO_DATA_SIZE];
ALIGN(4) static uint8_t audio_tx_data[AUDIO_DATA_SIZE];
```
AUDIO_DATA_SIZE为DMA ping-pong的大小，一次数据DMA数据大小的自己数为(AUDIO_DATA_SIZE / 2),
读写数据的时候，一次的数据量应该是(AUDIO_DATA_SIZE / 2)


## 使用音频驱动程序

适配器层注册 RT-Thread 请求的硬件支持功能，并使用 I2S HAL 实现这些功能。 I2S HAL 的 API 详见 [I2S](#hal-i2s)

## 对于使用 RT-Thread 麦克风设备进行音频捕获的用户，可以使用以下代码作为示例：

```c

uint8_t g_pipe_data[512];

// Find and open device
rt_device_t g_mic = rt_device_find("i2s1");
rt_err_t err = rt_device_open(g_mic, RT_DEVICE_FLAG_RDONLY);

// Configure Microphone deivce, sample rate 16000
struct rt_audio_caps caps;
caps.main_type = AUDIO_TYPE_INPUT;
caps.sub_type = AUDIO_DSP_SAMPLERATE;
caps.udata.value =16000;
rt_device_control(g_mic, AUDIO_CTL_CONFIGURE, &caps);;

// Start capture
int stream = 1; // record = 1, playback = 0
rt_device_set_rx_indicate(g_mic, audio_rx_ind);
rt_device_control(g_mic, AUDIO_CTL_START, &stream);


...

rt_err_t audio_rx_ind(rt_device_t dev, rt_size_t size)
{
    // Processing audio data. Please note this is in interrupt context.
    // User might need to start a thread to read and process data, call  rt_device_read(g_mic, 0, g_pipe_data, AUDIO_BUF_SIZE);
}

```

## 对于使用 RT-Thread 喇叭/耳机设备进行音频播放的用户，可以使用以下代码作为示例：

```c

uint8_t g_pipe_data[512];

// Find and open device
rt_device_t g_i2s = rt_device_find("i2s2");
rt_err_t err = rt_device_open(g_i2s, RT_DEVICE_FLAG_RDWR);


// Configure speaker deivce, sample rate 16000
struct rt_audio_caps caps;
caps.main_type = AUDIO_TYPE_INPUT;
caps.sub_type = AUDIO_DSP_SAMPLERATE;
caps.udata.value =16000;
rt_device_control(g_i2s, AUDIO_CTL_CONFIGURE, &caps);;

// Start capture
int stream = 0; // record = 1, playback = 0
rt_device_set_tx_complete(g_i2s, audio_tx_done);
rt_device_control(g_i2s, AUDIO_CTL_START, &stream);


...

rt_err_t audio_tx_done(rt_device_t dev, void *buffer)
{
    // Processing audio data. Please note this is in interrupt context.
    // User might need to start a thread to fill data, call  rt_device_write(g_i2s, 0, g_pipe_data, AUDIO_BUF_SIZE)
}

```
## 其他例子代码

 $(sdk_root)\example\rt_device\i2s

## 命令行参考代码
 drv_i2s_audio.c和drv_i2s_mic.c文件后面有命令行，也可以参考


[audio]: https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/audio/audio
## RT-Thread参考文档

- [AUDIO设备][audio]

