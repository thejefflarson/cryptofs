#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>

#include "lib/tweetnacl.h"
#include <fuse.h>

static char *crypto_dir;
static unsigned char key[crypto_secretbox_KEYBYTES];

void _crypto_path(char *crypto_path, char *cpath){

}

static int crypto_getattr(){
  return 0;
}

static int crypto_mknod(const char * buf, mode_t mode, dev_t dev){
  return 0;
}

static int crypto_open(const char *path, struct fuse_file_info *inf){
  char *cpath = malloc(strnlen(path, PATH_MAX));
  _crypto_path(cpath, path);
  uint64_t fh = open(cpath, inf->flags);
  free(cpath);

  if(fh == -1)
    return -errno;

  inf->fh = fh;
  return 0;
}

static int crypto_read(){
  return 0;
}

static int crypto_write(){
  return 0;
}

static struct fuse_operations crypto_ops = {
  .getattr  = crypto_getattr,
  .mknod    = crypto_mknod,
  .open     = crypto_open,
  .read     = crypto_read,
  .write    = crypto_write
};

int main(int argc, char *argv[]) {
  if(argc != 3) {
    printf("not enough arguments, usage: cryptofs <encdir> <mount>");
    return 1;
  }
  crypto_dir = argv[1];
  char *pw;
  pw = getpass("enter password: ");
  crypto_hash(key, (unsigned char *) pw, strnlen(pw, _PASSWORD_LEN));
  return fuse_main(argc, argv, &crypto_ops, NULL);
}