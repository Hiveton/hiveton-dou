---
name: 编译下载
description: 在 hiveton-dou 工程执行 sf32lb52-lcd_n16r8 的编译与串口下载。用于需要先进入 sdk 执行 source export.sh，再到 app/project 执行 scons 编译，并在编译成功后进入 build 目录运行 uart_download.sh 选择串口 0 下载的场景。
---

# 编译下载

当用户要求执行 `sf32lb52-lcd_n16r8` 的固件编译和串口下载时，按以下固定流程执行。

## 前置条件

- 工作目录为仓库根目录（包含 `sdk/` 与 `app/project/`）。
- 目标板卡为 `sf32lb52-lcd_n16r8`。

## 执行步骤

1. 进入 `sdk` 导入环境：

```bash
cd sdk
source export.sh
```

2. 进入 `app/project` 执行编译：

```bash
cd ../app/project
scons --board=sf32lb52-lcd_n16r8
```

3. 编译成功后进入构建目录：

```bash
cd build_sf32lb52-lcd_n16r8_hcpu
```

4. 执行下载脚本，提示串口时选择 `0`：

```bash
./uart_download.sh
```

## 失败处理

- `source export.sh` 失败：停止并回报环境导入错误。
- `scons` 非 0 退出：停止，不执行下载，并回报编译错误。
- `uart_download.sh` 未检测到串口：提示检查线缆、驱动和设备权限后重试。
