#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA固件打包工具 (Python版本) - 小端序版本，CRC算法与C代码同步
支持灵活的INI配置文件格式，统一小端序存储，魔数对应_OTA（0x5F4F5441）
"""

import os
import sys
import struct
import zlib
import configparser
import argparse
from typing import List

# CRC32相关定义，与C代码保持一致
CRC32_POLY = 0xEDB88320
crc32_table = []

def init_crc32_table():
    """初始化CRC32表，与C代码保持一致"""
    global crc32_table
    if crc32_table:  # 如果已经初始化过，直接返回
        return
    
    for i in range(256):
        crc = i
        for j in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ CRC32_POLY
            else:
                crc >>= 1
        crc32_table.append(crc)

def calculate_crc32(data: bytes, crc: int = 0xffffffff) -> int:
    """
    计算CRC32值，与C代码保持一致
    """
    init_crc32_table()  # 确保表已初始化
    
    crc = crc ^ 0xFFFFFFFF
    for byte in data:
        crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF]
    return crc ^ 0xFFFFFFFF

# GZIP压缩函数（生成带gzip头的压缩数据，兼容嵌入式解压逻辑）
def gzip_compress(data: bytes) -> bytes:
    """
    使用zlib.compressobj实现gzip格式压缩（支持wbits参数）
    """
    print(f"[DEBUG] GZIP压缩数据，原始长度: {len(data)} 字节")
    # wbits=16+zlib.MAX_WBITS：生成带gzip头和校验尾的压缩数据
    compress_obj = zlib.compressobj(level=6, wbits=16 + zlib.MAX_WBITS)
    compressed_data = compress_obj.compress(data)
    compressed_data += compress_obj.flush()  # 完成压缩，获取剩余数据
    print(f"[DEBUG] 压缩后长度: {len(compressed_data)} 字节")
    return compressed_data

class BinFile:
    """单个bin文件的处理类，负责读取、对齐、压缩和CRC计算"""
    def __init__(self, name: str, file_path: str, id: int, addr: int = 0, compress: bool = False, region_size: int = 0):
        self.name = name                  # 文件名
        self.file_path = file_path        # 文件路径
        self.id = id                      # 文件ID
        self.addr = addr                  # Flash目标地址
        self.compress = compress          # 是否压缩
        self.region_size = region_size    # Flash擦除区域大小
        self.data = b''                   # 原始数据（16字节对齐后）
        self.orig_len = 0                 # 原始数据16字节对齐后的长度
        self.compressed_data = b''        # 压缩后的数据（含分块头部）
        self.compressed_len = 0           # 压缩后的数据总长度
        self.orig_crc = 0                 # 原始数据的CRC32
        self.data_crc = 0                 # 压缩后数据的CRC32
        
    def process(self):
        """处理bin文件：读取→对齐→CRC→压缩（可选）"""
        print(f"\n[INFO] 开始处理bin文件: {self.name}")
        print(f"[INFO] 文件路径: {self.file_path}")
        print(f"[INFO] Flash地址: 0x{self.addr:08X}, 区域大小: 0x{self.region_size:08X}")
        
        # 1. 读取文件数据
        if not os.path.exists(self.file_path):
            raise FileNotFoundError(f"Bin文件不存在: {self.file_path}")
        with open(self.file_path, 'rb') as f:
            self.data = f.read()
        print(f"[INFO] 读取原始数据完成，长度: {len(self.data)} 字节")
        
        # 2. 16字节对齐（不足补0xFF）
        self.orig_len = (len(self.data) + 15) & ~15  # 向上对齐到16的倍数
        if len(self.data) < self.orig_len:
            padding_len = self.orig_len - len(self.data)
            self.data += b'\xFF' * padding_len
            print(f"[INFO] 16字节对齐：填充 {padding_len} 字节0xFF，对齐后长度: {self.orig_len} 字节")
        
        # 3. 计算原始数据CRC32
        self.orig_crc = calculate_crc32(self.data)
        print(f"[INFO] 原始数据CRC32: 0x{self.orig_crc:08X}")
        
        # 4. 压缩处理（按10KB分块）
        if self.compress:
            print(f"[INFO] 启用GZIP压缩，分块大小: 10KB")
            GZIP_PACKET_LEN = 10 * 1024
            compressed_blocks = []
            
            # 压缩头部：原始对齐长度（4字节） + 分块大小（4字节）
            compress_header = struct.pack('<II', self.orig_len, GZIP_PACKET_LEN)
            compressed_blocks.append(compress_header)
            print(f"[INFO] 压缩头部：原始长度={self.orig_len} 字节，分块大小={GZIP_PACKET_LEN} 字节")
            
            # 分块压缩每个数据块
            for i in range(0, self.orig_len, GZIP_PACKET_LEN):
                chunk = self.data[i:i+GZIP_PACKET_LEN]
                chunk_idx = i // GZIP_PACKET_LEN + 1
                print(f"[INFO] 压缩分块 {chunk_idx}：原始长度={len(chunk)} 字节")
                
                compressed_chunk = gzip_compress(chunk)
                # 分块头部：压缩后长度（4字节） + 压缩前长度（4字节）
                chunk_header = struct.pack('<II', len(compressed_chunk), len(chunk))
                compressed_blocks.append(chunk_header)
                compressed_blocks.append(compressed_chunk)
                print(f"[INFO] 压缩分块 {chunk_idx}：压缩后长度={len(compressed_chunk)} 字节")
            
            # 整合所有压缩块
            self.compressed_data = b''.join(compressed_blocks)
            self.compressed_len = len(self.compressed_data)
            self.data_crc = calculate_crc32(self.compressed_data)
            print(f"[INFO] 压缩完成：总长度={self.compressed_len} 字节，压缩数据CRC32: 0x{self.data_crc:08X}")
        else:
            # 不压缩：直接使用原始数据
            print(f"[INFO] 不启用压缩，使用原始对齐数据")
            self.compressed_data = self.data
            self.compressed_len = self.orig_len
            self.data_crc = self.orig_crc
            print(f"[INFO] 数据长度={self.compressed_len} 字节，CRC32: 0x{self.data_crc:08X}")

class OTAPackager:
    """OTA固件包生成类，负责整合所有bin文件和头部信息"""
    def __init__(self, magic: int, version: int):
        self.magic = magic                # 魔数（默认0x5F4F5441，对应_OTA）
        self.version = version            # 版本号
        self.bin_files: List[BinFile] = []# 待打包的bin文件列表
        print(f"[INFO] 初始化OTA打包器")
        print(f"[INFO] 魔数(Magic): 0x{self.magic:08X}（对应字符串：{self._magic_to_str()}）")
        print(f"[INFO] 版本号(Version): 0x{self.version:08X}")
        
    def _magic_to_str(self) -> str:
        """将魔数转换为ASCII字符串（用于直观显示）"""
        try:
            magic_bytes = struct.pack('<I', self.magic)  # 小端序打包后转字符串
            return magic_bytes.decode('ascii', errors='replace').rstrip('\x00')
        except Exception:
            return "无法解析为ASCII字符串"
    
    def add_bin_file(self, bin_file: BinFile):
        """添加bin文件到打包列表（过滤_kvdb.bin后缀）"""
        if not bin_file.name.endswith('_kvdb.bin'):
            self.bin_files.append(bin_file)
            print(f"[INFO] 添加bin文件到列表: {bin_file.name}")
        else:
            print(f"[INFO] 过滤KVDB文件: {bin_file.name}（不加入打包）")
    
    def generate_packet(self, output_path: str) -> bytes:
        """生成完整OTA固件包并写入文件"""
        print(f"\n[INFO] 开始生成OTA固件包，共{len(self.bin_files)}个bin文件")
        
        # 1. 处理所有bin文件（读取、压缩、CRC）
        for i, bin_file in enumerate(self.bin_files):
            print(f"\n{'='*50}")
            print(f"[INFO] 处理第{i+1}/{len(self.bin_files)}个bin文件")
            print(f"{'='*50}")
            bin_file.process()
        
        # 2. 构建所有bin文件的头部（每个76字节）
        bin_headers = b''
        print(f"\n[INFO] 构建所有bin文件头部（每个76字节）")
        for i, bin_file in enumerate(self.bin_files):
            print(f"[INFO] 构建第{i+1}个bin头部: {bin_file.name}")
            # 头部格式：48s(文件名) + H(ID) + H(gzip标志) + I(数据长度) + I(原始长度) + I(地址) + I(数据CRC) + I(原始CRC) + I(区域大小)
            name_bytes = bin_file.name.encode('utf-8')[:48].ljust(48, b'\x00')  # 48字节对齐
            gzip_flag = 1 if bin_file.compress else 0
            bin_header = struct.pack(
                '<48sHHIIIIII',
                name_bytes,
                bin_file.id,
                gzip_flag,
                bin_file.compressed_len,
                bin_file.orig_len,  # 使用 orig_len 而不是 gzip_orilen
                bin_file.addr,
                bin_file.data_crc,
                bin_file.orig_crc,
                bin_file.region_size
            )
            bin_headers += bin_header
        print(f"[INFO] 所有bin头部构建完成，总长度: {len(bin_headers)} 字节")
        
        # 3. 计算头部CRC（魔数+版本+bin数量+所有bin头部）
        print(f"\n[INFO] 计算头部CRC32（魔数+版本+bin数量+所有bin头部）")
        crc_data = struct.pack('<III', self.magic, self.version, len(self.bin_files)) + bin_headers
        header_crc = calculate_crc32(crc_data)
        print(f"[INFO] 头部CRC32计算完成: 0x{header_crc:08X}")
        
        # 4. 构建OTA包总头部（16字节：CRC4 + 魔数4 + 版本4 + bin数量4）
        total_header = struct.pack(
            '<IIII',
            header_crc,       # 头部CRC32
            self.magic,       # 魔数
            self.version,     # 版本号
            len(self.bin_files)  # bin文件数量
        )
        print(f"[INFO] OTA总头部构建完成（16字节）")
        
        # 5. 整合完整OTA包（总头部 + 所有bin头部 + 所有bin数据）
        ota_data = total_header + bin_headers
        print(f"\n[INFO] 整合OTA包数据：")
        print(f"[INFO] - 总头部: 16 字节")
        print(f"[INFO] - 所有bin头部: {len(bin_headers)} 字节")
        for i, bin_file in enumerate(self.bin_files):
            ota_data += bin_file.compressed_data
            print(f"[INFO] - {bin_file.name} 数据: {len(bin_file.compressed_data)} 字节")
        
        # 6. 写入OTA包文件
        with open(output_path, 'wb') as f:
            f.write(ota_data)
        
        # 7. 打印最终信息
        print(f"\n{'='*60}")
        print(f"OTA固件包生成完成！")
        print(f"{'='*60}")
        print(f"输出文件: {output_path}")
        print(f"总大小: {len(ota_data)} 字节 (0x{len(ota_data):08X})")
        print(f"包含bin文件数量: {len(self.bin_files)} 个")
        print(f"魔数: 0x{self.magic:08X}（{self._magic_to_str()}）")
        print(f"版本号: 0x{self.version:08X}")
        print(f"{'='*60}")
        
        return ota_data

def parse_ini_config(ini_path: str):
    """解析INI配置文件，提取COMMON和bin文件配置"""
    print(f"[INFO] 解析INI配置文件: {ini_path}")
    config = configparser.ConfigParser()
    config.read(ini_path, encoding='utf-8')
    
    # 1. 解析COMMON section
    common_config = {
        'file_path': '.',
        'img_flag': 0x5F4F5441,  # 默认魔数_OTA
        'img_ver': 0x00000134    # 默认版本1.3.4
    }
    if 'COMMON' in config:
        common_config['file_path'] = config.get('COMMON', 'FILE_PATH', fallback='.')
        common_config['img_flag'] = int(config.get('COMMON', 'IMG_FLAG', fallback='0x5F4F5441'), 16)
        common_config['img_ver'] = int(config.get('COMMON', 'IMG_VER', fallback='0x00000134'), 16)
    print(f"[INFO] COMMON配置:")
    print(f"[INFO] -  bin文件路径: {common_config['file_path']}")
    print(f"[INFO] -  魔数(IMG_FLAG): 0x{common_config['img_flag']:08X}")
    print(f"[INFO] -  版本号(IMG_VER): 0x{common_config['img_ver']:08X}")
    
    # 2. 解析所有bin文件section（排除COMMON）
    bin_configs = []
    for section in config.sections():
        if section == 'COMMON':
            continue
        if not config.has_option(section, 'NAME'):
            print(f"[WARN] 跳过section {section}：缺少NAME字段")
            continue
        
        # 提取bin文件配置（默认值合理设置）
        bin_cfg = {
            'name': config.get(section, 'NAME'),
            'sel': config.getint(section, 'SEL', fallback=1),  # 是否选中（1=选中）
            'gzip': config.getint(section, 'GZIP', fallback=0),# 是否压缩（1=压缩）
            'idx': config.getint(section, 'IDX', fallback=len(bin_configs)),  # 文件ID
            'addr': int(config.get(section, 'ADDR', fallback='0'), 16),  # Flash地址
            'region_size': int(config.get(section, 'REGION_SIZE', fallback='0'), 16)  # 擦除区域大小
        }
        bin_configs.append(bin_cfg)
        print(f"[INFO] 解析section {section}: {bin_cfg['name']}（SEL={bin_cfg['sel']}, GZIP={bin_cfg['gzip']}）")
    
    # 过滤未选中的bin文件（SEL=1）
    selected_bin_configs = [cfg for cfg in bin_configs if cfg['sel'] == 1]
    print(f"[INFO] 配置文件解析完成：共{len(bin_configs)}个bin配置，选中{len(selected_bin_configs)}个")
    
    return common_config, selected_bin_configs

def create_ota_from_ini(ini_path: str, output_path: str):
    """从INI配置文件创建OTA固件包"""
    # 1. 解析INI配置
    common_cfg, bin_cfg_list = parse_ini_config(ini_path)
    if not bin_cfg_list:
        raise ValueError("INI配置中无选中的bin文件，无法生成OTA包")
    
    # 2. 创建OTA打包器
    packager = OTAPackager(common_cfg['img_flag'], common_cfg['img_ver'])
    
    # 3. 添加所有选中的bin文件
    bin_base_path = common_cfg['file_path']
    print(f"\n[INFO] 从路径 {bin_base_path} 加载bin文件")
    for bin_cfg in bin_cfg_list:
        bin_full_path = os.path.join(bin_base_path, bin_cfg['name'])
        # 创建BinFile对象
        bin_file = BinFile(
            name=bin_cfg['name'],
            file_path=bin_full_path,
            id=bin_cfg['idx'],
            addr=bin_cfg['addr'],
            compress=(bin_cfg['gzip'] == 1),
            region_size=bin_cfg['region_size']
        )
        packager.add_bin_file(bin_file)
    
    # 4. 生成OTA包
    packager.generate_packet(output_path)

def main():
    """主函数：解析命令行参数并执行打包"""
    parser = argparse.ArgumentParser(description='OTA固件打包工具（Python版）')
    parser.add_argument('--ini', required=True, help='INI配置文件路径（必填）')
    parser.add_argument('--output', default='ota_mix.bin', help='输出OTA包路径（默认：ota_mix.bin）')
    
    args = parser.parse_args()
    
    print("="*70)
    print("          OTA固件打包工具          ")
    print("="*70)
    print(f"INI配置文件: {args.ini}")
    print(f"输出OTA文件: {args.output}")
    print("="*70)
    
    try:
        create_ota_from_ini(args.ini, args.output)
        print("\n[SUCCESS] OTA打包任务完成！")
    except Exception as e:
        print(f"\n[ERROR] OTA打包失败: {str(e)}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == '__main__':
    import traceback
    main()