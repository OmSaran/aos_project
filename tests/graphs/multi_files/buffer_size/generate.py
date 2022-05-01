import argparse
import sys
import os
import shutil
import subprocess
import json

# NOTE: self._rq_sizes can be modified

DEFAULT_NUM_FILES = 100
DEFAULT_FILE_SIZE = 1024 * 1024
BUFFER_SIZES = [4096, 8192]


DEBUG = True

# FIXME: hack
sys.path.append('../../../../')

from tests.generator import DirCreator
from tests.graphs.base_generate import BaseFileGraph, time, debug


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

class MultiFileBufferSizeGraph(BaseFileGraph):
    NAME = 'graph_bench_multi_buffer_size'
    def __init__(self, target_dir, bin_dir, skip_sanity, skip_drop, num_files, file_size, sq_poll):
        super().__init__(target_dir, bin_dir, skip_sanity, skip_drop, sq_poll=sq_poll)
        self._numfiles = num_files
        self._results = {
            'fcp': {
                'buffer_size': [],
                'time': []
            },
            'cp': {
                'buffer_size': [],
                'time': []
            }
        }
        self._skip_sanity = skip_sanity
        self._bin_dir = bin_dir
        self._skip_drop = skip_drop

        self._buffer_sizes = BUFFER_SIZES
        self._num_files = num_files
        self._file_size = file_size

        self._created_directories = []

    def create_required_workloads(self):
        for buffer_size in self._buffer_sizes:
            root_path = self.get_root_path(buffer_size)
            self._created_directories.append(root_path)
            work_gen = DirCreator(root_path)
            work_gen.create(1, 0, self._num_files, (self._file_size, self._file_size))
        
    def cleanup(self):
        for direc in self._created_directories:
            shutil.rmtree(direc)

    def _get_command_fcp(self, src_path, dst_path, buffer_size):
        bin_path = self._get_fcp_path()
        command = f'{bin_path} -r {src_path} {dst_path} -b {buffer_size}'
        if self._sq_poll:
            command += ' -k'
        return command.split()

    def _get_command_cp(self, src_path, dst_path):
        bin_path = self._get_orig_cp_path()
        command = f'{bin_path} -r {src_path} {dst_path}'
        return command.split()

    def _run_workload_fcp(self, file_size):
        src_path = self.get_root_path(file_size)
        dst_path = self._get_copy_root_path(file_size)

        self._ensure_not_present(dst_path)
        command = self._get_command_fcp(src_path, dst_path)
        
        t = self._run_cmd(command)

        self._results['fcp']['file_size'].append(file_size)
        self._results['fcp']['time'].append(t)

        shutil.rmtree(dst_path)

    def _run_workload_cp(self, file_size):
        src_path = self.get_root_path(file_size)
        dst_path = self._get_copy_root_path(file_size)

        self._ensure_not_present(dst_path)
        command = self._get_command_cp(src_path, dst_path)
        
        t = self._run_cmd(command)

        self._results['cp']['file_size'].append(file_size)
        self._results['cp']['time'].append(t)

        shutil.rmtree(dst_path)

    def run_workloads_fcp(self):
        for file_size in self._file_sizes:
            self._run_workload_fcp(file_size)
    
    def run_workloads_cp(self):
        for file_size in self._file_sizes:
            self._run_workload_cp(file_size)

    def get_results(self):
        meta = self._get_base_meta()
        ret = {
            'meta': {
                'num_files': self._num_files,
            },
            'data': {
                'cp': {
                    'file_size': self._results['cp']['file_size'],
                    'time': self._results['cp']['time']
                },
                'fcp': {
                    'file_size': self._results['fcp']['file_size'],
                    'time': self._results['fcp']['time']
                }
            }
        }
        meta.update(ret)
        return meta

def parse():
    parser = get_parser()
    parsed = parser.parse_args(sys.argv[1:])
    return parsed

def main():
    parsed = parse()

    graph = MultiFileFileSizeGraph(target_dir=parsed.target_dir, bin_dir=parsed.bin_dir, skip_sanity=parsed.skip_sanity, skip_drop=parsed.skip_drop, 
        num_files=parsed.num_files, sq_poll=parsed.sq_poll)
    graph.setup_wdir()
    graph.create_required_workloads()
    graph.run_workloads_fcp()
    graph.run_workloads_cp()
    graph.dump_results()

    if not parsed.skip_cleanup:
        graph.cleanup()

if __name__ == '__main__':
    main()
