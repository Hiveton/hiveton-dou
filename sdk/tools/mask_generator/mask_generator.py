import sys
from PIL import Image
import numpy as np

def generate_alpha_mask(image_path, format_type, output_file=None):
    """
    从图片中提取Alpha通道并生成指定格式的mask数据
    对齐与打包规则：
    - A8格式：无需对齐，每个像素1字节
    - A4格式：2像素对齐，每2像素打包为1字节
    - A2格式：4像素对齐，每4像素打包为1字节
    像素顺序：ABCDEFGH（从左到右显示）
    """
    # 验证格式类型
    if format_type not in ['A2', 'A4', 'A8']:
        print(f"错误: 不支持的格式 '{format_type}'，请使用'A2', 'A4'或'A8'")
        return None
    
    # 打开图片并转换为RGBA模式以获取Alpha通道
    try:
        img = Image.open(image_path).convert('RGBA')
        print(f"成功打开图片: {image_path}，尺寸: {img.size[0]}x{img.size[1]}")
    except Exception as e:
        print(f"无法打开图片: {e}")
        return None
    
    width, height = img.size
    pixels = np.array(img)
    
    # 提取Alpha通道 (0-255)
    alpha_channel = pixels[:, :, 3]  # 所有像素的Alpha值
    
    # 调试信息：检查Alpha通道值范围
    min_alpha = alpha_channel.min()
    max_alpha = alpha_channel.max()
    print(f"Alpha通道值范围: 最小={min_alpha}, 最大={max_alpha}")
    
    if min_alpha == 255 and max_alpha == 255:
        print("警告: 检测到Alpha通道所有值均为255（完全不透明）")
    else:
        # 随机采样10个像素的Alpha值进行验证
        sample_indices = np.random.choice(width * height, min(10, width * height), replace=False)
        sample_values = alpha_channel.flatten()[sample_indices]
        print(f"随机采样的Alpha值: {sample_values}")
    
    # 根据格式类型处理Alpha值
    mask_rows = []  # 按行存储处理后的数据
    bits = int(format_type[1:])  # 提取数字部分: 2, 4, 8
    max_value = (1 << bits) - 1  # 该格式下的最大值: 3(A2), 15(A4), 255(A8)
    
    # 转换每个Alpha值到目标格式
    for y in range(height):
        # 获取当前行的Alpha值（从左到右对应像素A,B,C,D,E,F,G,H...）
        row_alpha = [int((alpha / 255.0) * max_value) for alpha in alpha_channel[y, :]]
        
        # 根据位深和对齐规则处理数据
        packed_row = []
        if bits == 8:
            # A8格式: 无需对齐，每个像素1字节
            packed_row = row_alpha.copy()
        
        elif bits == 4:
            # A4格式: 2像素对齐，每2像素打包为1字节
            align_count = 2
            pad_count = (align_count - (len(row_alpha) % align_count)) % align_count
            row_alpha += [0] * pad_count  # 补充0像素
            
            # 每2像素打包：(A<<4 | B), (C<<4 | D)...
            for i in range(0, len(row_alpha), 2):
                A, B = row_alpha[i], row_alpha[i+1]
                packed_byte = (A << 4) | B
                packed_row.append(packed_byte)
        
        elif bits == 2:
            # A2格式: 4像素对齐，每4像素打包为1字节（核心调整）
            align_count = 4
            pad_count = (align_count - (len(row_alpha) % align_count)) % align_count
            row_alpha += [0] * pad_count  # 补充0像素至4的倍数
            
            # 每4像素打包为1字节：D1D0 C1C0 B1B0 A1A0
            # 像素顺序A(0), B(1), C(2), D(3)
            for i in range(0, len(row_alpha), 4):
                A, B, C, D = row_alpha[i], row_alpha[i+1], row_alpha[i+2], row_alpha[i+3]
                # 位分布：bit7~6=D1D0, bit5~4=C1C0, bit3~2=B1B0, bit1~0=A1A0
                packed_byte = (D << 6) | (C << 4) | (B << 2) | A
                packed_row.append(packed_byte)
        
        mask_rows.append(packed_row)
    
    # 计算每行字节数
    if bits == 8:
        bytes_per_row = width  # 无对齐
    elif bits == 4:
        bytes_per_row = (width + 1) // 2  # 2像素1字节，向上取整
    else:  # A2
        bytes_per_row = (width + 3) // 4  # 4像素1字节，向上取整（因已4像素对齐）
    
    # 生成C语言数组（按实际像素宽度排列）
    c_array = f"// {format_type}格式mask数据 ({width}x{height})\n"
    c_array += f"// 格式说明: {bits}位Alpha值，范围0~{max_value}，每行{bytes_per_row}字节\n"
    c_array += f"// 对齐规则: A8无对齐, A4按2像素对齐, A2按4像素对齐\n"
    c_array += f"// A2打包方式: 每4像素1字节 (D1D0C1C0B1B0A1A0)\n"
    c_array += f"const uint8_t {format_type.lower()}_mask_data[] = {{\n"
    
    # 按实际像素行输出
    for row in mask_rows:
        c_array += "    " + ", ".join(f"0x{val:02X}" for val in row) + ",\n"
    
    # 移除最后一行的逗号
    c_array = c_array.rstrip(",\n") + "\n};\n"
    c_array += f"const uint32_t {format_type.lower()}_mask_width = {width};\n"
    c_array += f"const uint32_t {format_type.lower()}_mask_height = {height};\n"
    c_array += f"const uint8_t {format_type.lower()}_mask_bits = {bits};\n"
    c_array += f"const uint32_t {format_type.lower()}_mask_bytes_per_row = {bytes_per_row};\n"
    
    # 输出结果
    if output_file:
        with open(output_file, 'w') as f:
            f.write(c_array)
        print(f"{format_type}格式mask数据已保存到 {output_file}")
    else:
        # 仅打印前3行和后3行数据
        print("\n生成的mask数据片段：")
        lines = c_array.split('\n')
        for line in lines[:5] + ['...'] + lines[-5:]:
            print(line)
    
    return mask_rows

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("用法: python generate_alpha_mask_a2_adjusted.py <图片路径> <格式(A2/A4/A8)> [输出文件路径]")
        print("示例:")
        print("  python generate_alpha_mask_a2_adjusted.py image.png A2 mask.h")
        print("  python generate_alpha_mask_a2_adjusted.py icon.png A4")
        sys.exit(1)
    
    input_path = sys.argv[1]
    format_type = sys.argv[2].upper()
    output_path = sys.argv[3] if len(sys.argv) > 3 else None
    
    generate_alpha_mask(input_path, format_type, output_path)
    