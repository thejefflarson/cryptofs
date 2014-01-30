#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[]){
  if(argc != 4) return -1;
  char b[atoi(argv[1])];
  memset(b, 0, atoi(argv[1]));
  printf("%i %i %s\n", atoi(argv[1]), atoi(argv[2]), argv[3]);

  int fd = open(argv[3], O_RDONLY);
  if(fd == -1) return errno;

  int res = pread(fd, b, atoi(argv[1]), atoi(argv[2]) );
  if(res == -1) {
    puts("error");
    return errno;
  }
  b[atoi(argv[1]) - 1] = '\0';

  printf("%s\n", b);

  return 0;
}
