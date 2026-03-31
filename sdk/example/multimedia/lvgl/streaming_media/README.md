# streaming_media example

源码路径：example/multimedia/lvgl/streaming_media

本示例演示如何通过蓝牙 PAN 协议连接手机热点后，自动从网络读取并播放在线视频。视频源参考 `main.c` 中的 `VIDEO_SRC` 数组。在网络未连接前，屏幕显示黑色。

基于此示例，可实现如智能终端联网视频播放、蓝牙热点流媒体展示等应用场景。

## 用法

详细的 SiFli-SDK 配置和编译方法请参考 [SiFli-SDK 快速入门](https://docs.sifli.com/projects/sdk/latest/sf32lb52x/quickstart/index.html)。

### 支持的开发板

- sf32lb52-lchspi-ulp

### 硬件需求

- 一块支持的开发板（如上所列）
- 一部手机（用于蓝牙 PAN 网络共享）

### 软件需求

- SiFli-SDK
- scons 构建工具
- 串口下载工具（如 uart_download.bat）

### 连接与操作步骤

1. **手机开启蓝牙网络共享**  
   - iOS：需插入 SIM 卡，打开“个人热点”即可。  
     ![IOS_ENB](./assets/ios_enable_pan.png)
   - Android：在“个人热点”设置中，开启“蓝牙网络共享”。  
     ![ANDRIOD_ENB](./assets/android_enable_pan.png)

2. **开发板上电后**  
   - 蓝牙自动进入可被发现和配对状态，手机可搜索到设备名为 `sifli_streaming` 的蓝牙设备并连接。

3. **连接成功后**  
   - 串口会打印 `pan connect successed`，屏幕底部显示当前读取的文件名。
   - 网络连接后，自动读取并播放 `VIDEO_SRC` 中的视频流。  
     ![PLAY_VIDEO](./assets/play_online_streaming_video.png)

4. **如需重新连接 PAN**  
   - 可通过 finsh 命令 `pan_cmd conn_pan` 重新连接。

## 配置项目

请在 menuconfig 中启用以下选项：

1. **使能蓝牙**
   - 路径：Sifli middleware → Bluetooth
   - 宏开关：`CONFIG_BLUETOOTH`

2. **使能 PAN & A2DP**
   - 路径：Sifli middleware → Bluetooth → Bluetooth service → Classic BT service
   - 宏开关：`CONFIG_CFG_PAN`、`CONFIG_BT_PROFILE_CUSTOMIZE`
   - 可选：`CONFIG_BT_FINSH`（使能 finsh 命令行）

3. **使能 BT connection manager**
   - 路径：Sifli middleware → Bluetooth → Bluetooth service → Classic BT service
   - 宏开关：`CONFIG_BSP_BT_CONNECTION_MANAGER`

4. **使能 NVDS**
   - 路径：Sifli middleware → Bluetooth → Bluetooth service → Common service
   - 宏开关：`CONFIG_BSP_BLE_NVDS_SYNC`

5. **蓝牙自动连接**
   - 路径：Sifli middleware → Bluetooth → Bluetooth service → Classic BT service
   - 宏开关：`CONFIG_BT_AUTO_CONNECT_LAST_DEVICE`
   - 路径：Third party packages
   - 宏开关：`CONFIG_PKG_USING_FLASHDB`

6. **使能屏幕**
   - 路径：Config LCD on board
   - 宏开关：`CONFIG_BSP_USING_LCD`
   - 路径：Config LCD on board → Built-in LCD module driver
   - 选择：1.85 rect QSPI Single-Screen LCD

7. **使能 lvgl 和 FFMPEG 库以及MBedTLS依赖**
   - 路径：Third party packages
   - 宏开关：`CONFIG_PKG_USING_LITTLEVGL2RTT`、`CONFIG_PKG_USING_FFMPEG`、`CONFIG_PKG_USING_MBEDTLS`

## 编译和烧录

切换到项目目录，编译：

```bash
scons --board=sf32lb52-lchspi-ulp -j16
```

烧录：

```bash
cd project/build_xx
./uart_download.bat
# 按提示选择串口号
```

详细步骤请参考 [快速入门](/quickstart/get-started.md)。

## 示例输出

蓝牙连接后，串口输出如下：

```
pan connect successed
```

网络连接后，屏幕底部显示当前读取的文件名，视频正常播放。网络未连接前，屏幕为黑色。

## 异常诊断

- 若蓝牙无法连接，请检查手机蓝牙网络共享是否开启。
- 若视频无法播放，请确认网络已连接，且视频源地址可访问。
- 如需重新连接 PAN，可通过 finsh 命令 `pan_cmd conn_pan` 操作。

如有技术疑问，请在 [GitHub issue](https://github.com/OpenSiFli/SiFli-SDK/issues) 提出。

## 参考文档


## 更新记录

| 版本   | 日期     | 发布说明   |
|:------|:--------|:----------|
| 0.0.1 | 08/2025 | 初始版本   |