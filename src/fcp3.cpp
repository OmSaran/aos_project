//! WARNING: Not thread safe
#include <iostream>
#include <vector>
#include <queue>
#include <array>
#include <filesystem>

#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <liburing.h>

#include "cxxopts.hpp"

#define RING_SIZE 1024
#define DIR_BUF_SIZE 65536
#define RW_BUF_SIZE 128 * 1024
#define BILLION 1000000000L
#define MIN(a, b) ((a < b) ? a : b)
#define MEASURE


struct timespec total_time;
struct timespec setup_time;
struct timespec src_open_time;
struct timespec dst_open_time;
struct timespec stat_time;
struct timespec data_copy_time;
struct timespec close_times;
struct timespec getd_compute;
struct timespec getd_time;
struct timespec mkdir_time;
struct timespec iou_mkdir_time;

struct io_uring ring;

struct cp_options
{
    size_t batchsize = 20;
};

using namespace std;

#define IORING_OP_GETDENTS64 41
static inline void io_uring_prep_getdents64(struct io_uring_sqe *sqe, int fd,
					    void *buf, unsigned int count, long offset)
{
	io_uring_prep_rw(IORING_OP_GETDENTS64, sqe, fd, buf, count, offset);
}

void get_time(struct timespec *tp);
struct timespec get_diff(struct timespec *start, struct timespec *end);
void update_ts(struct timespec *target, struct timespec *diff);


struct linux_dirent64 {
	int64_t		d_ino;    /* 64-bit inode number */
	int64_t		d_off;    /* 64-bit offset to next structure */
	unsigned short	d_reclen; /* Size of this dirent */
	unsigned char	d_type;   /* File type */
	char		d_name[]; /* Filename (null-terminated) */
};

class IORing {
private:
    struct io_uring ring;
    int size;
    int cqe_pending_tasks;
    int max_open_files;
public:
    IORing(ssize_t size) {
        this->size = size;
        this->cqe_pending_tasks = 0;
        this->max_open_files = 1024;
        int ret = io_uring_queue_init(size, &ring, 0);
        assert(ret == 0);
    }

    int ring_size() {
        return size;
    }

    int num_cqe_pending_tasks() {
        return cqe_pending_tasks;
    }

    io_uring_sqe* get_sqe() {
        struct io_uring_sqe *sqe;
        sqe = io_uring_get_sqe(&ring);
        assert(sqe != NULL);
        return sqe;
    }

    // int submit(bool skip) {
    //     io_uring_submit(&ring);
    //     if(!skip)
    //         cqe_pending_tasks += 1;
    // }

    int space_left() {
        return io_uring_sq_space_left(&ring);
    }

    void submit_and_wait(io_uring_cqe **cqe) {
        int ret;

        io_uring_submit_and_wait(&ring, 1);
        ret = io_uring_wait_cqe(&ring, cqe);

        cqe_pending_tasks += 1;

        assert(ret == 0);
    }

    void see_cqe(io_uring_cqe *cqe) {
        io_uring_cqe_seen(&ring, cqe);

        cqe_pending_tasks -= 1;
    }

    void see_num_cqes(int num) {
        io_uring_cq_advance(&ring, num);
        cqe_pending_tasks -= num;
    }

    void submit_and_wait_nr(int nr, int num_skipped) {
        int ret = io_uring_submit_and_wait(&ring, nr-num_skipped);
        assert(ret == nr);
        cqe_pending_tasks += (nr-num_skipped);
    }

    void get_cqe(io_uring_cqe **cqe) {
        int ret;

        ret = io_uring_wait_cqe(&ring, cqe);
        assert(ret == 0);
    }
};

class CopyJobInfo {
public:
    filesystem::path src;
    filesystem::path dst;
    int src_fd;
    int dst_fd;
    struct statx st_buf;

    CopyJobInfo(filesystem::path src_path, filesystem::path dst_path) 
        : src{src_path}, dst{dst_path}
    {}
};

class DirJob {
private:
    vector<uint8_t> dirent_buf;
    filesystem::path dir_path;
    filesystem::path dst_dir_path;
    bool is_opened;
    bool dir_fetched;
    bool dst_created;
    int dir_fd;
    int dst_dir_fd;
    int size;
    int buf_size;
    size_t dir_buf_size;
    IORing& ring;
    size_t batch_size;

    void open_dir() {
        if(is_opened)
            assert(0);
        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;
        sqe = ring.get_sqe();
#ifdef FD_DIRECT
#else
        io_uring_prep_openat(sqe, -1, dir_path.c_str(), O_DIRECTORY, 0);
        ring.submit_and_wait(&cqe);
        if(cqe->res < 0) {
            cerr << "Failed to open directory: " << dir_path.c_str() << " : " << strerror(-cqe->res) << endl;
            exit(1);
        }
        dir_fd = cqe->res;
        ring.see_cqe(cqe);
#endif
    }

public:
    DirJob(filesystem::path src_path, filesystem::path dst_path, IORing& ioring, int bufsize, size_t batch_size)
        : dir_path{src_path}, dst_dir_path{dst_path}, ring{ioring}, buf_size{bufsize}, batch_size{batch_size}
    {
        this->dir_buf_size = DIR_BUF_SIZE; 
        this->dirent_buf.resize(this->dir_buf_size);
        is_opened = false;
        dir_fetched = false;
    }

    void fetch_dir() {
        if(!is_opened)
            open_dir();

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;
        int res;
        int offset = 0;

        sqe = ring.get_sqe();

        assert(ring.num_cqe_pending_tasks() == 0);
        io_uring_prep_getdents64(sqe, dir_fd, (void *)&dirent_buf[0], this->dir_buf_size, offset);
        ring.submit_and_wait(&cqe);
        res = cqe->res;
        // cout << "got " << res << " bytes in getdents" << endl;
        assert(res >= 0);
        ring.see_cqe(cqe);
        dirent_buf.resize(res);
        
        while(res != 0) {
            dirent_buf.resize(dirent_buf.size() + this->dir_buf_size);

            sqe = ring.get_sqe();

            offset = dirent_buf.size()-this->dir_buf_size;

            io_uring_prep_getdents64(sqe, dir_fd, (void *)&dirent_buf[offset], this->dir_buf_size, offset);
            ring.submit_and_wait(&cqe);
            res = cqe->res;
            // cout << "got " << res << " more bytes in getdents for offset " << offset << endl;
            assert(res >= 0);
            ring.see_cqe(cqe);
            dirent_buf.resize(dirent_buf.size() - (this->dir_buf_size - res));
        }
    }

    void do_stat(vector<CopyJobInfo>& copy_info, int start, int end) {
        // batch all stats
        int num = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(int i=start; i<end; i++) {
            CopyJobInfo& info = copy_info[i];

            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            io_uring_prep_statx(sqe, dir_fd, info.src.filename().c_str(), 0, STATX_SIZE, &info.st_buf);
            // sqe->flags |= IOSQE_ASYNC;
            // if(count != end-1) {
            //     sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
            // }
        }
        ring.submit_and_wait_nr(end-start, 0);
        ring.see_num_cqes(end-start);
    }

    void do_src_opens(vector<CopyJobInfo>& copy_info, int start, int end) {
        // src file opens
        int num = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(int i=start; i<end; i++) {
            CopyJobInfo& info = copy_info[i];

            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            //! TODO: Add fadvise
            // cout<< "Opening source directory: " << info.src << endl;
            io_uring_prep_openat(sqe, dir_fd, info.src.filename().c_str(), O_RDONLY, 0);
            // sqe->flags |= IOSQE_ASYNC;
            io_uring_sqe_set_data64(sqe, i);
        }
        ring.submit_and_wait_nr(end-start, 0);

        for(int i=start; i<end; i++) {
            ring.get_cqe(&cqe);
            long index = (long)cqe->user_data;
            // cout<< "index = " << index << endl;
            assert(cqe->res >= 0);
            // cout<< "Opened source directory: " << copy_info[index].dst << "; with result = " << cqe->res << endl;
            copy_info[index].src_fd = cqe->res;
            ring.see_cqe(cqe);
        }
    }

    void do_dst_opens(vector<CopyJobInfo>& copy_info, int start, int end) {
        // src file opens
        int num = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(int i=start; i<end; i++) {
            CopyJobInfo& info = copy_info[i];

            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            //! TODO: Add fadvise
            // cout<< "Opening destination directory: " << info.dst << endl;
            io_uring_prep_openat(sqe, dst_dir_fd, info.dst.filename().c_str(), O_CREAT | O_WRONLY, 0777);
            // sqe->flags |= IOSQE_ASYNC;
            io_uring_sqe_set_data64(sqe, i);
        }
        ring.submit_and_wait_nr(end-start, 0);

        for(int i=start; i<end; i++) {
            ring.get_cqe(&cqe);
            long index = (long)cqe->user_data;
            // cout<< "index = " << index << endl;
            if(cqe->res < 0) {
                cerr << "failed to do open: " << strerror(-cqe->res) << endl;
                exit(1);
            }
            assert(cqe->res >= 0);
            // cout<< "Opened destination directory: " << copy_info[index].dst << "; with result = " << cqe->res << endl;
            copy_info[index].dst_fd = cqe->res;
            ring.see_cqe(cqe);
        }
    }

    void do_data_copy_serial_linked(vector<CopyJobInfo>& copy_info, int start, int end) {
        void *buf = malloc(buf_size);

        int num = 0;
        int idx = 0;
        int ret;

        bool writes_done = false;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(int i=start; i<end; i++) {
            size_t size = copy_info[i].st_buf.stx_size;
            size_t remaining = size;

            int src_fd = copy_info[i].src_fd;
            int dst_fd = copy_info[i].dst_fd;

            // cout<< "Copying the file: " << copy_info[i].src << "; size = " << copy_info[i].st_buf.stx_size << "; src_fd = " << src_fd << "; dst_fd = " << dst_fd << endl;

            while(remaining > 0) {
                cout << "SOMETHING TO COPY!" << endl;
                writes_done = true;
                int rw_size = MIN(remaining, buf_size);

                sqe = ring.get_sqe();
                io_uring_prep_read(sqe, src_fd, buf, rw_size, size-remaining);
                sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
                sqe->flags |= IOSQE_IO_LINK;
                // sqe->flags |= IOSQE_ASYNC;
                
                sqe = ring.get_sqe();
                io_uring_prep_write(sqe, dst_fd, buf, rw_size, size-remaining);
                sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
                sqe->flags |= IOSQE_IO_LINK;
                io_uring_sqe_set_data64(sqe, 25);
                if(remaining - rw_size == 0) {
                    // sqe->flags &= ~IOSQE_IO_LINK;
                }
                // sqe->flags |= IOSQE_ASYNC;

                num += 2;
                // If it is the last operation don't skip success -- we need to know when the last operation is completed.
                if(i == (end-1) && (remaining - rw_size) == 0) {
                    // cout<< "Last op in copying files" << endl;
                    sqe->flags &= ~(IOSQE_CQE_SKIP_SUCCESS);
                    sqe->flags &= ~(IOSQE_IO_LINK);
                }
                else if (ring.ring_size() - num <= 2) {
                    // // cout<< "Running out of space! " << endl;
                    sqe->flags &= ~(IOSQE_CQE_SKIP_SUCCESS);
                    sqe->flags &= ~(IOSQE_IO_LINK);

                    int num_submitted = num;
                    int num_skipped = num-1;

                    ring.submit_and_wait_nr(num_submitted, num_skipped);
                    ring.get_cqe(&cqe);
                    if(cqe->res < 0) {
                        cerr << "Failed in rw: " << strerror(cqe->res) << endl;
                        exit(1);
                    }

                    for(int j=0; j<num_submitted-num_skipped; j++) {
                        ring.see_cqe(cqe);
                    }

                    // // cout<< "ring.space_left() after clearing " <<  ring.space_left() << endl;
                    num = 0;
                } else {
                    // // cout<< "ring.space_left() " <<  ring.space_left() << endl;
                }

                remaining -= rw_size;
            }
        }
        if(!writes_done) return;

        ring.submit_and_wait(&cqe);
        if(cqe->res < 0) {
            cerr << "Failed in rw after last op: " << strerror(cqe->res) << endl;
            exit(1);
        }
        assert(cqe->res >= 0);
        ring.see_cqe(cqe);
    }

    void do_close_files(vector<CopyJobInfo>& copy_info, int start, int end) {
        // src file opens
        int num = 0;
        long idx = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(int i=start; i<end; i++) {
            CopyJobInfo& info = copy_info[i];

            sqe = ring.get_sqe();
            io_uring_prep_close(sqe, info.dst_fd);
            // sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;

            sqe = ring.get_sqe();
            io_uring_prep_close(sqe, info.src_fd);
            // if(i != copy_info.size()-1)
            //     sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
        }
        ring.submit_and_wait_nr((end-start)*2, 0);
        ring.see_num_cqes((end-start)*2);
    }

    //! WARNING: Assumes there are no other tasks running in ring
    void do_copy_files(vector<CopyJobInfo>& copy_info, int start, int end) {
        if(copy_info.size() == 0) return;
        struct timespec tp1, tp2;
        struct timespec diff;

        // cout<< "Doing stat: " << endl;
        get_time(&tp1);
        do_stat(copy_info, start, end);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&stat_time, &diff);

        // cout<< "Doing src opens: " << endl;
        get_time(&tp1);
        do_src_opens(copy_info, start, end);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&src_open_time, &diff);


        // cout<< "Doing dst opens: " << endl;
        get_time(&tp1);
        do_dst_opens(copy_info, start, end);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&dst_open_time, &diff);

        // cout<< "Doing copies: " << endl;
        get_time(&tp1);
        do_data_copy_serial_linked(copy_info, start, end);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&data_copy_time, &diff);

        get_time(&tp1);
        do_close_files(copy_info, start, end);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&close_times, &diff);
    }

    void do_create_dirs(vector<CopyJobInfo>& copy_info, int start, int end) {
        // cout<< "Doing create dest dirs: " << endl;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        if(copy_info.size() == 0) return;

        for(int i=start; i<end; i++) {
            CopyJobInfo& info = copy_info[i];

            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            io_uring_prep_mkdirat(sqe, -1, info.dst.c_str(), 0777);
            io_uring_sqe_set_data64(sqe, 240);
            // if(i != end-1){
            //     sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
            // }
        }
        ring.submit_and_wait_nr(end-start, 0);
        // ring.get_cqe(&cqe);
        // if(cqe->res < 0)
        //     cerr << "Failed to create iour dir: " << strerror(-cqe->res) << "; data = " << cqe->user_data << endl;
        ring.see_num_cqes(end-start);

        // ring.get_cqe(&cqe);
        // if(cqe->res < 0) {
        //     cerr << "Failed to create dir: " << strerror(-cqe->res) << " data = " << (int)cqe->user_data << endl;
        //     exit(1);
        // }
        // ring.see_cqe(cqe);
    }

    void create_dest_dir() {
        mkdir(dst_dir_path.c_str(), 0777);
    }

    vector<array<filesystem::path, 2>> copy_dir_files(bool dest_created) {
        // constraints and requirements per op
        int max_fds = 1000;
        int max_submissions = this->ring.ring_size();
        int fd_per_cp_file = 2;
        struct timespec tgd1, tgd2, tgddiff;


        //sync
        if(!dest_created)
            create_dest_dir();

        get_time(&tgd1);
        if(!dir_fetched)
            //sync
            fetch_dir();
        get_time(&tgd2);
        tgddiff = get_diff(&tgd1, &tgd2);
        update_ts(&getd_time, &tgddiff);

        uint8_t *bufp;
        uint8_t *end;

        filesystem::path src_path, dst_path, dst_dir;

        bufp = (uint8_t *)&dirent_buf[0];
        end = bufp + dirent_buf.size();

        vector<array<filesystem::path, 2>> directories;
        vector<CopyJobInfo> copy_files;
        vector<CopyJobInfo> copy_dirs;

        struct timespec t1;
        struct timespec t2;
        struct timespec diff;

        get_time(&t1);
        while (bufp < end) {
            struct linux_dirent64 *dent;

            dent = (struct linux_dirent64 *)bufp;
            if (strcmp(dent->d_name, ".") && strcmp(dent->d_name, "..")) {
                // Create copy jobs;
                src_path = dir_path / dent->d_name;
                dst_path = this->dst_dir_path / dent->d_name;
                // cout<< "Read the following dentry: " << src_path << endl;
                if(dent->d_type == DT_REG) {
                    // copy the files
                    CopyJobInfo info(src_path, dst_path);
                    copy_files.push_back(info);
                } 
                else if (dent->d_type == DT_DIR) {
                    // create the directories
                    CopyJobInfo info(src_path, dst_path);
                    copy_dirs.push_back(info);
                    directories.push_back({src_path, dst_path});
                }
            }
            bufp += dent->d_reclen;
        }
        get_time(&t2);
        diff = get_diff(&t1, &t2);
        update_ts(&getd_compute, &diff);

        dst_dir_fd = open(dst_dir_path.c_str(), O_DIRECTORY);


        // cout << "Num dirs: " << copy_dirs.size() << endl;
        int cp_file_batch_size = this->batch_size;
        int create_dir_batch_size = 1;

        int batch_size = cp_file_batch_size;
        int start_idx = 0;
        int end_idx = copy_files.size();
        int cur_start_idx = start_idx;
        int cur_end_idx = cur_start_idx + batch_size;

        while(cur_start_idx <= end_idx) {
            cur_end_idx = MIN(cur_start_idx + batch_size, end_idx);
            do_copy_files(copy_files, cur_start_idx, cur_end_idx);
            cur_start_idx += batch_size;
        }

        batch_size = create_dir_batch_size;
        start_idx = 0;
        end_idx = copy_dirs.size();
        cur_start_idx = start_idx;
        cur_end_idx = cur_start_idx + batch_size;

        // struct timespec t1;
        // struct timespec t2;
        // struct timespec diff;

        while(cur_start_idx <= end_idx) {
            cur_end_idx = MIN(cur_start_idx + batch_size, end_idx);

            get_time(&t1);
            do_create_dirs(copy_dirs, cur_start_idx, cur_end_idx);
            get_time(&t2);
            diff = get_diff(&t1, &t2);
            update_ts(&iou_mkdir_time, &diff);
            cur_start_idx += batch_size;
        }

        // do_copy_files(copy_files);
        // do_create_dirs(copy_dirs);

        close(dir_fd);

        return directories;
    }
};

void get_time(struct timespec *tp) {
    int ret;
    int sec;
    long nsec;
    ret = clock_gettime(CLOCK_MONOTONIC, tp); 
    tp->tv_sec += tp->tv_nsec / BILLION;
    tp->tv_nsec = tp->tv_nsec % BILLION;
    assert(ret == 0);
}

#ifndef MEASURE
struct timespec get_diff(struct timespec *start, struct timespec *end){return *start;}
void update_ts(struct timespec *target, struct timespec *diff){}
void print_metric(struct timespec *ts, string metric) {}
void print_all_metrics() {}
#else

struct timespec get_diff(struct timespec *start, struct timespec *end) {
    struct timespec sp;
    sp.tv_nsec = end->tv_nsec - start->tv_nsec;
    sp.tv_sec = end->tv_sec - start->tv_sec;

    if(sp.tv_nsec < 0) {
        sp.tv_sec -= 1;
        sp.tv_nsec += BILLION;
    }

    return sp;
}

void update_ts(struct timespec *target, struct timespec *diff) {
    target->tv_nsec += diff->tv_nsec;
    target->tv_sec += diff->tv_sec;
}

void print_metric(struct timespec *ts, string metric) {
    // cout << "Adding this to seconds: " << (uint64_t)ts->tv_nsec/BILLION << endl;
    long seconds = ts->tv_sec + ts->tv_nsec/BILLION;
    long ns = ts->tv_nsec % BILLION;
    cout << metric << "\t\t: " << seconds << " seconds and " << (double)ns/1000.0 << " microseconds" << endl;
}

void print_all_metrics() {
    print_metric(&setup_time, "Setup");
    print_metric(&src_open_time, "Opening source files");
    print_metric(&stat_time, "Stat Time");
    print_metric(&dst_open_time, "Destination files open");
    print_metric(&data_copy_time, "Data Copy Time");
    print_metric(&close_times, "Close Time");
    print_metric(&getd_compute, "Getdents compute time");
    print_metric(&mkdir_time, "Mkdir time");
    print_metric(&iou_mkdir_time, "IOU Mkdir time");
    print_metric(&getd_time, "Getdents IO time");
    cout << "---------------------------------------------------------------------" << endl;
    print_metric(&total_time, "Total time");
}
#endif

int main(int argc, char**argv) {
    struct timespec tt1, tt2, ttdiff;
    struct timespec tp1, tp2;

    cxxopts::Options options("cp", "barebones cp");
    options.allow_unrecognised_options();
    options.add_options()
    ("b,batchsize", "copy batch size", cxxopts::value<size_t>())
    ("h,help", "Print usage");

    auto result = options.parse(argc, argv);
    if (result.count("help"))
    {
      std::cout << options.help() << std::endl;
      exit(0);
    }

    cp_options cp_ops;
    if (result.count("batchsize"))
    {
        cp_ops.batchsize = result["batchsize"].as<size_t>();
    }

    cout << "batchsize = " << cp_ops.batchsize << endl;

    const std::vector<std::string>& args = result.unmatched();

    get_time(&tt1);

    get_time(&tp1);
    IORing ring(RING_SIZE);
    get_time(&tp2);

    struct timespec diff = get_diff(&tp1, &tp2);

    update_ts(&setup_time, &diff);


    queue<array<filesystem::path, 2>> q;
    filesystem::path src_path(args[0]);
    filesystem::path dst_path(args[1]);
    mkdir(dst_path.c_str(), 0777);

    cout << "src_path = " << src_path << endl;
    cout << "dst_path = " << dst_path << endl;

    vector<array<filesystem::path, 2>> dirs;
    array<filesystem::path, 2> dir_val;
    // vector<array<filesystem::path, 2>> new_dirs;

    DirJob job(src_path, dst_path, ring, DIR_BUF_SIZE, cp_ops.batchsize);
    dirs = job.copy_dir_files(false);

    for(auto& dir: dirs) {
        q.push(dir);
    }

    while(q.size() > 0) {
        dir_val = q.front();

        for(auto& dir: DirJob(dir_val[0], dir_val[1], ring, DIR_BUF_SIZE, cp_ops.batchsize).copy_dir_files(true)) {
            q.push(dir);
        }
        q.pop();
    }

    get_time(&tt2);
    ttdiff = get_diff(&tt1, &tt2);
    update_ts(&total_time, &ttdiff);

    print_all_metrics();
}
