# Mask Generator 使用说明文档

## 概述

Mask Generator 是一个 Python 脚本工具，用于从 PNG 图片中提取 Alpha 通道并生成适用于 SiFli GPU 的 A2、A4、A8 格式 mask 数据。该工具支持不同的像素对齐和打包规则，生成的 C 语言数组可直接用于嵌入式项目中。

## 功能特性

- 支持 A2、A4、A8 三种 Alpha 格式
- 自动处理像素对齐规则
- 生成符合硬件要求的打包数据
- 输出 C 语言格式的数组代码
- 支持文件保存和控制台输出


### 使用方法
python mask_generator.py <图片路径> <格式> [输出文件路径]

| 参数                        | 说明                       |必需性|
|-----------------------------|------------------------------|---|
| 图片路径          | 输入的 PNG 图片文件路径        |必需
| 格式        | Alpha 格式类型 (A2/A4/A8)           |必需
|输出文件路径|生成的 C 代码保存路径 |可选

### 使用示例
* 先cd到脚本目录下，安装依赖：
```bash
pip install Pillow numpy
```
* 生成 A2 格式 mask 并保存到文件：
```bash
python mask_generator.py image.png A2 mask_a2.h
```
### 输出格式
* 生成的 C 代码包含以下内容：
```bash
const uint8_t a2_mask_data[] = {
    // 数据内容
};
const uint32_t a2_mask_width = 宽度;
const uint32_t a2_mask_height = 高度;
const uint8_t a2_mask_bits = 2;
const uint32_t a2_mask_bytes_per_row = 每行字节数;
```
### 注意事项
输入图片要求：必须是包含 Alpha 通道的 PNG 图片

