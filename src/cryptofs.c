#include "lib/tweetnacl.h"
#include <fuse.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>

static char crypto_dir[PATH_MAX];

static int crypto_getattr(){
  return 0;
}

static int crypto_truncate(){
  return 0;
}

static int crypto_mknod(const char * buf, mode_t mode, dev_t dev){
  return 0;
}

static int crypto_open(){
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
  .truncate = crypto_truncate,
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

  return fuse_main(argc, argv, &crypto_ops, NULL);
}