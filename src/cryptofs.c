#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <termios.h>

#include "lib/tweetnacl.h"
#include <fuse.h>

static char *crypto_dir;
static unsigned char key[crypto_secretbox_KEYBYTES];
static const int crypto_PADDING = crypto_secretbox_NONCEBYTES + crypto_secretbox_BOXZEROBYTES;
static const size_t block_size = 4096; // OSX Page Size

#define WITH_CRYPTO_PATH(line) \
  char *cpath = _crypto_path(path); \
  if(cpath == NULL) return -ENOMEM; \
  line; \
  free(cpath);

#define CHECK_ERR \
  if(err == -1) return -errno; \
  return 0;

char * _crypto_path(const char *path){
  char *ret;
  asprintf(&ret, path[0] == '/' ? "%s%s" : "%s/%s" , crypto_dir, path);
  return ret;
}

static int crypto_fsync(const char *path, int datasync, struct fuse_file_info *inf){
  (void) path;
  (void) datasync;

  int err = fsync(inf->fh);

  CHECK_ERR
}

static int crypto_getattr(const char *path, struct stat *st){
  WITH_CRYPTO_PATH(int err = lstat(cpath, st))

  if(S_ISREG(st->st_mode)) {
    size_t num_blocks = st->st_size / block_size;
    size_t total_overhead = num_blocks * crypto_PADDING;
    if(st->st_size % block_size > 0)
      total_overhead += crypto_PADDING;
    st->st_size -= total_overhead;
  }

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
  size_t red = 0;

  while(size > 0) {
    int idx = off / (block_size - crypto_PADDING);
    size_t delta = off % (block_size - crypto_PADDING);
    size_t bsize = block_size, fudge = 0;

    if(size < block_size - crypto_PADDING) {
      // We have to check that we aren't in partial block land, when reading from
      // the end of the file by stating the file and checking that the requested
      // offset isn't a slice of a partial block.
      struct stat st = {0};
      int staterr = crypto_getattr(path, &st);
      if(staterr < 0) return staterr;
      size_t next_block = (idx + 1) * (block_size - crypto_PADDING);
      if(st.st_size < next_block)
        next_block = st.st_size;

      if(next_block - off > size)
        fudge = next_block - off - size;
    }

    if(bsize > block_size)
      printf("bzize: %zu size: %zu delta: %zu\n", bsize, size, delta);

    char block[bsize];
    int res = pread(inf->fh, block, bsize, block_size * idx);
    if(res == -1)
      return -errno;

    if(res == 0)
      return red;

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    memcpy(nonce, block, crypto_secretbox_NONCEBYTES);

    size_t csize = res - crypto_secretbox_NONCEBYTES + crypto_secretbox_BOXZEROBYTES;
    unsigned char cpad[csize];
    memset(cpad, 0, csize);
    memcpy(cpad + crypto_secretbox_BOXZEROBYTES, block + crypto_secretbox_NONCEBYTES, csize);

    unsigned char mpad[csize];
    memset(mpad, 0, csize);

    int ruroh = crypto_secretbox_open(mpad, cpad, csize, nonce, key);
    if(ruroh == -1) {
      printf("error at index: %i offset: %llu read: %i size: %zu path: %s\n", idx, off, res, bsize, path);
      return -ENXIO;
    }

    memcpy(buf + red, mpad + delta + crypto_secretbox_ZEROBYTES, csize - delta - fudge - crypto_secretbox_ZEROBYTES);

    size -= res - crypto_PADDING - fudge - delta;
    red  += res - crypto_PADDING - fudge - delta;
    off  += res - crypto_PADDING - fudge - delta;
  }

  return red;
}

// We encrypt like GDBE each sector has a random
// nonce prepended to each sector.
static int crypto_write(const char *path, const char *buf, size_t size,
                        off_t off, struct fuse_file_info *inf){
  size_t written = 0;

  while(size > 0) {
    int idx = off / (block_size - crypto_PADDING);

    // Grab a random nonce
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes(nonce, crypto_secretbox_NONCEBYTES);

    // Set up the necessary buffers
    size_t to_write = size < block_size - crypto_PADDING ? size + crypto_PADDING : block_size;
    size_t msize = to_write - crypto_PADDING;
    size_t fudge = 0;

    char padding[block_size];
    if(off % (block_size - crypto_PADDING) != 0) {
      // At partial block, have to read the rest of the data
      // and append the new stuff to our buffer.
      size_t leftovers = off % (block_size - crypto_PADDING);
      off_t  block_off = idx * (block_size - crypto_PADDING);
      struct fuse_file_info of = {.flags = O_RDONLY};
      int fd = crypto_open(path, &of);
      if(fd == -1) return -errno;
      int res = crypto_read(path, padding, leftovers, block_off, &of);
      if(res < 0) return res;
      close(fd);
      if(to_write + res < block_size - crypto_PADDING)
        to_write += res;
      fudge = res;
    }

    unsigned char mpad[to_write];
    unsigned char cpad[to_write];
    memset(mpad, 0, to_write);
    memset(cpad, 0, to_write);
    memcpy(mpad + crypto_secretbox_ZEROBYTES, padding, fudge);
    memcpy(mpad + crypto_secretbox_ZEROBYTES + fudge, buf + written, msize);

    int ohno = crypto_secretbox(cpad, mpad, msize + crypto_secretbox_ZEROBYTES, nonce, key);
    if(ohno < 0) return -ENXIO;

    unsigned char block[to_write];
    memset(block, 0, to_write);
    memcpy(block, nonce, crypto_secretbox_NONCEBYTES);
    memcpy(block + crypto_secretbox_NONCEBYTES, cpad + crypto_secretbox_BOXZEROBYTES, msize + crypto_secretbox_BOXZEROBYTES);

    int res = pwrite(inf->fh, block, to_write, block_size * idx);
    if(res == -1) return -errno;

    res     -= crypto_PADDING;
    written += res - fudge;
    size    -= res - fudge;
    off     += res - fudge;
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
  .fsync     = crypto_fsync,
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

// from: http://www.gnu.org/software/libc/manual/html_node/getpass.html
ssize_t _crypto_getpass(char **lineptr, size_t *n, FILE *stream){
  struct termios old, new;
  int nread = 0;

  if(isatty(fileno(stream))){
    if (tcgetattr(fileno(stream), &old) != 0)
      return -1;
    new = old;
    new.c_lflag &= ~ECHO;
    if (tcsetattr(fileno(stream), TCSAFLUSH, &new) != 0)
      return -1;
  }

  nread = getline(lineptr, n, stream);

  if(isatty(fileno(stream)))
    (void) tcsetattr(fileno(stream), TCSAFLUSH, &old);

  return nread;
}


int main(int argc, char *argv[]) {
  if(argc < 3) {
    printf("not enough arguments, usage: cryptofs <encdir> <mount>\n");
    return 1;
  }

  struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
  for(int i = 0; i < argc; i++) {
    if(i == 1) {
      crypto_dir = realpath(argv[i], NULL);
    } else {
      fuse_opt_add_arg(&args, argv[i]);
    }
  }

  printf("enter password: ");
  char *pw;
  size_t pwsize;
  ssize_t nread = _crypto_getpass(&pw, &pwsize, stdin);
  puts("");

  if(nread < 0){
    printf("%s\n", strerror(errno));
    return errno;
  }

  crypto_hash(key, (unsigned char *) pw, nread);
  memset(pw, 0, nread);
  free(pw);
  int ret = fuse_main(args.argc, args.argv, &crypto_ops, NULL);
  fuse_opt_free_args(&args);
  free(crypto_dir);
  return ret;
}
