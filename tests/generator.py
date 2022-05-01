#!/usr/bin/python3

import argparse
import os
import sys

from random import randint

CONFIG_DEBUG = 1

def debug(*args, **kwargs):
    if CONFIG_DEBUG:
        print(*args, **kwargs)

def check_depth(value):
    ivalue = int(value)
    if ivalue < 1:
        raise argparse.ArgumentTypeError("Invalid depth value. Should be at least 1")
    return ivalue

def get_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument('-d', '--depth', required=True, type=check_depth, help='Depth of the directory structure', dest='depth')
    parser.add_argument('-b', '--breadth', required=True, help='Number of directories per directory (except leaves)', type=int, dest='breadth')
    parser.add_argument('-n', '--num_files', required=True, help='Number of files per directory', type=int, dest='num_files')
    parser.add_argument('-l', '--file_size_min', help='Minimum file size in bytes', type=int, dest='file_size_min')
    parser.add_argument('-m', '--file_size_max', help='Maximum file size in bytes', type=int, dest='file_size_max')

    return parser

class DirCreator:
    def __init__(self, root_dir_path='rootdir'):
        self._count = 0
        self._dir_prefix = 'dir'
        self._file_prefix = 'file'
        self._root_dir_path = root_dir_path

    def _create_file(self, file_path: str, file_size_range: (int, int)):
        with open(file_path, 'wb') as fh:
            randsize = randint(file_size_range[0], file_size_range[1])
            debug(f'Creating random file {file_path} of size {randsize}')
            fh.write(os.urandom(randsize))

    def _create_dir_and_files(self, dir_path, num_dirs, num_files, file_size_range, skip_dirs):
        skip_dirs = False
        dirs = []
        for i in range(num_files):
            file_path = os.path.join(dir_path, f'{self._file_prefix}{i}')
            self._create_file(file_path, file_size_range)
        if skip_dirs:
            return dirs
        for i in range(num_dirs):
            nested_dir_path = os.path.join(dir_path, f'{self._dir_prefix}{i}')
            os.mkdir(nested_dir_path)
            dirs.append(nested_dir_path)
        return dirs

    def create(self, depth, breadth, num_files, file_size_range):
        os.mkdir(self._root_dir_path)
        skip_dirs = False
        if depth == 1:
            skip_dirs = True
        dirs = self._create_dir_and_files(self._root_dir_path, breadth, num_files, file_size_range, skip_dirs)
        depth -= 1
        while depth > 0:
            new_dirs = []
            for directory in dirs:
                res = self._create_dir_and_files(directory, breadth, num_files, file_size_range, skip_dirs)
                new_dirs.extend(res)
            depth -= 1
            if depth == 1:
                skip_dirs = True
            dirs = new_dirs

def main():
    parser = get_parser()
    parsed = parser.parse_args(sys.argv[1:])
    if parsed.num_files:
        if not parsed.file_size_max or not parsed.file_size_min:
            parser.error('--num_files requires --file_size_min and --file_size_max')

    debug('Following are the arguments:')
    debug(f'Depth = {parsed.depth}. Breadth = {parsed.breadth}. Num Files = {parsed.num_files}')

    dir_creator = DirCreator()
    dir_creator.create(parsed.depth, parsed.breadth, parsed.num_files, (parsed.file_size_min, parsed.file_size_max))


if __name__ == '__main__':
    main()
