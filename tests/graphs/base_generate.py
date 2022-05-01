from asyncio import AbstractEventLoop
import os
import json
import subprocess

from abc import abstractmethod
from time import time as orig_time

ORIGINAL_CP_BIN_NAME = 'cp'
FCP_BIN_NAME = 'fcp'
DEBUG = os.environ.get('DEBUG')

#! TODO: Find way to do REAL time instead of wall clock
def time():
    return orig_time()

def debug(*args, **kwargs):
    if not DEBUG:
        return
    print(*args, **kwargs)

class BaseFileGraph:
    NAME = 'BASE'
    def __init__(self, target_dir, bin_dir, skip_sanity, skip_drop, sq_poll=False, skip_dump=False):
        self._work_dir = os.path.join(target_dir, self.NAME)
        self._skip_sanity = skip_sanity
        self._bin_dir = bin_dir
        self._skip_drop = skip_drop
        self._skip_dump = skip_dump
        self._sq_poll = sq_poll

        self._rq_sizes = None

    def _get_orig_cp_path(self):
        return os.path.join(self._bin_dir, ORIGINAL_CP_BIN_NAME)

    def _get_fcp_path(self):
        return os.path.join(self._bin_dir, FCP_BIN_NAME)

    def get_root_name(self, suffix):
        return f'rootdir_{suffix}'
    
    def _get_copy_root_name(self, suffix):
        return f'cp_rootdir_{suffix}'

    def get_root_path(self, suffix):
        return os.path.join(self._work_dir, self.get_root_name(suffix))

    def _get_copy_root_path(self, suffix):
        return os.path.join(self._work_dir, self._get_copy_root_name(suffix))

    @abstractmethod
    def create_required_workloads(self):
        raise NotImplemented

    def setup_wdir(self):
        if os.path.exists(os.path.join(self._work_dir)):
            raise Exception(f"Error, working dir {self._work_dir} directory already exists!")
        os.mkdir(self._work_dir)
        

    @abstractmethod
    def cleanup(self):
        raise NotImplemented

    def _ensure_not_present(self, path):
        if os.path.exists(path):
            raise Exception(f'Path {path} already exists')

    def drop_cache(self):
        if self._skip_drop:
            return
        with open('/proc/sys/vm/drop_caches', 'w') as fh:
            fh.write('3')

    def _get_base_meta(self) -> dict:
        ret = {
            'meta': {
                'workdir': self._work_dir,
                'sq_poll': self._sq_poll,
                'name': self.NAME
            }
        }
        return ret

    def _run_cmd(self, command):
        debug(f'Executing command {" ".join(command)}')
        self.drop_cache()
        
        t1 = time()
        proc = subprocess.Popen(command)
        ret = proc.wait()
        t2 = time()
        if(ret != 0):
            raise Exception(f'Error doing copy for command: {command}')
        
        debug(f'{t2-t1} is the time taken')

        return t2 - t1

    @abstractmethod
    def _run_workload_fcp(self, rqsize):
        raise NotImplemented

    def _get_cp_results_path(self):
        return os.path.join(self._work_dir, 'results.json')

    @abstractmethod
    def run_workloads_fcp(self):
        raise NotImplemented

    @abstractmethod
    def get_results(self):
        raise NotImplemented


    def dump_results(self):
        if self._skip_dump:
            return
        path = self._get_cp_results_path()
        res = self.get_results()
        with open(path, 'w') as fh:
            fh.write(json.dumps(res, indent=2))