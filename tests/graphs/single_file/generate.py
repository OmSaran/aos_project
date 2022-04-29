import argparse
import sys
import os
import shutil

NAME = 'graph_bench_single'

# hack
sys.path.append('../../')

from generator import DirCreator

from time import time as orig_time

#! TODO: Find way to do REAL time instead of wall clock
def time():
    return orig_time()

def get_parser():
    parser = argparse.ArgumentParser()
    # parser.add_argument('--bin', dest='bin_dir', type=str, required=True, help='Directory where bin can be found')
    parser.add_argument('-t', dest='target_dir', type=str, required=True, help='Directory to run tests at')

    return parser

class SingleFileGraph:
    def __init__(self, target_dir):
        self.target_dir = target_dir
        self.sizes = None

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

        self.sizes = sizes_kb + sizes_mb + sizes_gb

    def get_root_name(self, size):
        return f'rootdir_{size}'

    def create_required_workloads(self):
        for size in self.sizes:
            work_gen = DirCreator(self.get_root_name(size))
            work_gen.create(1, 0, 1, (size, size))

    def setup_wdir(self):
        if os.path.exists(os.path.join(self.target_dir, NAME)):
            print("Error, directory already exists!")

    def cleanup(self):
        for size in self.sizes:
            shutil.rmtree(self.get_root_name(size))


def parse():
    parser = get_parser()
    parsed = parser.parse_args(sys.argv[1:])
    return parsed

def main():
    parsed = parse()
    graph = SingleFileGraph(target_dir=parsed.target_dir)
    graph.create_required_workloads()
    graph.cleanup()

if __name__ == '__main__':
    main()
