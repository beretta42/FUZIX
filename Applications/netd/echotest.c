#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


char ibuf[512];
int fd;
struct sockaddr_in addr;
char rstr[17];

void printm( char *ptr, int len)
{
    while (len--){
	fprintf(stderr,"%02x ", *ptr++);
    }
    fprintf(stderr,"\n");
}

int main( int argc, char *argv[])
{
    int ret;
    addr.sin_port = htons(7);
    addr.sin_family = AF_INET;
    socklen_t l;

    fd = socket( AF_INET, SOCK_DGRAM, 0);
    if (fd < 0){
	perror("socket");
	exit(1);
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
	perror("bind");
	exit(1);
    }

    l = sizeof(addr);
    ret = recvfrom(fd, ibuf, 512, 0, (struct sockaddr *)&addr, &l);
    if (ret < 0){
	perror("read");
	exit(1);
    }

    printm( &addr, sizeof(addr) );
    fprintf(stderr,"UDP packet received! %s : %u %x\n", 
	    inet_ntop(AF_INET, &addr.sin_addr, rstr, 17),
	    addr.sin_port, &addr);
    exit(0);

}




