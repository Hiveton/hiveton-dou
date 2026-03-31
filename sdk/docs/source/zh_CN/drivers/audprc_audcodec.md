# Audprc/Audcodec Audio设备
芯片内部有Audprc和Audcodec，Audprc用于数字处理，Audcodec是数字转模拟。
常用的音频数据流程是Audprc-->Audcodec-->Speaker
还有的方式是Audprc-->I2S-->芯片外围Codec芯片<br>
主要功能包括： <br>
- 输入和输出采样率设置
- 输入和输出通道数设置
- 每个采样的位深设置
- 音量大小的设置

播放的代码说明，具体例子在drv_audprc.c文件后面和example/rt_device/audprc里都有

```c
static rt_event_t g_tx_ev;

static rt_err_t speaker_tx_done(rt_device_t dev, void *buffer)
{
    //此函数在发送一帧完成的dma中断里，表示发送一次完成
    rt_event_send(g_tx_ev, 1);
    return RT_EOK;
}
#define DMA_BUF_SIZE    1600

void test_demo()
{
    g_tx_ev = rt_event_create("audio_tx_evt", RT_IPC_FLAG_FIFO);
    //1. 打开设备
    int err;
    rt_device_t audprc_dev;
    rt_device_t audcodec_dev;
    audprc_dev = rt_device_find("audprc");
    audcodec_dev = rt_device_find("audcodec");
    RT_ASSERT(audprc_dev && audcodec_dev);

    err = rt_device_open(audprc_dev, RT_DEVICE_FLAG_RDWR);
    RT_ASSERT(RT_EOK == err);
    err = rt_device_open(audcodec_dev, RT_DEVICE_FLAG_WRONLY);
    RT_ASSERT(RT_EOK == err);

    //2. 设置发送完成的回到函数
    rt_device_set_tx_complete(audprc_dev, speaker_tx_done);
    
    //3. 设置一次DMA buffer的大小，底层驱动里会使用2个这样的DMA buffer做ping/pong buffer
    rt_device_control(audprc_dev, AUDIO_CTL_SET_TX_DMA_SIZE, (void *)DMA_BUF_SIZE);

    //4. 音频输出到CODEC, 如果到I2S，可以设置AUDPRC_TX_TO_I2S
    rt_device_control(audcodec_dev, AUDIO_CTL_SETOUTPUT, (void *)AUDPRC_TX_TO_CODEC);
    rt_device_control(audprc_dev,   AUDIO_CTL_SETOUTPUT, (void *)AUDPRC_TX_TO_CODEC);
    
    //5. 设置codec参数
    struct rt_audio_caps caps;

    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = (1 << HAL_AUDCODEC_DAC_CH0);
#if BSP_ENABLE_DAC2
    caps.sub_type |= (1 << HAL_AUDCODEC_DAC_CH1);
#endif
    caps.udata.config.channels   = 1; //最后的输出为一个声道
    caps.udata.config.samplerate = 16000; //采样率, 8000/11025/12000/16000/22050/24000/32000/44100/48000
    caps.udata.config.samplefmt = 16; //位深度8 16 24 or 32
    rt_device_control(audcodec_dev, AUDIO_CTL_CONFIGURE, &caps);
    
    struct rt_audio_sr_convert cfg;
    cfg.channel = 2; //源数据的通道个数，如果是2，则数据传入的格式位LRLRLR....的interleave格式
    cfg.source_sr = 16000; //源数据的采样率
    cfg.dest_sr = 16000;   //播放时的采样率  
    rt_device_control(audprc_dev, AUDIO_CTL_OUTPUTSRC, (void *)(&cfg));

    //数据选择，一路源数据就这样配置就行了，多路源数据的处理参考《音频通路mix&mux功能说明.docx》
    caps.main_type = AUDIO_TYPE_SELECTOR;
    caps.sub_type = 0xFF;
    caps.udata.value = 0x5050;
    rt_device_control(audprc_dev, AUDIO_CTL_CONFIGURE, &caps);
    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type = 0xFF;
    caps.udata.value   = 0x5050;
    rt_device_control(audprc_dev, AUDIO_CTL_CONFIGURE, &caps);

    //源数据格式说明
    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = HAL_AUDPRC_TX_CH0;
    caps.udata.config.channels   = 2;
    caps.udata.config.samplerate = 16000;
    caps.udata.config.samplefmt = 16;
    rt_device_control(audprc_dev, AUDIO_CTL_CONFIGURE, &caps);

    //从EQ配置表中获得音量并设置到codec，vol_level为0 ~ 15
    uint8_t vol_level = 15;
    int volumex2 = eq_get_music_volumex2(vol_level);
    if (caps.udata.config.samplerate == 16000 || caps.udata.config.samplerate == 8000)
       volumex2 = eq_get_tel_volumex2(vol_level);
    rt_device_control(audcodec_dev, AUDIO_CTL_SETVOLUME, (void *)volumex2);

    //开始播放
    int stream = AUDIO_STREAM_REPLAY | ((1 << HAL_AUDPRC_TX_CH0) << 8);
    rt_device_control(audprc_dev, AUDIO_CTL_START, (void *)&stream);
    stream = AUDIO_STREAM_REPLAY | ((1 << HAL_AUDCODEC_DAC_CH0) << 8);
    rt_device_control(audcodec_dev, AUDIO_CTL_START, &stream);
    rt_uint32_t evt;
    int16_t tx_pipe_data[4096];
    while (1)
    {
        rt_event_recv(g_tx_ev, 1, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &evt);
#if 1
        rt_device_write(audprc_dev, 0, tx_pipe_data, DMA_BUF_SIZE);
#else
        //为了更快，也可以用
        bf0_audprc_device_write(audprc_dev, 0, tx_pipe_data, DMA_BUF_SIZE);
#endif
    }
    rt_device_close(audcodec_dev);
    rt_device_close(audprc_dev);
    rt_event_delete(g_tx_ev);
}
```
接收的代码说明

```c
static rt_event_t g_rx_ev;
static rt_err_t mic_rx_ind(rt_device_t dev, rt_size_t size)
{
    //in inturrupt
    rt_event_send(g_rx_ev, 1);
    return RT_EOK;
}
void test_demo()
{
    g_rx_ev = rt_event_create("audio_evt", RT_IPC_FLAG_FIFO);
    //1. 打开设备
    int err;
    rt_device_t audprc_dev;
    rt_device_t audcodec_dev;
    audprc_dev = rt_device_find("audprc");
    audcodec_dev = rt_device_find("audcodec");
    RT_ASSERT(audprc_dev && audcodec_dev);

    err = rt_device_open(audprc_dev, RT_DEVICE_FLAG_RDWR);
    RT_ASSERT(RT_EOK == err);
    err = rt_device_open(audcodec_dev, RT_DEVICE_FLAG_WRONLY);
    RT_ASSERT(RT_EOK == err);
    
    // 设置接受一帧完成的回到函数
    rt_device_set_rx_indicate(audprc_dev, mic_rx_ind);

    //config ADC
    struct rt_audio_caps caps;
    int stream;

    //2. 设置输入源自audcodec-->audprc，如果是I2S-->audprc, 这设置为AUDPRC_RX_FROM_I2S
    rt_device_control(audcodec_dev, AUDIO_CTL_SETINPUT, (void *)AUDPRC_RX_FROM_CODEC);
    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type = 1 << HAL_AUDCODEC_ADC_CH0;
    caps.udata.config.channels   = 1; //一个声道
    caps.udata.config.samplerate = 16000; //采样率
    caps.udata.config.samplefmt = 16; // 位深 8 16 24 or 32
    rt_device_control(audcodec_dev, AUDIO_CTL_CONFIGURE, &caps);
    rt_device_control(audprc_dev, AUDIO_CTL_SETINPUT, (void *)AUDPRC_RX_FROM_CODEC);

    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type = HAL_AUDPRC_RX_CH0 - HAL_AUDPRC_RX_CH0; //来自RX0
    caps.udata.config.channels   = 1;
    caps.udata.config.samplerate = 16000;
    caps.udata.config.samplefmt = 16;
    rt_device_control(audprc_dev, AUDIO_CTL_CONFIGURE, &caps);

    //3. EQ相关的在drv_audprc.c前面那段代码，那个可以用EQ工具生成的，里面g_adc_volume为mic的增益
    int stream = AUDIO_STREAM_RECORD | ((1 << HAL_AUDCODEC_ADC_CH0) << 8);
    rt_device_control(audcodec_dev, AUDIO_CTL_START, &stream);
    stream = AUDIO_STREAM_RECORD | ((1 << HAL_AUDPRC_RX_CH0) << 8);
    rt_device_control(audprc_dev, AUDIO_CTL_START, &stream);
    rt_uint32_t evt;
    
    uint8_t g_pipe_data[CFG_AUDIO_RECORD_PIPE_SIZE];
    while (1)
    {
        rt_event_recv(g_rx_ev, 1, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &evt);
        rt_device_read(audprc_dev, 0, g_pipe_data, CFG_AUDIO_RECORD_PIPE_SIZE);
    }
    rt_device_close(audcodec_dev);
    rt_device_close(audprc_dev);
    rt_event_delete(g_rx_ev);
}
```
## 驱动配置
在{menuselection}打开
`On-Chip Peripheral RTOS Drivers --> Enable Audio codec Driver`
`On-Chip Peripheral RTOS Drivers --> Enable Audio Process driver --> Enable AUDPRC TX Channel 0 DMA`
`On-Chip Peripheral RTOS Drivers --> Enable Audio Process driver --> Enable AUDPRC RX Channel 0 DMA`

[audio]: https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/audio/audio
## RT-Thread参考文档

- [AUDIO设备][audio]

