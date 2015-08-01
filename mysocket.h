#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<string.h>

int open_clientfd(char *hostname,int port)
{
	int clientfd;
	struct hostent *hp;
	struct sockaddr_in serveraddr;

	if((clientfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
		return -1;

	if((hp = gethostbyname(hostname)) == NULL)
		return -2;
	bzero((void *)&serveraddr,sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((void *)hp->h_addr_list[0],
	      (void *)&serveraddr.sin_addr.s_addr,hp->h_length);
	serveraddr.sin_port = htons((unsigned short)port);
	if(connect(clientfd,(struct sockaddr *) &serveraddr,sizeof(serveraddr)) < 0)
		return -1;
	return clientfd;
}

int open_listenfd(int port)
{
	int listenfd,optval = 1;
	struct sockaddr_in serveraddr;

	if((listenfd = socket(AF_INET,SOCK_STREAM,0)) < 0)
		return -1;

	/*Eliminates "Address already in use" error from blind*/
	if(setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,
		      (const void *)&optval,sizeof(int)) < 0)
		return -1;

	bzero((char *) &serveraddr,sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)port);
	if(bind(listenfd,(struct sockaddr *)&serveraddr,sizeof(serveraddr)) < 0)
			return -1;
	if(listen(listenfd,SOMAXCONN) < 0)
		return -1;
	return listenfd;


}
