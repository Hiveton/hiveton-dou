# zbt mesh示例

源码路径：example/zbt/mesh

## 支持的平台
<!-- 支持哪些平台 -->
全平台

## 概述
本例程演示了蓝牙 Mesh 功能。它包含多个标准的 Mesh 模型，并支持通过广播和 GATT 配网承载（即 PB-ADV 和 PB-GATT）进行配网。

该应用程序还需要一个功能正常的串行控制台，因为这是用于带外配网过程和模拟按键所必需的。

在带有 LED 的开发板上，通用 OnOff 服务器模型提供了通过网络控制板上第一个 LED 的功能。

在带有按钮的开发板上，通用 OnOff 客户端模型将在按钮按下时，向网络中的所有节点发送 OnOff 消息。

## 例程的使用
1. 此示例可以通过外部配网器设备被配网到现有的 Mesh 网络中，也可以通过发送mesh_btn命令模拟按钮进行自配网。
2. 当使用配网器设备进行配网时，配网器必须为设备提供一个应用密钥，并将其绑定到两个通用 OnOff 模型。
3. 当进行自配网时，设备将获取一个随机的单播地址，并将一个虚拟的应用密钥绑定到这些模型。
4. 一旦完成配网，发送给通用 OnOff 服务器的消息将用于打开或关闭 LED，而模拟按钮将用于向同一网络中的所有节点广播 OnOff 消息。

### 硬件需求
运行该例程前，需要准备：
+ 一块本例程支持的开发板([支持的平台](#Platform_peri))。
+ 安装有蓝牙mesh APP的手机设备。

### menuconfig配置
1. 使能蓝牙(`BLUETOOTH`)：
    - 路径：Sifli middleware → Bluetooth
    - 开启：Enable bluetooth
        - 宏开关：`CONFIG_BLUETOOTH`
        - 作用：使能蓝牙功能
2. 使能GAP, GATT Client, BLE connection manager：
    - 路径：Sifli middleware → Bluetooth → Bluetooth service → BLE service
    - 开启：Enable BLE GAP central role
        - 宏开关：`CONFIG_BLE_GAP_CENTRAL`
        - 作用：作为BLE CENTRAL（中心设备）的开关，打开后，提供扫描和主动发起与外设（Peripheral）的连接功能。
    - 开启：Enable BLE GATT client
        - 宏开关：`CONFIG_BLE_GATT_CLIENT`
        - 作用：GATT CLIENT的开关，打开后，可以主动搜索发现服务，读/写数据，接收通知。
    - 开启：Enable BLE connection manager
        - 宏开关：`CONFIG_BSP_BLE_CONNECTION_MANAGER`
        - 作用：提供BLE连接控制管理，包括多连接管理，BLE配对，链路连接参数更新等内容。
3. 使能NVDS：
    - 路径：Sifli middleware → Bluetooth → Bluetooth service → Common service
    - 开启：Enable NVDS synchronous
        - 宏开关：`CONFIG_BSP_BLE_NVDS_SYNC`
        - 作用：蓝牙NVDS同步。当蓝牙被配置到HCPU时，BLE NVDS可以同步访问，打开该选项；蓝牙被配置到LCPU时，需要关闭该选项。

### 编译和烧录
切换到例程project目录，运行scons命令执行编译：
```c
> scons --board=eh-lb525 -j8
```
切换到例程`project/build_xx`目录，运行`uart_download.bat`，按提示选择端口即可进行下载：
```c
$ ./uart_download.bat

     Uart Download

please input the serial port num:5
```
关于编译、下载的详细步骤，请参考[快速入门](/quickstart/get-started.md)的相关介绍。

## 例程的预期结果
<!-- 说明例程运行结果，比如哪几个灯会亮，会打印哪些log，以便用户判断例程是否正常运行，运行结果可以结合代码分步骤说明 -->
例程启动后：
1. 可以被手机通过BLE MESH APP搜到并进行配网，完成配网，发送给通用 OnOff 服务器的消息将用于LED的控制。
2. 自配网的设备通过模拟按钮可向同一网络中的所有节点广播 OnOff 消息。

## 异常诊断


## 参考文档
<!-- 对于rt_device的示例，rt-thread官网文档提供的较详细说明，可以在这里添加网页链接，例如，参考RT-Thread的[RTC文档](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/rtc/rtc) -->

## 更新记录
| 版本  | 日期    | 发布说明 |
| :---- | :------ | :------- |
| 0.0.1 | 06/2025 | 初始版本 |
|       |         |          |
|       |         |          |