# SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
import fnmatch
import glob
import json
import locale
import multiprocessing
import os
import re
import shutil
import subprocess
import sys
import tomllib
from typing import Any
from typing import Dict
from typing import List
from typing import Optional
from typing import Tuple
from urllib.error import URLError
from urllib.request import Request
from urllib.request import urlopen
from webbrowser import open_new_tab

import tomli_w

import click
from click.core import Context
from sdk_py_actions.constants import GENERATORS
from sdk_py_actions.constants import PREVIEW_TARGETS
from sdk_py_actions.constants import SUPPORTED_TARGETS
from sdk_py_actions.constants import URL_TO_DOC
from sdk_py_actions.errors import FatalError
from sdk_py_actions.global_options import global_options
from sdk_py_actions.tools import ensure_build_directory
from sdk_py_actions.tools import generate_hints
from sdk_py_actions.tools import sdk_version
from sdk_py_actions.tools import merge_action_lists
from sdk_py_actions.tools import print_warning
from sdk_py_actions.tools import PropertyDict
from sdk_py_actions.tools import run_target
from sdk_py_actions.tools import TargetChoice
from sdk_py_actions.tools import yellow_print


def action_extensions(base_actions: Dict, project_path: str) -> Any:

    def read_project_config(project_dir: str) -> Dict[str, Optional[str]]:
        """Read project configuration from .project.toml file."""
        config_path = os.path.join(project_dir, '.project.toml')
        config = {
            'board': None,
            'board_search_path': None
        }
        
        if not os.path.exists(config_path):
            return config
        
        try:
            with open(config_path, 'rb') as f:
                data = tomllib.load(f)
                config['board'] = data.get('board')
                config['board_search_path'] = data.get('board_search_path')
        except Exception as e:
            print_warning(f'Warning: Failed to read .project.toml: {e}')
        
        return config

    def write_project_config(project_dir: str, board: Optional[str], board_search_path: Optional[str]) -> None:
        """Write project configuration to .project.toml file."""
        config_path = os.path.join(project_dir, '.project.toml')
        
        # Read existing config
        config = {}
        if os.path.exists(config_path):
            try:
                with open(config_path, 'rb') as f:
                    config = tomllib.load(f)
            except Exception:
                pass
        
        # Update config
        if board is not None:
            config['board'] = board
        if board_search_path is not None:
            config['board_search_path'] = board_search_path
        
        # Write config
        try:
            with open(config_path, 'wb') as f:
                tomli_w.dump(config, f)
            print(f'Project configuration saved to {config_path}')
        except Exception as e:
            raise FatalError(f'Failed to write .project.toml: {e}')

    def resolve_board_options(project_dir: str, board: Optional[str], board_search_path: Optional[str], require_board: bool=False
                              ) -> Tuple[Optional[str], Optional[str]]:
        """Resolve board options using CLI args or fallback to .project.toml."""
        resolved_board = board
        resolved_board_search_path = board_search_path

        if resolved_board is None or resolved_board_search_path is None:
            config = read_project_config(project_dir)
            if resolved_board is None and config.get('board'):
                resolved_board = config['board']
                print(f'Using board from .project.toml: {resolved_board}')
            if resolved_board_search_path is None and config.get('board_search_path'):
                resolved_board_search_path = config['board_search_path']
                print(f'Using board_search_path from .project.toml: {resolved_board_search_path}')

        if require_board and not resolved_board:
            raise FatalError('Board name is required. Use "--board" or run "sdk.py set-target" to configure it.')

        return resolved_board, resolved_board_search_path

    def set_target(target_name: str, ctx: Context, args: PropertyDict, board: str, board_search_path: Optional[str]) -> None:
        """
        Set target board configuration and save to .project.toml file.
        """
        project_dir = args.project_dir
        
        if not board:
            raise FatalError('Board name is required. Usage: sdk.py set-target <board> [--board_search_path <path>]')
        
        write_project_config(project_dir, board, board_search_path)
        
        print(f'Target board set to: {board}')
        if board_search_path:
            print(f'Board search path set to: {board_search_path}')

    def menuconfig(target_name: str, ctx: Context, args: PropertyDict, board: Optional[str], board_search_path: Optional[str]) -> None:
        """
        Menuconfig target is build_target extended with the style argument for setting the value for the environment
        variable.
        """
        project_dir = args.project_dir

        board, board_search_path = resolve_board_options(project_dir, board, board_search_path)
        
        menuconfig_path = os.path.join(os.environ['SIFLI_SDK_PATH'], 'tools', 'kconfig', 'menuconfig.py')
        board_arg = ['--board', board] if board else []
        board_search_path_arg = ['--board_search_path', board_search_path] if board_search_path else []
        subprocess.run([sys.executable, menuconfig_path] + board_arg + board_search_path_arg)

    def build_callback(target_name: str, ctx: Context, args: PropertyDict, board: Optional[str], board_search_path: Optional[str],
                       jobs: Optional[int]) -> None:
        """Build project with scons."""
        project_dir = args.project_dir
        board, board_search_path = resolve_board_options(project_dir, board, board_search_path, require_board=True)

        if jobs in (None, 0):
            try:
                jobs = multiprocessing.cpu_count()
            except NotImplementedError:
                jobs = 1

        cmd = ['scons', f'--board={board}']
        if board_search_path:
            cmd.append(f'--board_search_path={board_search_path}')
        cmd.append(f'-j{jobs}')

        try:
            subprocess.run(cmd, cwd=project_dir, check=True)
        except subprocess.CalledProcessError as e:
            raise FatalError(f'scons build failed with exit code {e.returncode}')
        except FileNotFoundError:
            raise FatalError('scons executable not found. Please ensure SCons is installed and available in PATH.')

    def fullclean_callback(target_name: str, ctx: Context, args: PropertyDict) -> None:
        """Remove build directories for the configured board."""
        project_dir = args.project_dir
        config = read_project_config(project_dir)
        board = config.get('board')

        if not board:
            raise FatalError('No board configured. Please run "sdk.py set-target <board>" first.')

        patterns = [
            os.path.join(project_dir, f'build_{board}'),
            os.path.join(project_dir, f'build_{board}_*'),
        ]

        removed_dirs: List[str] = []
        project_root = os.path.realpath(project_dir)
        for pattern in patterns:
            for path in glob.glob(pattern):
                if not os.path.isdir(path):
                    continue
                abs_path = os.path.realpath(path)
                if os.path.commonpath([project_root, abs_path]) != project_root:
                    continue
                if abs_path == project_root:
                    continue
                print(f'Removing {abs_path}')
                try:
                    shutil.rmtree(abs_path)
                    removed_dirs.append(abs_path)
                except OSError as e:
                    raise FatalError(f'Failed to remove {abs_path}: {e}') from e

        if not removed_dirs:
            print(f'No build directories matching "build_{board}_*" found in {project_dir}')

    def verbose_callback(ctx: Context, param: List, value: str) -> Optional[str]:
        if not value or ctx.resilient_parsing:
            return None

        for line in ctx.command.verbose_output:
            print(line)

        return value

    def validate_root_options(ctx: Context, args: PropertyDict, tasks: List) -> None:
        args.project_dir = os.path.realpath(args.project_dir)
        if args.build_dir is not None and args.project_dir == os.path.realpath(args.build_dir):
            raise FatalError(
                'Setting the build directory to the project directory is not supported. Suggest dropping '
                "--build-dir option, the default is a 'build' subdirectory inside the project directory.")
        if args.build_dir is None:
            args.build_dir = os.path.join(args.project_dir, 'build')
        args.build_dir = os.path.realpath(args.build_dir)

    def sdk_version_callback(ctx: Context, param: str, value: str) -> None:
        if not value or ctx.resilient_parsing:
            return

        version = sdk_version()

        if not version:
            raise FatalError('SiFli-SDK version cannot be determined')

        print('SiFli-SDK %s' % version)
        sys.exit(0)

    def help_and_exit(action: str, ctx: Context, param: List, json_option: bool, add_options: bool) -> None:
        if json_option:
            output_dict = {}
            output_dict['target'] = get_target(param.project_dir)  # type: ignore
            output_dict['actions'] = []
            actions = ctx.to_info_dict().get('command').get('commands')
            for a in actions:
                action_info = {}
                action_info['name'] = a
                action_info['description'] = actions[a].get('help')
                if add_options:
                    action_info['options'] = actions[a].get('params')
                output_dict['actions'].append(action_info)
            print(json.dumps(output_dict, sort_keys=True, indent=4))
        else:
            print(ctx.get_help())
        ctx.exit()

    root_options = {
        'global_options': [
            {
                'names': ['--version'],
                'help': 'Show SiFli-SDK version and exit.',
                'is_flag': True,
                'expose_value': False,
                'callback': sdk_version_callback,
            },
            {
                'names': ['-C', '--project-dir'],
                'scope': 'shared',
                'help': 'Project directory.',
                'type': click.Path(),
                'default': os.getcwd(),
            },
            {
                'names': ['-B', '--build-dir'],
                'help': 'Build directory.',
                'type': click.Path(),
                'default': None,
            },
            {
                'names': ['-v', '--verbose'],
                'help': 'Verbose build output.',
                'is_flag': True,
                'is_eager': True,
                'default': False,
                'callback': verbose_callback,
            },
            {
                'names': ['--no-hints'],
                'help': 'Disable hints on how to resolve errors and logging.',
                'is_flag': True,
                'default': False
            }
        ],
        'global_action_callbacks': [validate_root_options],
    }

    board_options = [
        {
            'names': ['--board'],
            'help': (
                'board name'),
            'envvar': 'MENUCONFIG_BOARD',
            'default': None,
        },
        {
            'names': ['--board_search_path'],
            'help': (
                'board search path'),
            'envvar': 'MENUCONFIG_BOARD_SEARCH_PATH',
            'default': None,
        },
    ]

    build_actions = {
        'actions': {
            'set-target': {
                'callback': set_target,
                'help': 'Set target board configuration and save to .project.toml file.',
                'arguments': [
                    {
                        'names': ['board'],
                        'required': True,
                    },
                ],
                'options': global_options + [
                    {
                        'names': ['--board_search_path'],
                        'help': 'Board search path',
                        'default': None,
                    },
                ],
            },
            'menuconfig': {
                'callback': menuconfig,
                'help': 'Run "menuconfig" project configuration tool.',
                'options': global_options + board_options,
            },
            'build': {
                'callback': build_callback,
                'help': 'Build project with scons.',
                'options': global_options + board_options + [
                    {
                        'names': ['-j', '--jobs'],
                        'help': 'Number of parallel jobs for scons. Use without value to default to CPU count.',
                        'type': int,
                        'default': None,
                    },
                ],
            },
            'fullclean': {
                'callback': fullclean_callback,
                'help': 'Remove build directories for the configured board.',
                'options': global_options,
            },
        }
    }

    help_action = {
        'actions': {
            'help': {
                'callback': help_and_exit,
                'help': 'Show help message and exit.',
                'hidden': True,
                'options': [
                    {
                        'names': ['--json', 'json_option'],
                        'is_flag': True,
                        'help': 'Print out actions in machine-readable format for selected target.'
                    },
                    {
                        'names': ['--add-options'],
                        'is_flag': True,
                        'help': 'Add options about actions to machine-readable format.'
                    }
                ],
            }
        }
    }

    return merge_action_lists(root_options, build_actions, help_action)
