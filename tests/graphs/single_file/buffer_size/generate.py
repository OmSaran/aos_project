import argparse
import sys
import os
import shutil
import subprocess
import json

DEFAULT_FILE_SIZE = 1 << 30 # 1GB
BUFFER_SIZES = [4096, 8192, 16384]


DEBUG = True

# FIXME: hack
sys.path.append('../../../')

from generator import DirCreator
from tests.graphs.base_generate import BaseFileGraph, debug

def get_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bin', dest='bin_dir', type=str, required=True, help='Directory where binaries (cp, fcp) can be found')
    parser.add_argument('-t', dest='target_dir', type=str, required=True, help='Directory to run tests at')
    parser.add_argument('-s', dest='file_size', type=int, required=False, default=DEFAULT_FILE_SIZE, help="The file size to perform experiments on")
    parser.add_argument('-k', '--sq-poll', dest='sq_poll', type=bool, required=False, action='store_true', help='Enable sq poll')

    parser.add_argument('--skip_cleanup', dest='skip_cleanup', default=False, action='store_true')
    parser.add_argument('--skip_sanity', dest='skip_sanity', default=False, action='store_true')
    parser.add_argument('--skip_drop', dest='skip_drop', default=False, action='store_true')

    return parser

class SingleFileBufferGraph(BaseFileGraph):
    NAME = 'graph_bench_single_buffer_size'
    def __init__(self, target_dir, bin_dir, skip_sanity, skip_drop, file_size, sq_poll):
        super().__init__(target_dir, bin_dir, skip_sanity, skip_drop)
        self._filesize = file_size
        self._results = {
            'cp': {
                'buffer_size': [],
                'time': []
            },
            'fcp': {
                'buffer_size': [],
                'time': []
            }
        }
        self._skip_sanity = skip_sanity
        self._bin_dir = bin_dir
        self._skip_drop = skip_drop
        self._sq_poll = sq_poll

        self._buffer_sizes = BUFFER_SIZES

    def create_required_workloads(self):
        root_path = self.get_root_path(self._filesize)
        work_gen = DirCreator(root_path)
        work_gen.create(1, 0, 1, (self._filesize, self._filesize))
        
    def cleanup(self):
        shutil.rmtree(self.get_root_path(self._filesize))

    def _get_command_fcp(self, src_path, dst_path, buffer_size):
        bin_path = self._get_fcp_path()
        command = f'{bin_path} -r {src_path} {dst_path} -b {buffer_size}'
        if self._sq_poll:
            command += ' -k'
        return command.split()
    
    def _get_command_cp(self, src_path, dst_path, buffer_size):
        bin_path = self._get_orig_cp_path()
        command = f'{bin_path} -r {src_path} {dst_path} -b {buffer_size}'
        return command.split()

    def _run_workload_fcp(self, buffer_size):
        src_path = self.get_root_path(self._filesize)
        dst_path = self._get_copy_root_path(self._filesize)

        self._ensure_not_present(dst_path)
        command = self._get_command_fcp(src_path, dst_path, buffer_size)
        
        t = self._run_cmd(command)

        self._results['fcp']['buffer_size'].append(buffer_size)
        self._results['fcp']['time'].append(t)

        shutil.rmtree(dst_path)

    def _run_workload_cp(self, buffer_size):
        src_path = self.get_root_path(self._filesize)
        dst_path = self._get_copy_root_path(self._filesize)

        self._ensure_not_present(dst_path)
        command = self._get_command_cp(src_path, dst_path, buffer_size)
        
        t = self._run_cmd(command)

        self._results['cp']['buffer_size'].append(buffer_size)
        self._results['cp']['time'].append(t)

        shutil.rmtree(dst_path)

    def run_workloads_fcp(self):
        for size in self._buffer_sizes:
            self._run_workload_fcp(size)

    def run_workloads_cp(self):
        for size in self._buffer_sizes:
            self._run_workload_cp(size)

    def get_results(self):
        return {
            'meta': {
                'filesize': self._filesize
            },
            'data': {
                'cp' :{
                    'buffer_size': self._results['cp']['buffer_size'],
                    'time': self._results['cp']['time']
                },
                'fcp': {
                    'buffer_size': self._results['fcp']['buffer_size'],
                    'time': self._results['fcp']['time']
                }
            }
        }

def parse():
    parser = get_parser()
    parsed = parser.parse_args(sys.argv[1:])
    return parsed

def main():
    parsed = parse()

    graph = SingleFileBufferGraph(target_dir=parsed.target_dir, bin_dir=parsed.bin_dir, skip_sanity=parsed.skip_sanity, skip_drop=parsed.skip_drop, 
        file_size=parsed.file_size, sq_poll=parsed.sq_poll)
    graph.setup_wdir()
    graph.create_required_workloads()
    graph.run_workloads_cp()
    graph.run_workloads_fcp()
    graph.dump_results()

    if not parsed.skip_cleanup:
        graph.cleanup()

if __name__ == '__main__':
    main()
