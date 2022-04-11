#include <iostream>
#include <vector>
#include <string>
#include <string_view>

//! POSIX filesystem
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>

//! C++17 Filesystem for concat
#include <filesystem>

#include "cxxopts.hpp"

#define CHMOD_MODE_BITS \
  (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)

struct cp_options
{
    bool recursive = false;
};

bool copy(const std::string& src_name, const std::string& dst_name, 
          int dst_dirfd, std::string_view dst_relname, 
          bool nonexistent_dst, const cp_options& opt);

bool copy_dir(const std::string& src_name_in, const std::string& dst_name_in,
              int dst_dirfd, std::string_view dst_relname_in, bool new_dst,
              const struct stat* src_sb, const cp_options& opt)
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

        ok &= copy (src_name, dst_name, dst_dirfd,
                    dst_name.c_str() + (dst_relname_in.length() - dst_name_in.length()),
                    new_dst, opt);
    }

    return ok;
}

bool copy_reg(const std::string& src_name, const std::string& dst_name,
              int dst_dirfd, std::string_view dst_relname,
              const cp_options& opt,
              mode_t dst_mode, mode_t omitted_permissions, bool& new_dst,
              const struct stat& src_sb)
{
    bool return_val = true;
    int source_desc, dest_desc;
    mode_t extra_permissions;
    struct stat sb;
    struct stat src_open_sb;
    mode_t temporary_mode;
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

    //! Copy all data
    posix_fadvise(source_desc, 0, 0, POSIX_FADV_SEQUENTIAL);

    //! TODO: Add support for sparse files here!


close_src_and_dst_desc:
close_src_desc:
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
          bool nonexistent_dst, const cp_options& opt)
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

bool do_copy(const std::vector<std::string>& args, const cp_options& opt)
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
    
    //! Check if lastfile is actually a directory
    const auto& lastfile = args.back();
    int target_dirfd = open(lastfile.c_str(), O_DIRECTORY);
    if (target_dirfd < 0) {
        //! Since last file is not a directory, must be only arguments
        if (args.size() > 2)
        {
            fprintf(stderr, "last arg could not be opened as a dir / invalid no. of arguments");
            return false;
        }

        //! Check if this file does not exist
        int err = errno;
        return copy(args[0], args[1], AT_FDCWD, args[1], err == ENOENT, opt);
    }

    //! TODO: Copy multiple files to a directory

    return true;
}

int main(int argc, char** argv)
{
    cxxopts::Options options("cp", "barebones cp");
    options.allow_unrecognised_options();
    options.add_options()
    ("r,recursive", "copy files recursively", cxxopts::value<bool>()->default_value("false"))
    ("h,help", "Print usage");

    auto result = options.parse(argc, argv);
    if (result.count("help"))
    {
      std::cout << options.help() << std::endl;
      exit(0);
    }

    cp_options cp_ops;
    cp_ops.recursive = result["recursive"].as<bool>();

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

    bool ret = do_copy(result.unmatched(), cp_ops);

    return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}