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
static size_t block_size = 4096; // OSX Page Size

#define WITH_CRYPTO_PATH(line) \
  char *cpath = _crypto_path(path); \
  if(cpath == NULL) return -ENOMEM; \
  line; \
  free(cpath);

#define CHECK_ERR \
  if(err == -1) \
    return -errno; \
  return 0;

char * _crypto_path(const char *path){
  char *ret;
  asprintf(&ret, path[0] == '/' ? "%s%s" : "%s/%s" , crypto_dir, path);
  return ret;
}

static int crypto_getattr(const char *path, struct stat *st){
  WITH_CRYPTO_PATH(int err = lstat(cpath, st))

  CHECK_ERR
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

  int err;

  if(S_ISFIFO(mode)) {
    err = mkfifo(cpath, mode);
  } else {
    err = mknod(cpath, mode, dev);
  }

  free(cpath);

  CHECK_ERR
}

static int crypto_open(const char *path, struct fuse_file_info *inf){
  WITH_CRYPTO_PATH(int fh = open(cpath, inf->flags))

  if(fh == -1)
    return -errno;

  inf->fh = fh;

  return 0;
}

static int crypto_create(const char *path, mode_t mode,
                         struct fuse_file_info *inf){
  WITH_CRYPTO_PATH(int fh = open(cpath, inf->flags, mode))

  if(fh == -1)
    return -errno;

  inf->fh = fh;

  return 0;
}

static int crypto_unlink(const char *path) {
  WITH_CRYPTO_PATH(int err = unlink(cpath))

  CHECK_ERR
}

static int crypto_read(const char *path, char *buf, size_t size,
                       off_t off, struct fuse_file_info *inf){
  (void) path;

  int red = 0;
  off_t boff  = off / block_size * block_size;
  off_t delta = off - boff;
  while(size > 0) {
    char b[block_size];
    size_t res = pread(inf->fh, b, block_size, boff);
    printf("boff: %lli\n",  boff);

    if(res == -1)
      return -errno;
    memcpy(buf + red, b + delta, block_size - delta);
    size  -= res - delta;
    red   += res - delta;
    boff  += res;
    delta  = 0;
  }

  return red;
}

// We encrypt like GDBE each sector has a crypto_secretbox_NONCEBYTES-long
// nonce prepended to each sector.
static int crypto_write(const char *path, const char *buf, size_t size,
                        off_t off, struct fuse_file_info *inf){
  (void) path;
  int written = 0;
  off_t boff  = off / block_size * block_size;
  off_t delta = boff - off;

  while(size > 0) {
    size_t to_write = size < (block_size - delta) ? size : (block_size - delta);
    int res = pwrite(inf->fh, buf + written, to_write, boff - delta);
    if(res == -1)
      return -errno;
    written += res;
    boff    += res;
    size    -= res;
    delta    = 0;
  }

  return written;
}

static int crypto_truncate(const char *path, off_t off){
  WITH_CRYPTO_PATH(int err = truncate(cpath, off))

  CHECK_ERR
}

static int crypto_ftruncate(const char *path, off_t off,
                            struct fuse_file_info *inf){
  (void) path;

  int err = ftruncate(inf->fh, off);

  CHECK_ERR
}

static int crypto_statfs(const char *path, struct statvfs *stat){
  WITH_CRYPTO_PATH(int err = statvfs(cpath, stat))

  CHECK_ERR
}

static int crypto_mkdir(const char *path, mode_t mode){
  WITH_CRYPTO_PATH(int err = mkdir(cpath, mode))

  CHECK_ERR
}

static int crypto_rmdir(const char *path){
  WITH_CRYPTO_PATH(int err = rmdir(cpath))

  CHECK_ERR
}

static int crypto_rename(const char *from, const char *to){
  char *cfrom = _crypto_path(from);
  char *cto   = _crypto_path(to);
  int err     = rename(cfrom, cto);
  free(cfrom);
  free(cto);
  CHECK_ERR
}

static int crypto_symlink(const char *from, const char *to){
  char *cfrom = _crypto_path(from);
  char *cto   = _crypto_path(to);
  int err     = symlink(cfrom, cto);
  free(cfrom);
  free(cto);
  CHECK_ERR
}

static int crypto_link(const char *from, const char *to){
  char *cfrom = _crypto_path(from);
  char *cto   = _crypto_path(to);
  int err     = link(cfrom, cto);
  free(cfrom);
  free(cto);
  CHECK_ERR
}

// todo decrypt buf here
static int crypto_readlink(const char *path, char *buf, size_t size){
  WITH_CRYPTO_PATH(int err = readlink(cpath, buf, size - 1))

  if(err == -1)
    return -errno;

  buf[size] = '\0';
  return 0;
}

static int crypto_chmod(const char *path, mode_t mode){
  WITH_CRYPTO_PATH(int err = chmod(cpath, mode))

  CHECK_ERR
}

static int crypto_chown(const char *path, uid_t uid, gid_t gid){
  WITH_CRYPTO_PATH(int err = chown(cpath, uid, gid))

  CHECK_ERR
}

static struct fuse_operations crypto_ops = {
  .getattr   = crypto_getattr,
  .readdir   = crypto_readdir,
  .mknod     = crypto_mknod,
  .open      = crypto_open,
  .unlink    = crypto_unlink,
  .create    = crypto_create,
  .read      = crypto_read,
  .write     = crypto_write,
  .truncate  = crypto_truncate,
  .ftruncate = crypto_ftruncate,
  .statfs    = crypto_statfs,
  .mkdir     = crypto_mkdir,
  .rmdir     = crypto_rmdir,
  .rename    = crypto_rename,
  .symlink   = crypto_symlink,
  .link      = crypto_link,
  .readlink  = crypto_readlink,
  .chmod     = crypto_chmod,
  .chown     = crypto_chown
};

int main(int argc, char *argv[]) {
  if(argc < 3) {
    printf("not enough arguments, usage: cryptofs <encdir> <mount>\n");
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
