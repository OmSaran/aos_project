import argparse
import sys
import os
import shutil
import subprocess
import json


DEBUG = True

# hack
sys.path.append('../../../')
sys.path.append('../')

from generator import DirCreator
from base_generate import BaseFileGraph

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

class SingleFileGraph(BaseFileGraph):
    NAME = 'graph_bench_single_file_size'
    def __init__(self, target_dir, bin_dir, skip_sanity, skip_drop):
        super().__init__(target_dir, bin_dir, skip_sanity, skip_drop)
        self._sizes = None
        self._cp_results = {
            'size': [],
            'time': [],
        }
        self._fcp_results = {
            'size': [],
            'time': [],
        }

        self._init_sizes()

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

    def create_required_workloads(self):
        for size in self._sizes:
            root_path = self.get_root_path(size)
            work_gen = DirCreator(root_path)
            work_gen.create(1, 0, 1, (size, size))
        
    def cleanup(self):
        for size in self._sizes:
            shutil.rmtree(self.get_root_path(size))
            # shutil.rmtree(self._get_copy_root_path(size))

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
        
        t = self._run_cmd(command)

        self._cp_results['size'].append(size)
        self._cp_results['time'].append(t)

        shutil.rmtree(dst_path)

    def _run_workload_fcp(self, size):
        src_path = self.get_root_path(size)
        dst_path = self._get_copy_root_path(size)

        self._ensure_not_present(dst_path)
        command = self._get_command_fcp(src_path, dst_path)
        
        t = self._run_cmd(command)

        self._fcp_results['size'].append(size)
        self._fcp_results['time'].append(t)

        shutil.rmtree(dst_path)

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
