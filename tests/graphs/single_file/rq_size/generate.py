import argparse
import sys
import os
import shutil
import subprocess
import json

# NOTE: self._rq_sizes can be modified

DEFAULT_FILE_SIZE = 1 << 30 # 1GB
RQ_SIZES = [2, 4, 8, 16, 32, 64, 256, 1024, 2048, 4096, 16384, 32768]


DEBUG = True

# FIXME: hack
sys.path.append('../../../')

from generator import DirCreator
from tests.graphs.base_generate import BaseFileGraph, debug, time


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

class SingleFileRQGraph(BaseFileGraph):
    NAME = 'graph_bench_single_rq_size'
    def __init__(self, target_dir, bin_dir, skip_sanity, skip_drop, file_size, sq_poll):
        super().__init__(target_dir, bin_dir, skip_sanity, skip_drop)
        self._filesize = file_size
        self._results = {
            'rq_size': [],
            'time': []
        }
        self._skip_sanity = skip_sanity
        self._bin_dir = bin_dir
        self._skip_drop = skip_drop
        self._sq_poll = sq_poll

        self._rq_sizes = RQ_SIZES

    def create_required_workloads(self):
        root_path = self.get_root_path(self._filesize)
        work_gen = DirCreator(root_path)
        work_gen.create(1, 0, 1, (self._filesize, self._filesize))
        
    def cleanup(self):
        shutil.rmtree(self.get_root_path(self._filesize))

    def _get_command_fcp(self, src_path, dst_path, rqsize):
        bin_path = self._get_fcp_path()
        command = f'{bin_path} -r {src_path} {dst_path} -q {rqsize}'
        if self._sq_poll:
            command += ' -k'
        return command.split()

    def _run_workload_fcp(self, rqsize):
        src_path = self.get_root_path(self._filesize)
        dst_path = self._get_copy_root_path(self._filesize)

        self._ensure_not_present(dst_path)
        command = self._get_command_fcp(src_path, dst_path, rqsize)
        
        t = self._run_cmd(command)

        self._results['rq_size'].append(rqsize)
        self._results['time'].append(t)

        shutil.rmtree(dst_path)

    def run_workloads_fcp(self):
        for size in self._rq_sizes:
            self._run_workload_fcp(size)

    def get_results(self):
        return {
            'meta': {
                'filesize': self._filesize
            },
            'data': {
                'rq_size': self._results['rq_size'],
                'time': self._results['time']
            }
        }

def parse():
    parser = get_parser()
    parsed = parser.parse_args(sys.argv[1:])
    return parsed

def main():
    parsed = parse()

    graph = SingleFileRQGraph(target_dir=parsed.target_dir, bin_dir=parsed.bin_dir, skip_sanity=parsed.skip_sanity, skip_drop=parsed.skip_drop, 
        file_size=parsed.file_size, sq_poll=parsed.sq_poll)
    graph.setup_wdir()
    graph.create_required_workloads()
    graph.run_workloads_fcp()
    graph.dump_results()

    if not parsed.skip_cleanup:
        graph.cleanup()

if __name__ == '__main__':
    main()
