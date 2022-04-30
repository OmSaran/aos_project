import argparse
import sys
import os
import shutil
import subprocess
import json
import statistics
from copy import deepcopy
from time import time as orig_time

DEFAULT_NUM_FILES = 100
DEFAULT_FILE_SIZE = 1024 * 1024
BUFFER_SIZES = [4096, 8192]


ORIGINAL_CP_BIN_NAME = 'cp'
FCP_BIN_NAME = 'fcp'

DEBUG = os.environ.get('DEBUG')

def time():
    return orig_time()

def debug(*args, **kwargs):
    if not DEBUG:
        return
    print(*args, **kwargs)

def info(*args, **kwargs):
    if not DEBUG:
        return
    print(*args, **kwargs)

# FIXME: hack
sys.path.append('../../')

from tests.generator import DirCreator

class GenericGraph():
    def __init__(self, config):
        self._config = config
        self._name = config['name']
        self._work_dir = os.path.join(config['target_dir'], self._name)
        self._variant = config['variant']
        self._variant_param = self._variant['param']
        self._variant_values = self._variant['values']
        self._bin_dir = config['bin_dir']
        if config.get('results_dir'):
            self._results_dir = config['results_dir']
        else:
            self._results_dir = self._work_dir
        
        self._created_dirs = []

        res = {
            self._variant_param: [],
            'time_mean': [],
            'time_std_dev': [],
            'real_time_mean': [],
            'real_time_std_dev': [],
            'user_time_mean': [],
            'user_time_std_dev': [],
            'sys_time_mean': [],
            'sys_time_std_dev': []
        }
        self._results = {
            'fcp':  deepcopy(res),
            'cp': deepcopy(res)
        }

        self._validate()

    def _validate(self):
        path = self._get_results_path()
        if os.path.exists(path):
            raise Exception(f"Results file {path} already exists")

    def setup_wdir(self):
        if os.path.exists(self._work_dir):
            raise Exception(f"Error, working dir {self._work_dir} directory already exists!")
        os.mkdir(self._work_dir)

    def _get_root_name(self, suffix):
        return f'rootdir_{suffix}'

    def _get_root_path(self, suffix):
        if not self._is_dircr_variant():
            return os.path.join(self._work_dir, self._get_root_name(self._variant_values[0]))
        return os.path.join(self._work_dir, self._get_root_name(suffix))

    def _get_copy_root_name(self, suffix):
        return f'cp_rootdir_{suffix}'

    def _get_copy_root_path(self, suffix):
        if not self._is_dircr_variant():
            return os.path.join(self._work_dir, self._get_copy_root_name(self._variant_values[0]))
        return os.path.join(self._work_dir, self._get_copy_root_name(suffix))

    def _get_dir_args(self, variant_val):
        file_size = self._config['params_info']['file_size_bytes']['default']
        num_files = self._config['params_info']['num_files']['default']
        if self._variant_param == 'file_size_bytes':
            file_size = variant_val
        elif self._variant_param == 'num_files':
            num_files = variant_val
        return file_size, num_files

    def _is_dircr_variant(self):
        if self._variant_param in {'num_files', 'file_size_bytes', 'depth'}:
            return True
        return False

    def create_required_workloads(self):
        # FIXME: We don't need to create multiple files unless the variant is either num_files or file_size
        if not self._is_dircr_variant():
            root_path = self._get_root_path(self._variant_values[0])
            self._created_dirs.append(root_path)
            work_gen = DirCreator(root_path)
            file_size, num_files = self._get_dir_args(self._variant_values[0])
            work_gen.create(1, 0, num_files, (file_size, file_size))
        else:
            for val in self._variant_values:
                root_path = self._get_root_path(val)
                self._created_dirs.append(root_path)
                work_gen = DirCreator(root_path)
                file_size, num_files = self._get_dir_args(val)
                work_gen.create(1, 0, num_files, (file_size, file_size))

    def _ensure_not_present(self, path):
        if os.path.exists(path):
            raise Exception(f'Path {path} already exists')

    def _get_fcp_path(self):
        return os.path.join(self._bin_dir, FCP_BIN_NAME)
    
    def _get_cp_path(self):
        return os.path.join(self._bin_dir, ORIGINAL_CP_BIN_NAME)

    def _drop_cache(self):
        debug("doing sync")
        os.sync()
        debug("dropping cache")
        with open('/proc/sys/vm/drop_caches', 'w') as fh:
            fh.write('3')

    def _run_cmd(self, command, dst_path):
        num_times = 5

        sys_times = []
        user_times = []
        real_times = []
        ptimes = []

        time_cmd = '/usr/bin/time -f %e\n%U\n%S'.split(' ')
        command = time_cmd + command

        debug(f'Executing command {" ".join(command)} {num_times} times')

        for _ in range(num_times):
            self._drop_cache()

            proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            t1 = time()
            ret = proc.wait()
            t2 = time()
            if(ret != 0):
                err = proc.stderr.read().decode()
                raise Exception(f'Error doing copy for command: {command}: {err}')

            out = proc.stdout.read().decode()
            err = proc.stderr.read().decode()

            debug(out)
            debug(err)

            time_op = err.split()
            sys_time = float(time_op[-1])
            user_time = float(time_op[-2])
            real_time = float(time_op[-3])

            sys_times.append(sys_time)
            user_times.append(user_time)
            real_times.append(real_time)
            ptimes.append(t2-t1)
            
            debug(f'{t2-t1} is the ptime taken')

            shutil.rmtree(dst_path)


        return {
            'real_times': real_times, 
            'user_times': user_times, 
            'sys_times': sys_times, 
            'ptimes': ptimes
        }

    def _get_fcp_opts(self, variant_val):
        opts = ''
        config = deepcopy(self._config['params_info'])
        config[self._variant_param]['default'] = variant_val

        for k, v in config.items():
            if v.get('fcp_flag'):
                if isinstance(v['default'], bool):
                    if not v['default']:
                        continue
                    opts += f'{v["fcp_flag"]} '
                    continue
                opts += f'{v["fcp_flag"]} {v["default"]} '

        
        debug(f"The final fcp opts are: {opts}")
        return opts

    def _get_cp_opts(self, variant_val):
        opts = ''
        config = deepcopy(self._config['params_info'])
        config[self._variant_param]['default'] = variant_val

        for k, v in config.items():
            if v.get('cp_flag'):
                opts += f'{v["cp_flag"]} {v["default"]} '

        
        debug(f"The final fcp opts are: {opts}")
        return opts

    def _get_command_fcp(self, src_path, dst_path, variant_val):
        bin_path = self._get_fcp_path()
        command = f'{bin_path} ' + self._get_fcp_opts(variant_val) + f'-r {src_path} {dst_path}'
        return command.split()

    def _get_command_cp(self, src_path, dst_path, variant_val):
        bin_path = self._get_cp_path()
        command = f'{bin_path} ' + self._get_cp_opts(variant_val) + f'-r {src_path} {dst_path}'
        return command.split()

    def _run_workload_type(self, variant_val, cp_type):
        src_path = self._get_root_path(variant_val)
        dst_path = self._get_copy_root_path(variant_val)

        self._ensure_not_present(dst_path)
        if cp_type == 'fcp':
            command = self._get_command_fcp(src_path, dst_path, variant_val)
        elif cp_type == 'cp':
            command = self._get_command_cp(src_path, dst_path, variant_val)
        else:
            raise Exception("Unknown cp type")
        
        t = self._run_cmd(command, dst_path)
        real_times = t['real_times']
        user_times = t['user_times']
        sys_times = t['sys_times']
        ptimes = t['ptimes']

        self._results[cp_type][self._variant_param].append(variant_val)
        
        self._results[cp_type]['time_mean'].append(statistics.mean(ptimes))
        self._results[cp_type]['time_std_dev'].append(statistics.stdev(ptimes))

        self._results[cp_type]['real_time_mean'].append(statistics.mean(real_times))
        self._results[cp_type]['real_time_std_dev'].append(statistics.stdev(real_times))

        self._results[cp_type]['user_time_mean'].append(statistics.mean(user_times))
        self._results[cp_type]['user_time_std_dev'].append(statistics.stdev(user_times))

        self._results[cp_type]['sys_time_mean'].append(statistics.mean(sys_times))
        self._results[cp_type]['sys_time_std_dev'].append(statistics.stdev(sys_times))

    def _run_workload_fcp(self, variant_val):
        self._run_workload_type(variant_val, 'fcp')
    
    def _run_workload_cp(self, variant_val):
        self._run_workload_type(variant_val, 'cp')

    def run_workloads_fcp(self):
        for val in self._variant_values:
            self._run_workload_fcp(val)
    
    def run_workloads_cp(self):
        for val in self._variant_values:
            self._run_workload_cp(val)
        
    def cleanup(self):
        for direc in self._created_dirs:
            shutil.rmtree(direc)

    def get_results(self):
        res = {
            'meta': {
                'name': self._name,
                'commit': self._get_commit(),
                'parsed_config': self._config
            },
            'data': self._results
        }
        return res

    def _get_results_path(self):
        return os.path.join(self._results_dir, f'{self._name}_results.json')

    def _get_commit(self):
        cmd = "git rev-parse HEAD"
        proc = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        ret = proc.wait()
        if ret != 0:
            raise Exception(f"Failed to get commit {proc.stderr.read().decode()}")
        commit = proc.stdout.read().decode().strip()
        debug(f"Got the commit id {commit}")
        return commit

    def dump_results(self):
        path = self._get_results_path()
        res = self.get_results()
        with open(path, 'w') as fh:
            fh.write(json.dumps(res, indent=2))
        info(f"Results at {os.path.abspath(path)}")


def get_params_info():
    params = {
        'file_size_bytes': {
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
        'buffer_size_kb': {
            'default': 128,
            'fcp_flag': '-b',
            'cp_flag': '-b'
        },
        'ring_size': {
            'default': 1024,
            'fcp_flag': '-q'
        }
    }
    return params


def get_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', dest='bin_dir', type=str, required=False, help='Override directory where binaries (cp, fcp) can be found')
    parser.add_argument('-t', dest='target_dir', type=str, required=False, help='Override directory to run tests at')
    parser.add_argument('-f', dest='config_path', type=str, required=False, default='config.json', help='Config file path')
    parser.add_argument('-k', dest='sq_poll', type=str, required=False, help='Enable sq poll')
    parser.add_argument('-r', dest='results_dir', type=str, required=False, help='Results will be created at this location')

    return parser

def get_parsed():
    parser = get_parser()
    parsed = parser.parse_args(sys.argv[1:])

    with open(parsed.config_path) as fh:
        config = json.loads(fh.read())
    params = get_params_info()

    # override the defaults
    for k, v in config.get('invariants', {}).items():
        params[k]['default'] = v
    
    if config['variant']['param'] not in params:
        raise Exception(f"Unknown variant parameter {config['variant']['param']}")

    ret = {
        'variant': {
            'param': config['variant']['param'],
            'values': config['variant']['values']
        },
        'params_info': params,
        'name': config['name'],
        'target_dir': config['target_dir'],
        'bin_dir': config['bin_dir'],
        'skip_cleanup': config.get('skip_cleanup', False),
        'run_cp': config.get('run_cp', False)
    }

    # override with cli args
    if parsed.bin_dir:
        ret['bin_dir'] = parsed.bin_dir
    if parsed.target_dir:
        ret['target_dir'] = parsed.target_dir
    if parsed.sq_poll is not None:
        ret['params_info']['sq_poll']['default'] = parsed.sq_poll
    if parsed.results_dir is not None:
        ret['results_dir'] = os.path.abspath(parsed.results_dir)
        if not os.path.exists(parsed.results_dir):
            raise Exception(f"Results dir {parsed.results_dir} does not exist")

    debug(f'Parsed the following config {json.dumps(ret, indent=2)}')
    return ret

def main():
    parsed = get_parsed()

    graph = GenericGraph(parsed)

    graph.setup_wdir()
    graph.create_required_workloads()
    graph.run_workloads_fcp()
    if parsed['run_cp']:
        graph.run_workloads_cp()

    graph.dump_results()

    if not parsed['skip_cleanup']:
        graph.cleanup()

if __name__ == '__main__':
    main()
