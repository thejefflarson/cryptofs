#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "lib/tweetnacl.h"
#include <fuse.h>

static char *crypto_dir;
static unsigned char key[crypto_secretbox_KEYBYTES];
static size_t block_size = 1024;

char * _crypto_path(char *crypto_path, const char *path){
  return strndup(path, strnlen(path, PATH_MAX));
}

static int crypto_getattr(const char *path, struct stat *st){
  char *cpath = _crypto_path(cpath, path);
  if(cpath == NULL) return -ENOMEM;

  int res = lstat(cpath, st);
  free(cpath);

  if(res == -1)
    return -errno;

  return 0;
}

static int crypto_mknod(const char *path, mode_t mode, dev_t dev){
  char *cpath = _crypto_path(cpath, path);
  if(cpath == NULL) return -ENOMEM;

  int res;

  if(S_ISFIFO(mode)) {
    res = mkfifo(cpath, mode);
  } else {
    res = mknod(cpath, mode, dev);
  }

  free(cpath);

  if(res == -1)
    return -errno;

  return 0;
}

static int crypto_open(const char *path, struct fuse_file_info *inf){
  char *cpath = _crypto_path(cpath, path);
  if(cpath == NULL) return -ENOMEM;

  uint64_t fh = open(cpath, inf->flags);
  free(cpath);

  if(fh == -1)
    return -errno;

  inf->fh = fh;
  return 0;
}

static int crypto_read(const char *path, char *buf, size_t size,
                       off_t off, struct fuse_file_info *inf){
  (void) path;

  int res = pread(inf->fh, buf, size, off);
  if(res == -1)
    return -errno;

  return res;
}

static int crypto_write(const char *path, const char *buf, size_t size,
                        off_t off, struct fuse_file_info *inf){
  (void) path;

  int res = pwrite(inf->fh, buf, size, off);
  if(res == -1)
    return -errno;

  return res;
}

static struct fuse_operations crypto_ops = {
  .getattr  = crypto_getattr,
  .mknod    = crypto_mknod,
  .open     = crypto_open,
  .read     = crypto_read,
  .write    = crypto_write
};

int main(int argc, char *argv[]) {
  if(argc < 3) {
    printf("%i not enough arguments, usage: cryptofs <encdir> <mount>\n", argc);
    fflush(stdout);
    return 1;
  }
  struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
  for(int i = 0; i < argc; i++) {
    if (i == 1)
      crypto_dir = argv[i];
    else
      fuse_opt_add_arg(&args, argv[i]);
  }
  crypto_dir = argv[1];
  char *pw;
  pw = getpass("enter password: ");
  crypto_hash(key, (unsigned char *) pw, strnlen(pw, _PASSWORD_LEN));
  return fuse_main(args.argc, args.argv, &crypto_ops, NULL);
}