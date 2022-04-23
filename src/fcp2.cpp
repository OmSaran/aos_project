#include <iostream>
#include <cassert>
#include <vector>
#include <map>
#include <queue>
#include <array>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <cstring>
#include <filesystem>

#include "fcp.h"

#include <linux/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <memory>

using namespace std;

// TODO: P0: File copying done only partially
// TODO: P0: Call getdents till we see all entries -- done only partially till now.
// TODO: P0: Make use of fixed buffers.
// TODO: P0: Use stat to get the file size, use that to pipeline bufsize of reads/writes.
// TODO: P0: Pipeline the open/create/read/write 
// TODO: P1: This won't scale, we need to schedule considering sq, cq sizes, fixed files, fixed buffers.
// TODO: P1: Check if we want to fstat to get the file size before copying. Alternative is to read until EOF in a pipelined fashion
// TODO: P2: Fix asserts;

#define IOSQE_CQE_SKIP_SUCCESS_BIT 6
#define IOSQE_CQE_SKIP_SUCCESS	(1U << IOSQE_CQE_SKIP_SUCCESS_BIT)

// TODO: Fix memory leaks


// TODO P2: Vectors are better
unordered_set<string> dirjobs;
// TODO P2: Should find a way without this
unordered_set<string> submitted_readdirs;


// src, dst, dst_dir // TODO: Probably compute dst_dir
// typedef array<string, 3> copyjob;


// Data structures
struct io_uring ring;

unordered_set<std::shared_ptr<CopyJob>> cp_jobs;
vector<io_uring_cqe*> pending_cqes;
unordered_set<string> created_dest_dirs;
RegFDAllocator<REG_FD_SIZE> fd_alloc;
unordered_map<string, std::vector<linux_dirent64>> dirent_buf_map;

static inline void io_uring_prep_getdents64(struct io_uring_sqe *sqe, int fd,
					    void *buf, unsigned int count)
{
	io_uring_prep_rw(IORING_OP_GETDENTS64, sqe, fd, buf, count, 0);
}

int prep_mkdir(const filesystem::path& dst_path) {
    struct io_uring_sqe *sqe;
    //! FIXME: Allocating memory for every mkdir can't be good; do something better
    RequestMeta *meta = new RequestMeta(FCP_OP_MKDIR);
    meta->dirpath = dst_path;

    // Get mkdir sqe
    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    // Prepare mkdir request
    // TODO: Fix perms
    cout << "Creating directory at " << dst_path.c_str() << endl;
    
    // TODO: Fix memory leak. 
    string *create_dirname = new string(dst_path);

    // path dst_pth = path(dst_path);
    io_uring_prep_mkdirat(sqe, -1, create_dirname->c_str(), 0777);
    io_uring_sqe_set_data(sqe, (void *)meta);

    return 1;
}

// Return number of requests queued
int prep_readdir(const filesystem::path& dirpath, std::vector<linux_dirent64>& dirbuf, const filesystem::path& dst_path) {
    struct io_uring_sqe *sqe;

    //! FIXME: mem alloc for every dir read can't be good :(
    RequestMeta *meta = new RequestMeta(FCP_OP_GETDENTS);
    // Store the regfd that was used for the open.
    meta->reg_fd = fd_alloc.get_free();

    // Get sqe
    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    //! FIXME: Fix memory leak / mem alloc for every dirpath can't be good
    string *sqe_dirname = new string(dirpath.string());

    // Prepare open request
    // TODO: We don't want to open it again for subsequent writes.
    io_uring_prep_openat_direct(sqe, 0, sqe_dirname->c_str(), O_RDONLY, 0, meta->reg_fd);
    // This operation won't return a cqe (IOSQE_CQE_SKIP_SUCCESS)
    sqe->flags = IOSQE_IO_LINK | IOSQE_CQE_SKIP_SUCCESS;

    // Get sqe for getdents
    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    // Prepare getdents request
    // TODO: Do this in loop.
    io_uring_prep_getdents64(sqe, meta->reg_fd, &dirbuf[0], DIR_BUF_SIZE);

    //! FIXME: Copying strings/path is never good, use a hash/id of the entire job, and keep a job set
    meta->dirpath = dirpath;
    meta->dest_dirpath = dst_path;
    io_uring_sqe_set_data(sqe, (void *)meta);
    sqe->flags = IOSQE_FIXED_FILE;
    // sqe->file_index = meta->reg_fd + 1;

    return 2;
}

void process_dir(const filesystem::path& src_path, const filesystem::path& dst) {
    struct io_uring_cqe *cqe;
    int num_wait = 0;
    int ret;

    // filesystem::path dst(dst_path);

    // bool need_submission = false;
    
    if(created_dest_dirs.find(dst.parent_path().string()) != created_dest_dirs.end()) {
        // Prepare mkdir request
        num_wait += prep_mkdir(dst);
        // need_submission = true;
    } else {
        //! FIXME: Way too much string passing, use id/hash
        dirjobs.insert(dst.string());
    }

    dirent_buf_map[src_path.string()].resize(MAX_DIR_ENT);

    // if(submitted_readdirs.find(src_path) == submitted_readdirs.end()) {
        // Prepare readdir request
        num_wait += prep_readdir(src_path, dirent_buf_map[src_path.string()], dst);
        // need_submission = true;
    // }

    io_uring_submit(&ring);


    // num_wait = 1;
    // ret = io_uring_submit_and_wait(&ring, 1);
    // assert(ret == num_wait);
}


void process_dir_jobs() {
    vector<string> to_erase;
    for(const auto& dir: dirjobs) {
        filesystem::path dst(dir);

        if(created_dest_dirs.find(dst.parent_path()) != created_dest_dirs.end()) {
            // If parent directory has been created, then create the directory
            prep_mkdir(dst);
            io_uring_submit(&ring);
            to_erase.push_back(dir);
        } else {
            cout << "Could not find parent directory for " << dst.string() << endl;   
        }
    }

    for(const auto& elem: to_erase) {
        dirjobs.erase(elem);
    } 
}

void process_getdents(const io_uring_cqe *cqe) {
    // Read the direntry list and then add to the cpjobs
    // copyjob* job = new copyjob(src, dst, dst_dir_path)
    assert(cqe->res >= 0);
	uint8_t *end;
    RequestMeta *meta = (RequestMeta *)cqe->user_data;
    filesystem::path src_path, dst_path, dst_dir;

	const auto& dirents = dirent_buf_map[meta->dirpath];

    const int num_entries_read = cqe->res / sizeof(linux_dirent64);

    for (int i = 0; i < num_entries_read; i++)
    {
        const auto& dent = dirents[i];
		if (strcmp(dent.d_name, ".") && strcmp(dent.d_name, "..")) 
        {
			// Create copy jobs;
            src_path = meta->dirpath / dent.d_name;
            dst_path = meta->dest_dirpath / dent.d_name;
            if(dent.d_type == DT_REG) {
                // cout << "dirent: " << dent->d_name << endl;

                // TODO: Compute the dest dirpath instead.
                // Sets the state to FSTAT_PENDING
                cp_jobs.insert(std::make_shared<CopyJob>(src_path, dst_path));
            } 
            else if (dent.d_type == DT_DIR) {
                process_dir(src_path, dst_path);
            }
		}
	}

    assert(meta->reg_fd != -1);
    fd_alloc.release(meta->reg_fd);
}

void process_stat_copy_job(const io_uring_cqe *cqe) {
    RequestMeta *meta = (RequestMeta *) cqe->user_data;
    meta->cp_job->set_size(meta->statbuf->stx_size);
    cout << "Setting the state to COPY_STAT_DONE for file " << meta->cp_job->get_dst_path() << endl;
    meta->cp_job->set_state(COPY_STAT_DONE);
}

void process_write_completion(const std::shared_ptr<CopyJob>& job) {
    if(job->get_size() - job->get_bytes_copied() == 0) {
        job->set_state(COPY_CP_DONE);
    }
}

/**
 * Process the current cqe //and re-process the rest 
 */
int process_cqe(const io_uring_cqe *cqe) {
    RequestMeta *meta;
    meta = (RequestMeta *)cqe->user_data;

    // TODO: P1: Handle other types
    switch(meta->type)
    {
        case FCP_OP_MKDIR: {
            cout << "GOT CQE! Processing a mkdir operation" << endl;
            if(cqe->res < 0) {
                cerr << "Mkdir at " << meta->dirpath << " operation failed: " << strerror(-cqe->res) << endl;
                exit(1);
            }
            created_dest_dirs.insert(meta->dirpath);
            break;
        }
        case FCP_OP_GETDENTS: {
            cout << "GOT CQE! Processing a getdents operation" << endl;
            if(cqe->res < 0) {
                cerr << "Getdents operation failed: " << strerror(-cqe->res) << endl;
                exit(1);
            }
            process_getdents(cqe);
            break;
        }
        case FCP_OP_OPENDIR: {
            // TODO: 
            cerr << "FCP_OP_OPENDIR not handled" << endl;
            exit(1);
            break;
        }
        case FCP_OP_READ: {
             if(cqe->res < 0) {
                cerr << "GOT CQE! A read operation failed: " << strerror(-cqe->res) << endl;
                exit(1);
            } else {
                cout << "GOT CQE! A read operation completed: " << cqe->res << endl;
            }
            // return 1;
            break;
        }
        case FCP_OP_WRITE: {
            if(cqe->res < 0) {
                cerr << "GOT CQE! A write operation failed: " << strerror(-cqe->res) << endl;
                exit(1);
            } else {
                cout << "GOT CQE! A write operation completed: " << cqe->res << endl;
                process_write_completion(meta->cp_job);
            }
            break;
        }
        case FCP_OP_CREATFILE: {
            if(cqe->res < 0) {
                cerr << "A create file operation for copy job failed: " << strerror(-cqe->res) << endl;
                exit(1);
            } else {
                RequestMeta *meta = (RequestMeta *)cqe->user_data;
                meta->cp_job->set_src_opened();
                cout << "GOT CQE! A create file operation for file " << meta->cp_job->get_dst_path() << " for copyjob has completed: " << cqe->res << endl;
            }
            break;
        }
        case FCP_OP_STAT_COPY_JOB: {
            if(cqe->res < 0) {
                cerr << "A stat operation for copy job failed: " << strerror(-cqe->res) << endl;
                exit(1);
            } else {
                cout << "GOT CQE! A stat operation for copy job completed: " << cqe->res << endl;
                process_stat_copy_job(cqe);
            }
            break;
        }
        case FCP_OP_OPENFILE: {
            if(cqe->res < 0) {
                cerr << "An openfile operation for copy job failed: " << strerror(-cqe->res) << endl;
                exit(1);
            } else {
                RequestMeta *meta = (RequestMeta *)cqe->user_data;
                meta->cp_job->set_dst_opened();
                cout << "GOT CQE! An openfile operation for copyjob of file " << meta->cp_job->get_dst_path() << " has completed: " << cqe->res  << endl;
            }
            break;
        }
        default: assert(false);
    }

    return 0;
}

void _prep_copy_opens(std::shared_ptr<CopyJob> job) {
    struct io_uring_sqe *sqe;
    RequestMeta *meta;

    int dst_reg_fd = fd_alloc.get_free();
    int src_reg_fd = fd_alloc.get_free();

    // ***** BEGIN: Open src dir *****
    //
    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    // TODO: Add fadvise if needed
    io_uring_prep_openat_direct(sqe, -1, job->get_src_path().c_str(), O_RDONLY, 0, src_reg_fd);
    sqe->flags = IOSQE_IO_LINK;
    // sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    meta = new RequestMeta(FCP_OP_OPENFILE);
    meta->cp_job = job;
    io_uring_sqe_set_data(sqe, (void *)meta);

    // ***** END: Open src dir *****

    // ***** BEGIN: Open/Create dst dir *****
    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    // TODO: Fix permissions
    io_uring_prep_openat_direct(sqe, -1, job->get_dst_path().c_str(), O_CREAT | O_WRONLY, 0777, dst_reg_fd);
    sqe->flags = IOSQE_IO_LINK;
    // sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    meta = new RequestMeta(FCP_OP_CREATFILE);
    meta->cp_job = job;
    io_uring_sqe_set_data(sqe, (void *) meta);
    // ***** END: Open/Create dst dir *****

    job->set_src_fd(src_reg_fd);
    job->set_dst_fd(dst_reg_fd);
}

// TODO: This can be further asynchronized/pipelined.
// return if submitted new operation
bool do_file_copy(std::shared_ptr<CopyJob> job) {
    struct io_uring_sqe *sqe;
    RequestMeta *meta;

    ssize_t max_buf_size = 1024;
    ssize_t bytes_to_copy = job->get_size() - job->get_bytes_copied();
    bytes_to_copy = max_buf_size < bytes_to_copy ? max_buf_size : bytes_to_copy;

    // finished copying the file
    if(job->get_size() > 0 && bytes_to_copy == 0 ) {
        if(job->get_state() == COPY_CP_DONE) {
            cout << "Error: " << job->get_dst_path() << endl;
            assert(0);
            // job->set_state(COPY_CP_DONE);
        }
        return false;
    }

    // TODO: Fix memory leak.
    // TODO: Use registered buffers.
    char *buf = (char *)calloc(bytes_to_copy, 1);

    // Submission chain.
    // open(src) -> open(dst) -> read(src) -> write(src) or read(src) -> write(src)

    if(job->get_bytes_copied() == 0) {
        // This means that this is the first write operation so we need to do open as well.
        _prep_copy_opens(job);
    } else {
        // If the file creation/openings have not completed, then do nothing.
        if(!job->is_dst_opened() || !job->is_src_opened()) {
            cout << "DOING NOTHING: " << job->get_dst_path() << endl;
            return false;
        }
    }

    cout << "bytes_to_copy = " << bytes_to_copy << " for file " << job->get_dst_path() << endl;

    // ***** BEGIN: Read src file *****
    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    cout << "src_reg_fd = " << job->get_src_fd() << endl;
    io_uring_prep_read(sqe, job->get_src_fd(), buf, bytes_to_copy, job->get_bytes_copied());
    sqe->flags = IOSQE_FIXED_FILE;
    // hardlink won't fail for partial reads.
    sqe->flags |= IOSQE_IO_LINK;
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    meta = new RequestMeta(FCP_OP_READ);
    io_uring_sqe_set_data(sqe, (void *)meta);
    // ***** END: Read src file *****
    
    // ***** BEGIN: Write dst file *****
    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    // TODO: Use file size for deterministic copy size.
    // TODO: Skip success CQE unless it is the last write
    cout << "dst_reg_fd = " << job->get_dst_fd() << endl;
    io_uring_prep_write(sqe, job->get_dst_fd(), buf, bytes_to_copy, job->get_bytes_copied());
    sqe->flags = IOSQE_FIXED_FILE;
    meta = new RequestMeta(FCP_OP_WRITE);
    meta->cp_job = job;
    cout << "WRITE ISSUE: " << job->get_dst_path() << " " << meta->cp_job << " = " << job  << endl;
    io_uring_sqe_set_data(sqe, (void *)meta);
    // ***** END: Write dst file *****

    // TODO: We assume success, fix for robustness
    job->add_bytes_copied(bytes_to_copy);

    io_uring_submit(&ring);
    return true;
}

void do_copy_fstat(std::shared_ptr<CopyJob> job) {
    struct io_uring_sqe *sqe;

    // TODO: Fix memory leaks
    RequestMeta *meta = new RequestMeta(FCP_OP_STAT_COPY_JOB);
    meta->cp_job = job;
    meta->statbuf = std::make_unique<statx>();

    // This means that stat is not done yet.
    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    io_uring_prep_statx(sqe, -1, job->get_src_path().c_str(), 0, STATX_SIZE, meta->statbuf.get());
    io_uring_sqe_set_data(sqe, meta);
    io_uring_submit(&ring);
    job->set_state(COPY_STAT_SUBMITTED);
    cout << "Submitted stat operation for " << job->get_dst_path() << endl;
}

bool process_copy_jobs() {
    cout << "Processing copy_jobs: " << cp_jobs.size() << endl;

    bool submitted = false;

    vector<std::shared_ptr<CopyJob>> to_delete;

    for(const auto& job: cp_jobs) {
        if(created_dest_dirs.find(job->get_dst_dir()) == created_dest_dirs.end()) {
            // parent dir not present so skip.
            break;
        }

        switch(job->get_state()) {
        case COPY_STAT_PENDING:
            cout << "Processing job in STAT_PENDING state "<< endl;
            do_copy_fstat(job);
            submitted = true;
            break;
        case COPY_STAT_SUBMITTED:
            break;
        case COPY_STAT_DONE:
        case COPY_CP_IN_PROGRESS:
            cout << "Processing job in STAT_DONE/CP_IN_PROGRESS " << endl;
            if(do_file_copy(job))
                submitted = true;
            break;
        case COPY_CP_DONE:
            cout << "Processing job in CP_DONE state "<< endl;
            to_delete.push_back(job);
            fd_alloc.release(job->get_dst_fd());
            fd_alloc.release(job->get_src_fd());
            break;
        default:
            cout << "Copy Job in invalid state " << job->get_state() << " Crashing" << endl;
            exit(1);
        }
    }

    //! FIXME: Bad pattern; erase in place using iterators
    for(const auto& job: to_delete) {
        cp_jobs.erase(job);
    }
    return submitted;
}

int main() {
    int ret;
    int files[REG_FD_SIZE];
    struct io_uring_cqe *cqe;

    // cp_jobs = new unordered_set<CopyJob*>();
    // pending_cqes = new vector<io_uring_cqe*>();
    // created_dest_dirs = new unordered_set<string>();
    // dirent_buf_map = new unordered_map<string, uint8_t*>();

    ret = io_uring_queue_init(RINGSIZE, &ring, 0);
    if (ret != 0)
    {
        cerr << "Failed to init io_uring queue " << strerror(-ret) << endl;
        return 1;
    }

    ret = io_uring_register_files(&ring, fd_alloc.fd_list.data(), fd_alloc.get_size());
    if(ret != 0) {
        cerr << "Failed to register files: " << strerror(-ret) << endl;
        return 1;
    }

    const filesystem::path src_dir("/home/ubuntu/project/aos_project/src_dir");
    const filesystem::path dst_dir("/home/ubuntu/project/aos_project/dst_dir");

    created_dest_dirs.insert("/home/ubuntu/project/aos_project");
    process_dir("/home/ubuntu/project/aos_project/build", "/home/ubuntu/project/aos_project/dst_dir");

    struct __kernel_timespec ts;
    ts.tv_nsec = 0;
    ts.tv_sec = 2;

    // for debugging
    // int count = 0;

    // Pipeline, pipeline, pipeline!
    while(true) {

        // For debugging, uncomment me.
        // if(count > 9) {
        //     exit(1);
        // }
        // count += 1;


        // TODO: P2: Find a better way to determine all jobs are complete.
        ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);
        if(ret != 0) {
            cerr << "Failed to get cqe: " << strerror(-ret) << endl;
            if(cp_jobs.size() == 0 && dirjobs.size() == 0) {
                cout << "No pending jobs and failed to get cqe, exiting" << endl;
                // sync();
                exit(0);
            }
            exit(1);
        }
        ret = process_cqe(cqe);
        if(ret == 0) {
            io_uring_cqe_seen(&ring, cqe);
        }
        process_copy_jobs();
        process_dir_jobs();
    }
}
