import argparse
import sys
import os
import shutil
import subprocess
import json

NAME = 'graph_bench_single'
ORIGINAL_CP_BIN_NAME = 'cp'
FCP_BIN_NAME = 'fcp'

DEBUG = True

# hack
sys.path.append('../../')

from generator import DirCreator

from time import time as orig_time

def debug(*args, **kwargs):
    if DEBUG:
        print(*args, **kwargs)

#! TODO: Find way to do REAL time instead of wall clock
def time():
    return orig_time()

def get_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', dest='bin_dir', type=str, required=True, help='Directory where binaries (cp, fcp) can be found')
    parser.add_argument('-t', dest='target_dir', type=str, required=True, help='Directory to run tests at')
    parser.add_argument('--skip_cleanup', dest='skip_cleanup', default=False, action='store_true')
    parser.add_argument('--skip_sanity', dest='skip_sanity', default=False, action='store_true')
    parser.add_argument('--skip_drop', dest='skip_drop', default=False, action='store_true')

    return parser

class SingleFileGraph:
    def __init__(self, target_dir, bin_dir, skip_sanity, skip_drop):
        self._work_dir = os.path.join(target_dir, NAME)
        self._sizes = None
        self._cp_results = {
            'size': [],
            'time': [],
        }
        self._fcp_results = {
            'size': [],
            'time': [],
        }
        self._skip_sanity = skip_sanity
        self._bin_dir = bin_dir
        self._skip_drop = skip_drop

        self._init_sizes()

    def _get_orig_cp_path(self):
        return os.path.join(self._bin_dir, ORIGINAL_CP_BIN_NAME)

    def _get_fcp_path(self):
        return os.path.join(self._bin_dir, FCP_BIN_NAME)

    def _from_mul_list(self, li, mul):
        return [ele * mul for ele in li]

    def from_kb_list(self, li):
        return self._from_mul_list(li, 1024)

    def from_mb_list(self, li):
        return self._from_mul_list(li, 1024 * 1024)

    def from_gb_list(self, li):
        return self._from_mul_list(li, 1024 * 1024 * 1024)

    def _init_sizes(self):
        sizes_kb = self.from_kb_list([4, 8, 16, 20, 24, 28, 32])
        sizes_mb = self.from_mb_list([1, 4, 8, 16, 32, 64])
        # sizes_gb = self.from_gb_list([1, 2, 4])
        sizes_gb = self.from_gb_list([])

        self._sizes = sizes_kb + sizes_mb + sizes_gb

    def get_root_name(self, size):
        return f'rootdir_{size}'
    
    def _get_copy_root_name(self, size):
        return f'cp_rootdir_{size}'

    def get_root_path(self, size):
        return os.path.join(self._work_dir, self.get_root_name(size))

    def _get_copy_root_path(self, size):
        return os.path.join(self._work_dir, self._get_copy_root_name(size))

    def create_required_workloads(self):
        for size in self._sizes:
            root_path = self.get_root_path(size)
            work_gen = DirCreator(root_path)
            work_gen.create(1, 0, 1, (size, size))

    def setup_wdir(self):
        if os.path.exists(os.path.join(self._work_dir)):
            raise Exception(f"Error, working dir {self._work_dir} directory already exists!")
        os.mkdir(self._work_dir)
        

    def cleanup(self):
        for size in self._sizes:
            shutil.rmtree(self.get_root_path(size))
            # shutil.rmtree(self._get_copy_root_path(size))

    def _ensure_not_present(self, path):
        if os.path.exists(path):
            raise Exception(f'Path {path} already exists')

    def _get_command_cp(self, src_path, dst_path):
        bin_path = self._get_orig_cp_path()
        command = f'{bin_path} -r {src_path} {dst_path}'
        return command.split()

    def _get_command_fcp(self, src_path, dst_path):
        bin_path = self._get_fcp_path()
        command = f'{bin_path} -r {src_path} {dst_path}'
        return command.split()

    def _run_workload_cp(self, size):
        src_path = self.get_root_path(size)
        dst_path = self._get_copy_root_path(size)

        self._ensure_not_present(dst_path)
        command = self._get_command_cp(src_path, dst_path)
        
        t = self._run_cmd_cp(command)

        self._cp_results['size'].append(size)
        self._cp_results['time'].append(t)

        shutil.rmtree(dst_path)

    def drop_cache(self):
        if self._skip_drop:
            return
        with open('/proc/sys/vm/drop_caches', 'w') as fh:
            fh.write('3')

    def _run_cmd_cp(self, command):
        t1 = time()

        self.drop_cache()

        proc = subprocess.Popen(command)
        ret = proc.wait()
        t2 = time()
        if(ret != 0):
            raise Exception(f'Error doing copy for command: {command}')
        
        debug(f'{t2-t1} is the time taken')

        return t2 - t1

    def _run_workload_fcp(self, size):
        src_path = self.get_root_path(size)
        dst_path = self._get_copy_root_path(size)

        self._ensure_not_present(dst_path)
        command = self._get_command_fcp(src_path, dst_path)
        
        t = self._run_cmd_cp(command)

        self._fcp_results['size'].append(size)
        self._fcp_results['time'].append(t)

        shutil.rmtree(dst_path)

    def _get_cp_results_path(self):
        return os.path.join(self._work_dir, 'results.json')

    def run_workloads_fcp(self):
        for size in self._sizes:
            self._run_workload_fcp(size)

    def run_workloads_cp(self):
        for size in self._sizes:
            self._run_workload_cp(size)

    def get_results(self):
        return {
            'cp': self._cp_results,
            'fcp': self._fcp_results,
        }

    def dump_results(self):
        path = self._get_cp_results_path()
        res = self.get_results()
        with open(path, 'w') as fh:
            fh.write(json.dumps(res, indent=2))

def parse():
    parser = get_parser()
    parsed = parser.parse_args(sys.argv[1:])
    return parsed

def main():
    parsed = parse()

    graph = SingleFileGraph(target_dir=parsed.target_dir, bin_dir=parsed.bin_dir, skip_sanity=parsed.skip_sanity, skip_drop=parsed.skip_drop)
    graph.setup_wdir()
    graph.create_required_workloads()
    graph.run_workloads_cp()
    graph.run_workloads_fcp()
    graph.dump_results()

    debug(graph._cp_results)

    if not parsed.skip_cleanup:
        graph.cleanup()

if __name__ == '__main__':
    main()
