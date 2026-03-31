# OTA 下载与安装示例
源码路径：example\dfu_pan

## 支持的平台
例程可以运行在以下开发板。
* sf32lb52-lcd_n16r8


## 示例概述
* 实现通过蓝牙PAN连接进行OTA固件下载和安装功能
* 提供基于命令行的OTA版本管理与下载接口
* 支持蓝牙连接管理、固件包解析、Flash写入和CRC校验等完整OTA流程

## 例程的使用

### 功能说明
该示例程序实现了完整的OTA下载和安装功能：
1. 初始化蓝牙协议栈并建立PAN连接
2. 通过UI线程显示OTA状态信息
3. 提供命令行接口用于版本查询、下载和安装
4. 支持固件包解析、Flash写入和校验

### 编译和烧录
#### 以sf32lb52-lcd工程代码为例编译
切换到例程project目录，运行scons命令执行编译：

> scons --board=sf32lb52-lcd_n16r8 -j8

切换到例程`project/build_xx`目录，运行[uart_download.bat]，按提示选择端口即可进行下载：

> build_sf32lb52-lcd_n16r8_hcpu\uart_download.bat

> Uart Download

> please input the serial port num:5

### 主要功能模块

#### 蓝牙连接管理
程序初始化蓝牙协议栈并监听连接事件，支持自动建立PAN连接用于网络传输。

#### OTA命令接口
提供以下MSH命令用于OTA操作：
* `dfu_pan_print_files_cmd` - 显示可用OTA版本信息
* `dfu_pan_down_files_cmd <index>` - 下载指定索引的OTA版本
* `dfu_pan_finish_cmd` - OTA完成验证命令
* `ota_cmd_start del_bond` - 删除蓝牙配对信息
* `ota_cmd_start conn_pan` - 连接PAN配置文件

#### 下载与安装流程
1. 使用`dfu_pan_print_files_cmd`查看可用版本
2. 选择目标版本索引，使用`dfu_pan_down_files_cmd <index>`开始下载
3. 程序自动解析固件包、写入Flash并进行CRC校验
4. 下载完成后自动清除更新标志位

### 例程输出结果展示:
* 程序启动日志:
```
SFBL
...
I/ota_install bts: bt_app_interface_event_handle_type: 0
I/btapp_ge bts: << Scan mode: 0
I/btapp_ge bts: <<            No scans enbd.
```

* 蓝牙连接成功:
```
BT_NOTIFY_COMMON_BT_STACK_READY
BT/BLE stack and profile ready
------Set_local_name------: sifli-pan
BT_NOTIFY_COMMON_ACL_CONNECTED
Encryption competed
bd addr 0xXX:XX:XX:XX:XX:XX
pan connect successed
OTA_BT_APP_CONNECT_PAN_SUCCESS
------start_ota------
```

* 版本查询输出:
```
msh />dfu_pan_print_files_cmd
--------OTA version_list Files Status:--------
==========================
OTA version_list Files Address: 0xXXXXXXXX
File 0:
Filename: v1.2.0
URL: http://ota.example.com/firmware/v1.2.0.zip
Needs Update: 1
------------------------
```

* 下载过程输出:
```
msh />dfu_pan_down_files_cmd 0
Downloading version: v1.2.0
Starting to download and parse OTA package from:
http://ota.example.com/firmware/v1.2.0.zip
Package header - CRC: 0xXXXXXXXX, Magic: 0x5F4F5441, Version: 0xXXXXXXXX, Bin Count: 2
Bin file 0:
  Name: app.bin
  ID: 1
  GZIP: 1
  Compressed Length: 123456
  Original Length: 234567
  Address: 0x00080000
  Data CRC: 0xXXXXXXXX
  Original CRC: 0xXXXXXXXX
  Region Size: 0x00040000
Writing app.bin: 50% (61728/123456 bytes)
Writing app.bin: 100% (123456/123456 bytes)
CRC verification passed for app.bin
dfu_pan_parse_package_from_url finished with result: 0
```

## 例程说明

### 线程与资源管理
程序创建了以下主要线程和资源：
1. OTA UI线程 - 处理OTA状态显示
2. 蓝牙事件处理线程 - 处理蓝牙连接和PAN配置
3. 邮箱机制 - 用于线程间通信
4. 定时器 - 用于PAN连接延迟处理

### 核心数据结构
* `bt_app_t_ota` - 蓝牙应用环境结构体
* `version_info` - 版本信息结构体
* `firmware_info` - 固件信息结构体

### Flash存储布局
OTA相关信息存储在Flash的特定区域：
* 固件信息存储地址: `OTA_INFO_BASE_ADDR`
* 版本信息存储地址: `VERSION_INFO_BASE_ADDR`

## API参考
* `dfu_pan_parse_package_from_url()` - 解析并下载OTA包
* `dfu_pan_get_version_info()`- 获取版本信息
* `dfu_pan_print_files()` - 打印版本文件状态
* `bt_interface_register_bt_event_notify_callback()` - 注册蓝牙事件回调

## 注意事项
1. 需要预先通过其他方式获取版本信息并存储在Flash中
2. 确保设备有足够的Flash空间存储新固件
3. 下载过程中应保持稳定的网络连接
4. 下载完成后设备将提示点击重启进入新固件

## 更新记录
|版本  |日期    |发布说明 |
|:---  |:---    |:---    |
|1.0.0 |11/2025 |初始版本 |
|      |        |        |
