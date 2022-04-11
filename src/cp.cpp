#include <iostream>
#include <vector>
#include <string>

//! POSIX filesystem
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "cxxopts.hpp"

struct cp_options
{
    bool recursive = false;
};

/**
 * @brief copy `src` to `dst_dirfd` + `dst_name` 
 * 
 * @param src 
 * @param dst_name 
 * @param dst_dirfd 
 * @param dst_relname 
 * @param nonexistent_dst true if file does not exist
 * @return true 
 * @return false 
 */
bool copy(const std::string& src_name, const std::string& dst_name, int dst_dirfd, const std::string& dst_relname, bool nonexistent_dst, const cp_options& opt)
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
    else if (fstatat(dst_dirfd, dst_relname.c_str(), &dst_sb, 0) != 0)
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
    }

    /* TODO: If the ownership might change, or if it is a directory (whose
     special mode bits may change after the directory is created),
     omit some permissions at first, so unauthorized users cannot nip
     in before the file is ready. */
    
    if (S_ISDIR(src_sb.st_mode))
    {
        //! TODO: Check if this directory has already been copied during recursion

        //! TODO: Copy directory
    }
    //! TODO: else if (symbolic_link)
    //! TODO: else if (hard_link)
    else if (S_ISREG(src_sb.st_mode))
    {

    }
    //! TODO: elseif (S_ISFIFO(src_sb.st_mode))
    //! TODO: block, CHR, socket, S_ISLNK
    else
    {
        fprintf(stderr, "%s has unknown file type", src_name.c_str());
        return false;
    }



    return false;
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