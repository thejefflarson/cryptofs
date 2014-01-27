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
static int crypto_PADDING = crypto_secretbox_NONCEBYTES + (crypto_secretbox_ZEROBYTES - crypto_secretbox_BOXZEROBYTES);
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

  size_t num_blocks = st->st_size / block_size;
  size_t total_overhead = num_blocks * crypto_PADDING;
  if(st->st_size % block_size > 0)
    total_overhead += crypto_PADDING;
  st->st_size -= total_overhead;

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

  size_t red = 0;
  int idx = off / (block_size - crypto_PADDING);
  size_t delta = off % (block_size - crypto_PADDING);

  while(size > 0) {
    size_t bsize = size < block_size - crypto_PADDING ? size + crypto_PADDING : block_size;
    char block[size];
    int res = pread(inf->fh, block, bsize, block_size * idx) - crypto_PADDING;
    if(res == -1)
      return -errno;

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    memcpy(nonce, block, crypto_secretbox_NONCEBYTES);

    size_t csize = bsize - crypto_PADDING;

    unsigned char cpad[csize + crypto_secretbox_BOXZEROBYTES];
    memset(cpad, 0, csize);
    memcpy(cpad + crypto_secretbox_BOXZEROBYTES, block + crypto_secretbox_NONCEBYTES, csize);

    unsigned char mpad[csize + crypto_secretbox_BOXZEROBYTES];
    memset(mpad, 0, csize);

    int ruroh = crypto_secretbox_open(mpad, cpad, csize, nonce, key);
    if(ruroh == -1)
      return -ENXIO;

    memcpy(buf + red, mpad + delta + crypto_secretbox_ZEROBYTES, csize - delta);
    size -= res;
    red  += res;
    idx  += 1;
    off  += delta;
  }

  return red;
}

// We encrypt like GDBE each sector has a crypto_secretbox_NONCEBYTES-long
// nonce prepended to each sector. This is so confusing, and needs to be
// cleaned up.
static int crypto_write(const char *path, const char *buf, size_t size,
                        off_t off, struct fuse_file_info *inf){
  (void) path;

  size_t written = 0;
  int idx = off / (block_size - crypto_PADDING);

  while(size > 0) {
    int aligned = (off % (block_size - crypto_PADDING) == 0);


    size_t to_write = size < block_size - crypto_PADDING ? size + crypto_PADDING : block_size;
    unsigned char block[to_write];
    memset(block, 0, to_write);

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes(nonce, crypto_secretbox_NONCEBYTES);

    size_t msize = to_write - crypto_PADDING;
    unsigned char mpad[msize + crypto_secretbox_ZEROBYTES];
    unsigned char cpad[msize + crypto_secretbox_ZEROBYTES];
    memset(mpad, 0, msize + crypto_secretbox_ZEROBYTES);
    memset(cpad, 0, msize + crypto_secretbox_ZEROBYTES);

    if(aligned) {
      // we are writing a full block, or the last partial block
      memcpy(mpad + crypto_secretbox_ZEROBYTES, buf + written, msize);
    } else {
      // we are at a first partial block, we have to read the rest of the data
      // and append the new stuff to our buffer.

      size_t leftovers = off % (block_size - crypto_PADDING);
      off_t  block_off = idx * (block_size - crypto_PADDING);

      char b[leftovers];
      int res = crypto_read(path, b, leftovers, block_off, inf);
      if(res < 0) return res;
      memcpy(mpad + crypto_secretbox_ZEROBYTES, b, leftovers);
      memcpy(mpad + crypto_secretbox_ZEROBYTES + leftovers, buf, msize);
      msize += res;
      to_write += res;
    }

    int ohno = crypto_secretbox(cpad, mpad, msize + crypto_secretbox_ZEROBYTES, nonce, key);
    if(ohno < 0) return -ENXIO;

    memcpy(block, nonce, crypto_secretbox_NONCEBYTES);
    memcpy(block + crypto_secretbox_NONCEBYTES, cpad + crypto_secretbox_BOXZEROBYTES, msize + crypto_secretbox_BOXZEROBYTES);

    int res = pwrite(inf->fh, block, to_write, block_size * idx) - crypto_PADDING;
    if(res == -1)
      return -errno;
    written += res;
    idx     += 1;
    size    -= res;
    off     += res;
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

// from: http://www.gnu.org/software/libc/manual/html_node/getpass.html
ssize_t _crypto_getpass(char **lineptr, size_t *n, FILE *stream){
  struct termios old, new;
  int nread = 0;

  if(isatty(fileno(stream))){
    /* Turn echoing off and fail if we can't. */
    if (tcgetattr(fileno(stream), &old) != 0)
      return -1;
    new = old;
    new.c_lflag &= ~ECHO;
    if (tcsetattr(fileno(stream), TCSAFLUSH, &new) != 0)
      return -1;
  }

  /* Read the password. */
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
    if (i == 1)
      crypto_dir = realpath(argv[i], NULL);
    else
      fuse_opt_add_arg(&args, argv[i]);
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
