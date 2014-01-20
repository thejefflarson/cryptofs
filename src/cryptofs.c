#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "lib/tweetnacl.h"
#include <fuse.h>

static char *crypto_dir;
static unsigned char key[crypto_secretbox_KEYBYTES];
static size_t block_size = 1024;

char * _crypto_path(const char *path){
  char *ret;
  asprintf(&ret, "%s%s", crypto_dir, path);
  return ret;
}

static int crypto_getattr(const char *path, struct stat *st){
  char *cpath = _crypto_path(path);
  if(cpath == NULL) return -ENOMEM;
  int res = lstat(cpath, st);
  free(cpath);

  if(res == -1)
    return -errno;

  return 0;
}

static int crypto_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *inf){
  DIR *dp;
  struct dirent *de;

  (void) offset;
  (void) inf;
  char *cpath = _crypto_path(path);
  if(cpath == NULL) return -ENOMEM;

  dp = opendir(cpath);
  if(dp == NULL) {
    free(cpath);
    return -errno;
  }

  while ((de = readdir(dp)) != NULL) {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_ino = de->d_ino;
    st.st_mode = de->d_type << 12;
    int ret = filler(buf, de->d_name, &st, 0);
    if(ret) break;
  }

  closedir(dp);
  free(cpath);
  return 0;
}

static int crypto_mknod(const char *path, mode_t mode, dev_t dev){
  char *cpath = _crypto_path(path);

  if(cpath == NULL) return -ENOMEM;

  int res;

  if(S_ISFIFO(mode)) {
    res = mkfifo(cpath, mode);
  } else {
    puts(cpath);
    res = mknod(cpath, mode, dev);
  }

  free(cpath);
  printf("%i", res);
  if(res == -1)
    return -errno;

  return 0;
}

static int crypto_open(const char *path, struct fuse_file_info *inf){
  char *cpath = _crypto_path(path);
  if(cpath == NULL) return -ENOMEM;

  int fh = open(cpath, inf->flags);
  free(cpath);

  if(fh == -1)
    return -errno;

  inf->fh = fh;

  return 0;
}

static int crypto_create(const char *path, mode_t mode, struct fuse_file_info *inf){
  char *cpath = _crypto_path(path);
  if(cpath == NULL) return -ENOMEM;

  int fh = open(cpath, inf->flags, mode);
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
  .readdir  = crypto_readdir,
  .mknod    = crypto_mknod,
  .open     = crypto_open,
  .create   = crypto_create,
  .read     = crypto_read,
  .write    = crypto_write
};

int main(int argc, char *argv[]) {
  if(argc < 3) {
    printf("%i not enough arguments, usage: cryptofs <encdir> <mount>\n", argc);
    return 1;
  }
  struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
  for(int i = 0; i < argc; i++) {
    if (i == 1)
      crypto_dir = realpath(argv[i], NULL);
    else
      fuse_opt_add_arg(&args, argv[i]);
  }
  char *pw = getpass("enter password: ");
  crypto_hash(key, (unsigned char *) pw, strnlen(pw, _PASSWORD_LEN));
  int ret = fuse_main(args.argc, args.argv, &crypto_ops, NULL);
  fuse_opt_free_args(&args);
  free(crypto_dir);
  return ret;
}