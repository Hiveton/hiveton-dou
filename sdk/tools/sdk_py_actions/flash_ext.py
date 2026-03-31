# -*- coding:utf-8 -*-
#
# File      : flash_ext.py
# This file is part of SiFli-SDK
# Flash management extension for sdk.py
#
# Change Logs:
# Date           Author          Notes
# 2025-09-22     SiFli           Add flash command for sdk.py
# 2025-10-20     SiFli           Implement flash download functionality
#

import os
import sys
import platform
import subprocess
import json
from typing import Any, Dict

import click
from click.core import Context
from sdk_py_actions.tools import PropertyDict


def find_build_dir(device: str = None) -> str:
    """Find the build directory in the project.
    
    The SiFli SDK build system generates directories with names like:
    - build_sf32lb56-lcd_a128r12n1_hcpu
    - build_sf32lb52x-evb_hcpu
    
    Args:
        device: Device/board configuration string (e.g., 'sf32lb56-lcd_a128r12n1_hcpu')
                Must be the exact, full configuration name. REQUIRED!
    
    Returns:
        Path to the build directory
    """
    cwd = os.getcwd()
    
    # Device must be specified - no auto-detection to avoid mistakes
    if not device:
        raise ValueError("Device (-d/--device) parameter is required. "
                        "Please specify the exact board/device configuration name.")
    
    # Look for exact match: build_<device>
    build_path = os.path.join(cwd, f'build_{device}')
    return build_path


def find_sftool_param(build_dir: str) -> str:
    """Find sftool_param.json file in build directory."""
    json_path = os.path.join(build_dir, 'sftool_param.json')
    if not os.path.exists(json_path):
        raise FileNotFoundError(f"sftool_param.json not found in {build_dir}. Please build the project first.")
    return json_path


def find_download_script(build_dir: str) -> str:
    """Find download script based on platform."""
    system = platform.system()
    
    if system == 'Windows':
        script_path = os.path.join(build_dir, 'download.bat')
    else:  # Linux or macOS
        script_path = os.path.join(build_dir, 'download.sh')
    
    if not os.path.exists(script_path):
        raise FileNotFoundError(f"Download script not found: {script_path}. Please build the project first.")
    
    return script_path


def flash_uart(port: str, baud: int, build_dir: str) -> None:
    """Flash firmware using UART protocol with sftool."""
    try:
        # Find sftool_param.json
        json_path = find_sftool_param(build_dir)
        
        # Read sftool configuration
        with open(json_path, 'r', encoding='utf-8') as f:
            config = json.load(f)
        
        chip = config.get('chip', '')
        memory = config.get('memory', 'NOR')
        write_flash = config.get('write_flash', {})
        files = write_flash.get('files', [])
        
        if not files:
            print("Error: No files to flash found in sftool_param.json")
            return
        
        # Build sftool command
        # Format: sftool -p <port> -b <baud> -c <chip> -m <memory> write_flash <files>
        cmd = ['sftool', '-p', port, '-b', str(baud), '-c', chip, '-m', memory.lower(), 'write_flash']
        
        # Add files
        for file_info in files:
            file_path = file_info.get('path', '')
            address = file_info.get('address', None)
            
            # Convert relative path to absolute path based on build_dir
            if not os.path.isabs(file_path):
                file_path = os.path.join(build_dir, file_path)
            
            if not os.path.exists(file_path):
                print(f"Warning: File not found: {file_path}")
                continue
            
            # Add file to command
            if address:
                cmd.append(f'{file_path}@{address}')
            else:
                cmd.append(file_path)
        
        # Execute sftool command
        print(f"Executing: {' '.join(cmd)}")
        print(f"Flashing via UART on port {port} at {baud} baud...")
        
        # Change to build directory for execution
        original_dir = os.getcwd()
        try:
            os.chdir(build_dir)
            result = subprocess.run(cmd, check=True)
            if result.returncode == 0:
                print("Flash completed successfully!")
            else:
                print(f"Flash failed with return code: {result.returncode}")
        finally:
            os.chdir(original_dir)
            
    except FileNotFoundError as e:
        print(f"Error: {e}")
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Failed to parse sftool_param.json: {e}")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"Error: Flash operation failed: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


def flash_jlink(build_dir: str) -> None:
    """Flash firmware using JLink protocol."""
    try:
        # Find download script
        script_path = find_download_script(build_dir)
        
        print(f"Flashing via JLink using script: {script_path}")
        
        # Change to build directory for execution
        original_dir = os.getcwd()
        try:
            os.chdir(build_dir)
            
            # Execute download script
            system = platform.system()
            if system == 'Windows':
                # Execute batch file
                result = subprocess.run([script_path], shell=True, check=True)
            else:
                # Execute shell script
                result = subprocess.run(['bash', script_path], check=True)
            
            if result.returncode == 0:
                print("Flash completed successfully!")
            else:
                print(f"Flash failed with return code: {result.returncode}")
        finally:
            os.chdir(original_dir)
            
    except FileNotFoundError as e:
        print(f"Error: {e}")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"Error: Flash operation failed: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


def action_extensions(base_actions: Dict, project_path: str) -> Any:
    """Flash download extension for sdk.py."""
    
    def flash_callback(target_name: str, ctx: Context, args: PropertyDict, 
                      protocol: str = 'uart', 
                      port: str = None, 
                      baud: int = 1000000,
                      device: str = None) -> None:
        """Flash firmware to device."""
        
        # Find build directory based on device
        try:
            build_dir = find_build_dir(device)
        except ValueError as e:
            print(f"Error: {e}")
            print("\nAvailable build directories:")
            import glob
            builds = glob.glob(os.path.join(os.getcwd(), 'build_*'))
            if builds:
                for b in builds:
                    dir_name = os.path.basename(b)
                    # Extract device name from build_<device>
                    device_name = dir_name[6:] if dir_name.startswith('build_') else dir_name
                    print(f"  - {dir_name}")
                    print(f"    Use: -d {device_name}")
            else:
                print("  (none found - please build the project first)")
            print("\n Example: sdk.py flash -p COM3 -d sf32lb56-lcd_a128r12n1_hcpu")
            sys.exit(1)
        
        # Show which build directory is being used
        print(f"Using build directory: {build_dir}")
        
        if not os.path.isdir(build_dir):
            print(f"Error: Build directory not found: {build_dir}")
            if device:
                print(f"\nSpecified device: '{device}'")
                print(f"Expected directory: build_{device}")
                print("\nAvailable build directories:")
                import glob
                builds = glob.glob(os.path.join(os.getcwd(), 'build_*'))
                if builds:
                    for b in builds:
                        dir_name = os.path.basename(b)
                        # Extract device name from build_<device>
                        device_name = dir_name[6:] if dir_name.startswith('build_') else dir_name
                        print(f"  - {dir_name}")
                        print(f"    Use: -d {device_name}")
                else:
                    print("  (none found)")
                print("\n Tip: Use the full device name with -d parameter")
                print("   Example: sdk.py flash -p COM3 -d sf32lb56-lcd_a128r12n1_hcpu")
            else:
                print("Available build directories:")
                import glob
                builds = glob.glob(os.path.join(os.getcwd(), 'build_*'))
                if builds:
                    for b in builds:
                        print(f"  - {os.path.basename(b)}")
                else:
                    print("  (none found)")
            print("\nPlease build the project first.")
            sys.exit(1)
        
        # Validate protocol
        protocol = protocol.lower()
        if protocol not in ['uart', 'jlink']:
            print(f"Error: Invalid protocol '{protocol}'. Must be 'uart' or 'jlink'.")
            sys.exit(1)
        
        # Execute flash based on protocol
        if protocol == 'uart':
            # Validate UART parameters
            if not port:
                print("Error: Port (-p/--port) is required for UART protocol.")
                sys.exit(1)
            
            # Ensure port format (add COM prefix on Windows if needed)
            # Support both 'com' and 'COM' formats
            if platform.system() == 'Windows':
                port_upper = port.upper()
                if not port_upper.startswith('COM'):
                    # User input is a number, add COM prefix
                    if port.isdigit():
                        port = f'COM{port}'
                elif port != port_upper:
                    # User input is 'comX', convert to 'COMX'
                    port = port_upper
            
            flash_uart(port, baud, build_dir)
        else:  # jlink
            flash_jlink(build_dir)
    
    flash_actions = {
        'actions': {
            'flash': {
                'callback': flash_callback,
                'help': 'Flash firmware to device using UART or JLink protocol.',
                'options': [
                    {
                        'names': ['-u', '--protocol'],
                        'help': 'Download protocol: uart or jlink (default: uart).',
                        'type': click.Choice(['uart', 'jlink'], case_sensitive=False),
                        'default': 'uart',
                    },
                    {
                        'names': ['-p', '--port'],
                        'help': 'Serial port for UART download (e.g., COM3, /dev/ttyUSB0). Required for UART protocol.',
                        'type': str,
                        'default': None,
                    },
                    {
                        'names': ['-b', '--baud'],
                        'help': 'Baud rate for UART download (default: 1000000).',
                        'type': int,
                        'default': 1000000,
                    },
                    {
                        'names': ['-d', '--device'],
                        'help': 'Board/device configuration (e.g., sf32lb56-lcd_a128r12n1_hcpu) to locate build directory. REQUIRED.',
                        'type': str,
                        'default': None,
                        'required': True,
                    },
                ],
            },
        }
    }

    return flash_actions