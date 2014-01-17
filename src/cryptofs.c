#include "lib/tweetnacl.h"
#include <fuse.h>
#include <limits.h>
#include <sys/stat.h>

static crypto_dir[PATH_MAX];

static int crypto_mknod(){

  return 0;
}



static struct fuse_operations = {
  .getattr  = crypto_getattr,
  .truncate = crypto_truncate,
  .mknod    = crypto_mknod,
  .open     = crypto_open,
  .read     = crypto_read,
  .write    = crypto_write
};

int main(int argc, char *argv[]) {

}