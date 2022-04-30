parameters = {
    'defaults': {
        'file_size': 1 << 20,
        'num_files': 100,
        'sq_poll': False,
        'num_buffers': 1,
        'ring_size': 1024,
    },
}

import argparse
import sys
import os
import shutil
import subprocess
import json
from copy import deepcopy
from time import time as orig_time

# NOTE: self._rq_sizes can be modified

DEFAULT_NUM_FILES = 100
DEFAULT_FILE_SIZE = 1024 * 1024
BUFFER_SIZES = [4096, 8192]


ORIGINAL_CP_BIN_NAME = 'cp'
FCP_BIN_NAME = 'fcp'
DEBUG = True

def time():
    return orig_time()

def debug(*args, **kwargs):
    if not DEBUG:
        return
    print(*args, **kwargs)

# FIXME: hack
sys.path.append('../../')

from tests.generator import DirCreator


def get_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', dest='bin_dir', type=str, required=True, help='Directory where binaries (cp, fcp) can be found')
    parser.add_argument('-t', dest='target_dir', type=str, required=True, help='Directory to run tests at')
    parser.add_argument('-n', dest='num_files', type=int, required=False, default=DEFAULT_NUM_FILES, help="The num of files to perform experiments on")
    parser.add_argument('-s', dest='file_size', type=int, required=False, default=DEFAULT_FILE_SIZE, help="The file size to perform experiments on")
    parser.add_argument('-k', '--sq-poll', dest='sq_poll', required=False, action='store_true', help='Enable sq poll')

    parser.add_argument('--skip_cleanup', dest='skip_cleanup', default=False, action='store_true')
    parser.add_argument('--skip_sanity', dest='skip_sanity', default=False, action='store_true')
    parser.add_argument('--skip_drop', dest='skip_drop', default=False, action='store_true')

    return parser

class GenericGraph():
    def __init__(self, config):
        self._config = config
        self._name = config['name']
        self._work_dir = os.path.join(config['target_dir'], self._name)
        self._variant = config['variant']
        self._variant_param = self._variant['param']
        self._variant_values = self._variant['values']
        self._bin_dir = config['bin_dir']
        
        self._created_dirs = []
        self._results = {
            'fcp':  {
                self._variant_param: [],
                'time': []
            },
            'cp': {
                self._variant_param: [],
                'time': []
            }
        }

    def setup_wdir(self):
        if os.path.exists(self._work_dir):
            raise Exception(f"Error, working dir {self._work_dir} directory already exists!")
        os.mkdir(self._work_dir)

    def _get_root_name(self, suffix):
        return f'rootdir_{suffix}'

    def _get_root_path(self, suffix):
        return os.path.join(self._work_dir, self._get_root_name(suffix))

    def _get_copy_root_name(self, suffix):
        return f'cp_rootdir_{suffix}'

    def _get_copy_root_path(self, suffix):
        return os.path.join(self._work_dir, self._get_copy_root_name(suffix))

    def _get_dir_args(self, variant_val):
        file_size = self._config['params_info']['file_size']['default']
        num_files = self._config['params_info']['num_files']['default']
        if self._variant_param == 'file_size':
            file_size = variant_val
        elif self._variant_param == 'num_files':
            num_files = variant_val
        return file_size, num_files

    def create_required_workloads(self):
        dircr_params = {'file_size', 'num_files'}
        for val in self._variant_values:
            root_path = self._get_root_path(val)
            self._created_dirs.append(root_path)
            work_gen = DirCreator(root_path)
            file_size, num_files = self._get_dir_args(val)
            work_gen.create(1, 0, num_files, (file_size, file_size))

    def _ensure_not_present(self, path):
        if os.path.exists(path):
            raise Exception(f'Path {path} already exists')

    def _get_fcp_opts(self, variant_val):
        opts = ''
        config = deepcopy(self._config['params_info'])
        config[self._variant_param]['default'] = variant_val

        for k, v in config.items():
            if v.get('fcp_flag'):
                if k == 'sq_poll' and v ['default'] == False:
                    continue
                opts += f'{v["fcp_flag"]} {v["default"]} '

        
        debug(f"The final fcp opts are: {opts}")
        return opts

    def _get_fcp_path(self):
        return os.path.join(self._bin_dir, FCP_BIN_NAME)
    
    def _get_command_fcp(self, src_path, dst_path, variant_val):
        bin_path = self._get_fcp_path()
        command = f'{bin_path} ' + self._get_fcp_opts(variant_val) + f'-r {src_path} {dst_path}'
        return command.split()

    def _drop_cache(self):
        # with open('/proc/sys/vm/drop_caches', 'w') as fh:
        #     fh.write('3')
        return

    def _run_cmd(self, command):
        debug(f'Executing command {" ".join(command)}')
        self._drop_cache()
        
        t1 = time()
        proc = subprocess.Popen(command)
        ret = proc.wait()
        t2 = time()
        if(ret != 0):
            raise Exception(f'Error doing copy for command: {command}')
        
        debug(f'{t2-t1} is the time taken')

        return t2 - t1

    def _run_workload_fcp(self, variant_val):
        src_path = self._get_root_path(variant_val)
        dst_path = self._get_copy_root_path(variant_val)

        self._ensure_not_present(dst_path)
        command = self._get_command_fcp(src_path, dst_path, variant_val)
        
        t = self._run_cmd(command)

        self._results['fcp'][self._variant_param].append(variant_val)
        self._results['fcp']['time'].append(t)

        shutil.rmtree(dst_path)

    def run_workloads_fcp(self):
        for val in self._variant_values:
            self._run_workload_fcp(val)
        
    def cleanup(self):
        for direc in self._created_dirs:
            shutil.rmtree(direc)

    def get_results(self):
        res = {
            'meta': {
                'name': self._name,
                'parsed_config': self._config
            },
            'data': self._results
        }
        return res

    def _get_cp_results_path(self):
        return os.path.join(self._work_dir, 'results.json')

    def dump_results(self):
        path = self._get_cp_results_path()
        res = self.get_results()
        with open(path, 'w') as fh:
            fh.write(json.dumps(res, indent=2))


def get_params_info():
    params = {
        'file_size': {
            'default': 1 << 20,
        },
        'num_files': {
            'default': 100
        },
        'sq_poll': {
            'default': False,
            'fcp_flag': '-k'
        },
        'num_buffers': {
            'default': 1,
            'fcp_flag': '-n'
        },
        'buffer_size': {
            'default': 4096,
            'fcp_flag': '-b'
        },
        'ring_size': {
            'default': 1024,
            'fcp_flag': '-q'
        }
    }
    return params


def get_parsed():
    with open("config.json") as fh:
        config = json.loads(fh.read())
    params = get_params_info()

    # override the defaults
    for k, v in config.get('invariants', {}).items():
        params[k]['default'] = v
    
    return {
        'variant': {
            'param': config['variant']['param'],
            'values': config['variant']['values']
        },
        'params_info': params,
        'name': config['name'],
        'target_dir': config['target_dir'],
        'bin_dir': config['bin_dir'],
        'skip_cleanup': config.get('skip_cleanup', False)
    }

def main():
    parsed = get_parsed()

    graph = GenericGraph(parsed)

    graph.setup_wdir()
    graph.create_required_workloads()
    graph.run_workloads_fcp()
    graph.dump_results()
    # graph.run_workloads_cp()
    # graph.dump_results()

    if not parsed['skip_cleanup']:
        graph.cleanup()

if __name__ == '__main__':
    main()
