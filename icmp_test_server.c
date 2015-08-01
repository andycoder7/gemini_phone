#include "mysocket.h"

int main(int argc, const char *argv[])
{
	int socketfd	= open_listenfd(1234);
	int fd;
	while((fd = accept(socketfd,NULL,0)) != -1)
		if(fork() != 0){
			puts("accept");
			short len,head;
			int addr;
			unsigned char buff[4000];
			while(recv(fd,&head,2,MSG_WAITALL) > 0){
				printf("recv head: %02x\n",head);
				recv(fd,&len,2,MSG_WAITALL);
				recv(fd,buff,len,MSG_WAITALL);
#define DEBUG
#ifdef DEBUG
				int n;
				printf("recv %d bytes:",len);
				for(n = 0;n < len;n++)
					printf("%2x ",buff[n]);
				putchar(10);
#endif
				if(buff[0x09] == 0x01 && buff[0x14] == 0x08){
					puts("ICMP(ping)");
					memcpy(&addr,buff+0x0c,4);
					memcpy(buff+0x0c,buff+0x10,4);
					memcpy(buff+0x10,&addr,4);
					buff[0x14] = 0x00;
					head = 0x4A04;		//little enfing
					send(fd,&head,2,0);
					send(fd,&len,2,0);
					send(fd,buff,len,0);
				}
			}
			perror("recv:");
			return 0;
		}
	return 0;
}
