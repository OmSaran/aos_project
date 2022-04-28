#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <cassert>

//! POSIX filesystem
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

//! C++17 Filesystem for concat
#include <filesystem>

//! liburing
#include <liburing.h>
#include <atomic>

#include "buffer-lcm.h"
#include "cxxopts.hpp"

#define CHMOD_MODE_BITS \
  (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

//! Must be >= 2
#define RINGSIZE (1 << 14)

//! Must be a multiple of 2
#define MAX_OPEN_FILES 1000

//! coreutils/cp.c hardcodes this to 128KiB
//! We use this as the default bufsize
enum { IO_BUFSIZE = 128 * 1024 };

struct {
    struct io_uring* ring;
    unsigned pending_cqe;
    std::vector<int> open_fds;
    unsigned page_size;
    BufferManager buf_mgr;
    // char* buf;
} ctx;

void close_all_files()
{
    for (auto i : ctx.open_fds)
    {
        close(i);
    }
    ctx.open_fds.clear();
}

int handle_cqes(unsigned num_cqes)
{
    if (num_cqes == 0) return 0;
    assert(ctx.pending_cqe >= num_cqes);
    struct io_uring_cqe* cqe;

    // int ret = 0;
    // for (int i = 0; i < num_cqes; i++)
    // {
    //     cqe = NULL;
    //     io_uring_wait_cqe(ctx.ring, &cqe);
    //     io_uring_cq_advance(ctx.ring, 1);
    // }

    // printf("Handled %u cqes\n", num_cqes);
    int ret = io_uring_wait_cqe_nr(ctx.ring, &cqe, num_cqes);
    if (unlikely(ret < 0))
    {
        fprintf(stderr, "io_uring_wait_cqe_nr: %s\n", strerror(-ret));
        return ret;
    }
    //! TODO: Handle cqes

    io_uring_cq_advance(ctx.ring, num_cqes);
    
    ctx.pending_cqe -= num_cqes;
    return ret;
}

struct cp_options
{
    bool recursive = false;
    bool kernel_poll = false;
    unsigned ktime = 60000;
    size_t buf_size = IO_BUFSIZE;
    int num_bufs = 2;
    size_t ring_size = RINGSIZE;
};

bool copy(const std::string& src_name, const std::string& dst_name, 
          int dst_dirfd, std::string_view dst_relname, 
          bool nonexistent_dst, cp_options& opt);

bool copy_dir(const std::string& src_name_in, const std::string& dst_name_in,
              int dst_dirfd, std::string_view dst_relname_in, bool new_dst,
              const struct stat* src_sb, cp_options& opt)
{
    DIR* dirp = opendir(src_name_in.c_str());
    if (!dirp)
    {
        fprintf(stderr, "cannot access %s", src_name_in.c_str());
        return false;
    }

    bool ok = true;
    for(;;)
    {
        struct dirent const* dp;
        const char* entry;
        dp = readdir(dirp);
        if (!dp) break;

        entry = dp->d_name;

        /* Skip "", ".", and "..". */
        if (entry[entry[0] != '.' ? 0 : entry[1] != '.' ? 1 : 2] == '\0') continue;

        bool local_copy_into_self;
        std::string src_name = (std::filesystem::path(src_name_in) / entry);
        std::string dst_name = (std::filesystem::path(dst_name_in) / entry);

        ok &= copy(src_name, dst_name, dst_dirfd,
                   dst_name.c_str() + (dst_name_in.length() - dst_relname_in.length()),
                   new_dst, opt);
    }

    return ok;
}

/**
 * @brief copy regular file open on `src_fd` to `dst_fd`
 * *abuf for temp storage, allocating it lazily
 * copy no more than `max_n_read` bytes
 * 
 * @param src_fd 
 * @param dest_fd
 * @param abuf - this is to reuse the same buffer if it was 
 *               allocated in a prev call to sparse_copy
 * @param buf_size 
 * @param src_name 
 * @param dst_name 
 * @param max_n_read 
 * @param total_n_read 
 * @return true sucessful completion
 * @return false 
 */
bool sparse_copy(int src_fd, int dest_fd, char *buf, size_t buf_size,
                 const std::string& src_name, const std::string& dst_name,
                 const size_t filesize, off_t& total_n_read, cp_options& opt)
{
    // if (!*abuf)
    // {
    //     *abuf = (char *)aligned_alloc(getpagesize(), buf_size);
    //     if (unlikely(*abuf == NULL))
    //     {
    //         fprintf(stderr, "failed to allocate memory for buffer %d %ld", getpagesize(), buf_size);
    //         return false;
    //     }
    // }
    total_n_read = 0;
    off_t psize = 0;

    struct io_uring_sqe* sqe;
    size_t bytes_left = filesize;
    unsigned num_cqes = 0;
    bool unsubmitted_sqe = false;
    while(bytes_left)
    {
        //! Free up ring queue
        unsigned available_sqe = opt.ring_size - ctx.pending_cqe;
        unsigned needed_sqe = MIN(opt.ring_size, 2 * ((bytes_left / buf_size) + ((bytes_left % buf_size) != 0)));
        if (needed_sqe > available_sqe)
        {
            if (unsubmitted_sqe)
            {
                int ret = io_uring_submit(ctx.ring);
                if (unlikely(ret < 0))
                {
                    fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
                    return false;
                }
                unsubmitted_sqe = false;
            }
            
            //! TODO: Maybe MIN(NUM_FREE_AT_ONCE, needed_sqe - available_sqe) will work better
            //!       or maybe MIN(pending_cqe, NUM_FREE_AT_ONCE)??
            int ret = handle_cqes(needed_sqe - available_sqe);
            if (unlikely(ret) < 0)
            {
                return false;
            }
            available_sqe = needed_sqe;
        }

        // handle_cqes(ctx.pending_cqe);
        //! Queue RW requests
        unsigned num_used = 0;
        while ((num_used < available_sqe) && bytes_left)
        {
            size_t bytes_to_read = MIN(buf_size, bytes_left);
            sqe = io_uring_get_sqe(ctx.ring);
            assert(sqe);

            // ssize_t n_read = read(src_fd, *abuf, MIN(max_n_read, buf_size));
            io_uring_prep_read(sqe, src_fd, buf, bytes_to_read, -1);
            io_uring_sqe_set_data64(sqe, 1);
            sqe->flags |= IOSQE_IO_LINK;
            total_n_read += bytes_to_read;
            num_used++;

            sqe = io_uring_get_sqe(ctx.ring);
            assert(sqe);
            io_uring_prep_write(sqe, dest_fd, buf, bytes_to_read, -1);
            io_uring_sqe_set_data64(sqe, 2);
            sqe->flags |= IOSQE_IO_LINK;
            num_used++;

            bytes_left -= bytes_to_read;
        }

        //! Update state
        unsubmitted_sqe = true;
        ctx.pending_cqe += num_used;
    }
    
    int ret = io_uring_submit(ctx.ring);
    if (unlikely(ret < 0))
    {
        fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
        return false;
    }
    
    return true;
}

bool copy_reg(const std::string& src_name, const std::string& dst_name,
              int dst_dirfd, std::string_view dst_relname,
              cp_options& opt,
              mode_t dst_mode, mode_t omitted_permissions, bool& new_dst,
              const struct stat& src_sb)
{
    char* buf = NULL;
    off_t n_read;
    bool return_val = true;
    int source_desc, dest_desc;
    mode_t extra_permissions;
    struct stat sb;
    struct stat src_open_sb;
    mode_t temporary_mode;

    size_t buf_size, src_blk_size;
    size_t blcm_max, blcm;

    //! # of open files shouldn't exceed `MAX_OPEN_FILES-2`
    if (ctx.open_fds.size() > MAX_OPEN_FILES - 1)
    {
        handle_cqes(ctx.pending_cqe);
        close_all_files();
    }
    source_desc = open(src_name.c_str(), O_RDONLY);
    if (source_desc < 0)
    {
        fprintf(stderr, "cannot open %s for reading", src_name.c_str());
        return false;
    }
    if (fstat(source_desc, &src_open_sb) != 0)
    {
        fprintf(stderr, "cannot fstat %s", src_name.c_str());
        return_val = false;
        goto close_src_desc;
    }

    /* Compare the source dev/ino from the open file to the incoming,
     saved ones obtained via a previous call to stat.  */
    if ((src_sb.st_ino != src_open_sb.st_ino) || 
        (src_sb.st_dev != src_open_sb.st_dev))
    {
        fprintf(stderr, "skipping file %s, as it was replaced while being copied",
                src_name.c_str());
        return_val = false;
        goto close_src_desc;
    }

    if (!new_dst)
    {
        int open_flags = O_WRONLY | O_TRUNC;
        dest_desc = openat(dst_dirfd, dst_relname.data(), open_flags);

        //! TODO: to support -f, unlink file after failed open
        if (dest_desc < 0 && errno == ENOENT)
        {
            new_dst = true;
        }
    }
    if (new_dst)
    {
        //! TODO: Add copy_file_range here!!!
        //! TODO: add support for --preserve here
        mode_t open_mode = dst_mode & ~omitted_permissions;
        extra_permissions = open_mode & ~dst_mode; /* either 0 or S_IWUSR */

        int open_flags = O_WRONLY | O_CREAT;
        dest_desc = openat(dst_dirfd, dst_relname.data(), open_flags | O_EXCL, open_mode);

    }
    else
    {
        omitted_permissions = extra_permissions = 0;
    }

    if (dest_desc < 0)
    {
        fprintf(stderr, "cannot create regular file %s", dst_name.c_str());
        return_val = false;
        goto close_src_desc;
    }

    //! TODO: Add support for --reflink here!!!
    if (fstat(dest_desc, &sb) != 0)
    {
        fprintf(stderr, "cannot fstat %s", dst_name.c_str());
        return_val = false;
        goto close_src_and_dst_desc;
    }

    /* If extra permissions needed for copy_xattr didn't happen (e.g.,
     due to umask) chmod to add them temporarily; if that fails give
     up with extra permissions, letting copy_attr fail later.  */
    temporary_mode = sb.st_mode | extra_permissions;
    if (temporary_mode != sb.st_mode
        && (fchmod(dest_desc, temporary_mode) != 0))
    {
        extra_permissions = 0;
    }

    //! TODO: Add support for sparse files here!

    //! advise sequential read
    posix_fadvise(source_desc, 0, 0, POSIX_FADV_SEQUENTIAL);

    // buf_size = MIN(SIZE_MAX / 2UL + 1UL, (size_t)MAX(opt.buf_size, sb.st_blksize));
    // src_blk_size = MIN(SIZE_MAX / 2UL + 1Ul, (size_t)MAX(opt.buf_size, src_open_sb.st_blksize));
    
    // /* Compute the least common multiple of the input and output
    //    buffer sizes, adjusting for outlandish values.  */
    // blcm_max = SIZE_MAX;
    // blcm = buffer_lcm(src_blk_size, buf_size, blcm_max);

    // /* Do not bother with a buffer larger than the input file, plus one
    //    byte to make sure the file has not grown while reading it.  */
    // if (S_ISREG (src_open_sb.st_mode) && (size_t)src_open_sb.st_size < buf_size)
    // {
    //     buf_size = src_open_sb.st_size + 1;
    // }

    // /* However, stick with a block size that is a positive multiple of
    //    blcm, overriding the above adjustments.  Watch out for
    //    overflow.  */
    // buf_size += blcm - 1;
    // buf_size -= buf_size % blcm;
    // if (buf_size == 0 || blcm_max < buf_size)
    // {
    //     buf_size = blcm;
    // }
    buf = ctx.buf_mgr.get_next_buf();
    if (buf == NULL)
    {
        handle_cqes(ctx.pending_cqe);
        ctx.buf_mgr.free_all();
        buf = ctx.buf_mgr.get_next_buf();
    }
    sparse_copy(source_desc, dest_desc, buf, opt.buf_size,
                src_name, dst_name, src_open_sb.st_size, n_read, opt);
    //! TODO: --preserve timestamps, ownerships, xattr, author, acl
    //! TODO: remove extra permissions


close_src_and_dst_desc:
    ctx.open_fds.push_back(dest_desc);
    // if (close(dest_desc) < 0)
    // {
    //     fprintf(stderr, "failed to close %s", dst_name.c_str());
    //     return_val = false;
    // }
close_src_desc:
    ctx.open_fds.push_back(source_desc);
    // if (close(source_desc) < 0)
    // {
    //     fprintf(stderr, "failed to close %s", src_name.c_str());
    //     return_val = false;
    // }

    /**
     * @note the reason why buf is allocated inside sparse_copy,
     *       but freed here is so that it can be reused when copying multiple
     *       blocks
     */
    return return_val;
}
/**
 * @brief copy `src` to `dst_dirfd` + `dst_name` 
 * 
 * @param src 
 * @param dst_name 
 * @param dst_dirfd 
 * @param dst_relname 
 * @param nonexistent_dst true if file does not exist
 * @return bool
 */
bool copy(const std::string& src_name, const std::string& dst_name, 
          int dst_dirfd, std::string_view dst_relname, 
          bool nonexistent_dst, cp_options& opt)
{
    struct stat src_sb, dst_sb;
    //! TODO: symlinks are NOT followed; change to support -L
    if (fstatat(AT_FDCWD, src_name.c_str(), &src_sb, AT_SYMLINK_NOFOLLOW))
    {
        fprintf(stderr, "cannot stat %s", src_name.c_str());
        return false;
    }

    if (S_ISDIR(src_sb.st_mode) & !opt.recursive)
    {
        fprintf(stderr, "-r not specified, omitting directory %s", src_name.c_str());
        return false;
    }
    
    //! TODO: For multifile copy, check if same file appears more than once



    bool new_dst = false;
    if (!nonexistent_dst)
    {
        new_dst = true;
    }
    else if (fstatat(dst_dirfd, dst_relname.data(), &dst_sb, 0) != 0)
    {
        if (errno != ENOENT)
        {
            fprintf(stderr, "cannot stat %s", dst_name.c_str());
            return false;
        }
        new_dst = true;
    }

    if (!new_dst) 
    {
        //! TODO: Check if src is the same file as dst
        //! TODO: For --update, compare timestamps
        //! TODO: For -i or interactive_always_no, check if overwriting is ok or return true

        //! if src is a dir, but destination isn't -- error
        if (!S_ISDIR(dst_sb.st_mode))
        {
            if (S_ISDIR(src_sb.st_mode))
            {
                fprintf(stderr, "cannot overwrite non-directory %s with directory %s", dst_name.c_str(), src_name.c_str());
                return false;
            }

            //! TODO: Handle this case
            //! rm -rf a b c; mkdir a b c; touch a/f b/f; mv a/f b/f c
            //! error ("will not overwrite just-created c/f with b/f")
        }

        //! if destination is a directory, but source isn't
        if (!S_ISDIR(src_sb.st_mode) && S_ISDIR(dst_sb.st_mode))
        {
            fprintf(stderr, "cannot overwrite directory with non-directory %s", dst_name.c_str());
            return false;
        }

        //! TODO: Handle backups
        //! TODO: Handle unlinking destination if needed
    }


    /* TODO: If the ownership might change, or if it is a directory (whose
     special mode bits may change after the directory is created),
     omit some permissions at first, so unauthorized users cannot nip
     in before the file is ready. */
    mode_t dst_mode_bits, omitted_permissions;
    dst_mode_bits = src_sb.st_mode & CHMOD_MODE_BITS;
    //! TODO: handle preserve_ownership
    omitted_permissions = dst_mode_bits & (S_ISDIR(src_sb.st_mode) ? S_IWGRP | S_IWOTH : 0);

    //! TODO: For SELinux and -Z, set process security context

    bool delayed_ok = true;
    if (S_ISDIR(src_sb.st_mode))
    {
        //! TODO: Check if this directory has already been copied during recursion or otherwise
        bool restore_dst_mode = true;
        // make new dir
        if (new_dst || !S_ISDIR(dst_sb.st_mode))
        {
            mode_t mode = dst_mode_bits & ~omitted_permissions;
            if (mkdirat(dst_dirfd, dst_relname.data(), mode) != 0)
            {
                fprintf(stderr, "cannot create directory %s", dst_name.c_str());
                return false;
            }

            /* We need search and write permissions to the new directory
             for writing the directory's contents. Check if these
             permissions are there.  */
            if (fstatat(dst_dirfd, dst_relname.data(), &dst_sb, AT_SYMLINK_NOFOLLOW) != 0)
            {
                fprintf(stderr, "cannot stat %s", dst_name.c_str());
                return false;
            }
            else if ((dst_sb.st_mode & S_IRWXU) != S_IRWXU)
            {
                if (fchmodat(dst_dirfd, dst_relname.data(), dst_sb.st_mode | S_IRWXU, AT_SYMLINK_NOFOLLOW) != 0)
                {
                    fprintf(stderr, "setting permissions for %s", dst_name.c_str());
                    return false;
                }
            }
        }
        else
        {
            omitted_permissions = 0;
        }

        //! TODO: Handle --one_file_system
        delayed_ok = copy_dir(src_name, dst_name, dst_dirfd, dst_relname, 
                              new_dst, &src_sb, opt);
    }
    //! TODO: else if (symbolic_link)
    //! TODO: else if (hard_link)
    else if (S_ISREG(src_sb.st_mode))
    {
        if (!copy_reg(src_name, dst_name, dst_dirfd, dst_relname,
                      opt, dst_mode_bits & (S_IRWXU|S_IRWXG|S_IRWXO),
                      omitted_permissions, new_dst, src_sb))
        {
            return false;
        }
    }
    //! TODO: elseif (S_ISFIFO(src_sb.st_mode))
    //! TODO: block, CHR, socket, S_ISLNK
    else
    {
        fprintf(stderr, "%s has unknown file type", src_name.c_str());
        return false;
    }


    //! TODO: Set acl
    return delayed_ok;
}


static inline bool
must_be_working_directory (char const *f)
{
  /* Return true for ".", "./.", ".///./", etc.  */
  while (*f++ == '.')
    {
      if (*f != '/')
        return !*f;
      while (*++f == '/')
        continue;
      if (!*f)
        return true;
    }
  return false;
}

bool do_copy(const std::vector<std::string>& args, cp_options& opt)
{
    if (args.size() == 0) 
    {
        fprintf(stderr, "missing file operand");
        return false;
    }
    if (args.size() == 1)
    {
        fprintf(stderr, "missing destination file operand after %s", args[0].c_str());
        return false;
    }
    
    //! Check if lastfile is actually a directory (target_directory_operand)
    const auto& lastfile = args.back();
    // int 
    int target_dirfd;
    //! TODO: Opt: if directory is ., then just use AT_FDCWD
    if (must_be_working_directory(lastfile.c_str()))
    {
        target_dirfd = AT_FDCWD;
    }
    else
    {
        target_dirfd = open(lastfile.c_str(), O_DIRECTORY | O_PATH);
    }
    bool new_dst = (errno == ENOENT);
    bool ok = true;
    if (target_dirfd < 0) 
    {
        //! Since last file is not a directory, must be only arguments
        if (args.size() > 2)
        {
            fprintf(stderr, "last arg could not be opened as a dir / invalid no. of arguments");
            return false;
        }
        //! Check if this file does not exist
        ok = copy(args[0], args[1], AT_FDCWD, args[1], !new_dst, opt);
    }
    else
    {
        /* cp file1...filen edir
         Copy the files 'file1' through 'filen'
         to the existing directory 'edir'. */
        int n_files = args.size() - 1;
        for (int i = 0; i < n_files; i++)
        {
            const auto& file = std::filesystem::path(args[i]);
            auto it = file.end();
            it--;
            if (*it == "") it--;
            
            //! Think what will happen on cp -r A/.. B/
            if (*it == "..") {
                it++;
            }
            std::string dst_name = std::filesystem::path(lastfile) / *it;

            ok &= copy(file, dst_name, target_dirfd, 
                       std::string_view(dst_name).substr(lastfile.length(), (*it).string().length()),
                       !new_dst, opt);
        }
    }

    return ok;
}

int main(int argc, char** argv)
{
    cxxopts::Options options("cp", "barebones cp");
    options.allow_unrecognised_options();
    options.add_options()
    ("r,recursive", "copy files recursively", cxxopts::value<bool>()->default_value("false"))
    ("k,kpoll", "use kernel polling w/ io_uring", cxxopts::value<bool>()->default_value("false"))
    ("t,ktime", "kernel polling timeout", cxxopts::value<unsigned>()->default_value("60000"))
    ("b,buffersize", "total size of all buffers in KiB", cxxopts::value<size_t>())
    ("n,num_bufs", "number of buffers", cxxopts::value<int>())
    ("q,ringsize", "size of io_uring ring queue", cxxopts::value<size_t>())
    ("h,help", "Print usage");

    auto result = options.parse(argc, argv);
    if (result.count("help"))
    {
      std::cout << options.help() << std::endl;
      exit(0);
    }

    cp_options cp_ops;
    cp_ops.recursive = result["recursive"].as<bool>();
    cp_ops.kernel_poll = result["kpoll"].as<bool>();
    cp_ops.ktime = result["ktime"].as<unsigned>();

    if (result.count("num_bufs"))
    {
        cp_ops.num_bufs = result["num_bufs"].as<int>();
    }
    if (result.count("buffersize"))
    {
        cp_ops.buf_size = result["buffersize"].as<size_t>() * 1024 / cp_ops.num_bufs;
    }
    if (result.count("ringsize"))
    {
        cp_ops.ring_size = result["ringsize"].as<size_t>();
    }
    /**
     * Options not supported:
     * 1. -p: preserve perms
     * 2. -i: interactive
     * 3. -L/-l: hardlinks deref
     * 4. -v: verbose
     * 5. --refline
     * 6. --sparse
     * 7. -Z: selinux stuff
     * 
     * TODO: Add support for -t?
     * TODO: Add support for -T?
     */
    
    //! Init ctx
    ctx.buf_mgr.init(cp_ops.num_bufs, cp_ops.buf_size);
    ctx.open_fds.reserve(MAX_OPEN_FILES);
    ctx.page_size = getpagesize();

    //! Init io_uring
    struct io_uring iou;
    ctx.ring = &iou;
    
    struct io_uring_params params;
    memset(&params, 0, sizeof(io_uring_params));

    //! TODO: Handle wakeup
    if (cp_ops.kernel_poll)
    {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = cp_ops.ktime;
    }

    int res = io_uring_queue_init_params(cp_ops.ring_size, ctx.ring, &params);
    if (res != 0)
    {
        fprintf(stderr, "failed to init io_uring queue (%s)\n", strerror(-res));
        return EXIT_FAILURE;
    }
    bool ret = do_copy(result.unmatched(), cp_ops);

    //! Handle remaining cqe
    int err = handle_cqes(ctx.pending_cqe);
    if(unlikely(err < 0))
    {
        ret = false;
    }

    //! Exit io_uring
    io_uring_queue_exit(ctx.ring);

    //! close all files
    close_all_files();
    return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}