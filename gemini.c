/*
 * Copyright (c) 2014 SerComm Corporation.
 * All Rights Reserved.
 * SerComm Corporation reserves the right to make changes to this document
 * without notice.
 * SerComm Corporation makes no warranty, representation or guarantee
 * regarding the suitability of its products for any particular purpose. SerComm
 * Corporation assumes no liability arising out of the application or use of any
 * product or circuit. SerComm Corporation specifically disclaims any and all
 * liability, including without limitation consequential or incidental damages;
 * neither does it convey any license under its patent rights, nor the rights of
 * others.
 * Partners of Sercomm have rights to read and change this document before
 * Project submission.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/icmp.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_tun.h>
#include <linux/ip.h>
#include <linux/wireless.h>
#include <net/route.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/tcp.h>

#define DEBUG
#define DEB_INIT
#define DEB_DETAIL
#define UIDEBUG
#define ERR

#define MAC_HEAD	14
#define MAX_IF		10
#define MAX_BUFF	2048
#define MAX_SEQ     10000
#define SEQ_TOR     1000
#define BUF_LEN     2000


/*
 * port number with gemini module
 */
#define GTG 44444//3g port number
#define GWIFI 5389//wifi port number
#define APP 20158
/*
 * ip get from gemini module
 */

int tgrecv = 0;
int wifirecv = 0;
int wifisend = 0;
int tgsend = 0;

int GMN_FLAG;
int AUTO_FLAG;
char RATE_FLAG[10];
int CONF_FLAG;
int READY_FLAG;

FILE *gmn_log;
FILE *gmn_file;
/*
int pthread_gmn_send=0;
int pthread_gmn_recv=0;
int pthread_gmn_buff2tun=0;
int pthread_mnt=0;
*/

struct sockaddr_in wifiip;
struct sockaddr_in tgip;
struct sockaddr_in serveraddr;
struct ifreq ifr;

/*
 * add host route entry before the server_for_gemini function start,
 * this is because the default route will all be deleted,
 * so one must add 3g and wifi host route to determine
 * which interface the packet will transferm through
 */
struct rtentry hostrt;

/*
 * 3g and wifi socket file descriptions,
 * all initialized with 0;
 */
int tgfd = 0;
int wififd = 0;
int wifitrfd = 0;
int tunfd = 0;
int fd;
int initflag = 0;


char app_msg[128];
char msg[128];

int gmnok = 0;
//int init = 0;

//pthread_t dae;
pthread_t tgs=0;
pthread_t tgr=0;
//pthread_t initfl;
pthread_t gmn=0;
pthread_t b2t=0;
pthread_t mnt=0;
pthread_t env=0;
pthread_t ui=0;
//pthread_t gmninit;
void *tgs_join;
void *tgr_join;
void *b2t_join;


int lasttg   = 0;
int lastwifi = 0;
int lastn = 0;
int firn = 0;

pthread_mutex_t m_env;
pthread_cond_t  c_env;
pthread_mutex_t m_gmn;
pthread_cond_t  c_gmn;
pthread_mutex_t m_ui;
pthread_cond_t  c_ui;
pthread_mutex_t *MTX_RECV;
pthread_cond_t *WAIT_RECV; 


/*
 * flags about whether function has run successfully
 */
int double_ch_flag_gmn = 0; //1 if gemini return s to ue through wifi socket
//int double_ch_flag_app = 1; //1 if app turn on wifi interface
int autoshunt = 0; //1 if  automatically set the shunt persentage

/*
 * phone ip address, send to gemini through wifi interface
 */
unsigned char ip[20] = "\0";

typedef unsigned char mac_addr[6];
mac_addr SOURCE_MAC, GATEWAY_MAC;
struct rtentry OLD_DEFAULT_ROUTE;
char INTERFACE_NAME[64];
unsigned char DATABUFF[MAX_SEQ][MAX_BUFF];

/*
 * self_defined protocol head
 */
struct gemini_head
{
	uint8_t headlen;
	uint8_t dir;
	uint8_t type;
	void *context;
	void *data;
};

/*
 * self_defined protocol head specially for the type 21
 */
struct gemini_head21
{
	uint8_t headlen;
	uint8_t dir;
	uint8_t type;
	int32_t context;
	void *data;
};

/*
 * store the tunnel 3g and wifi socket file description
 */
struct sock
{
	int vsock; //tunnel "tun0"
	int rsock[4]; //3g, wifi initial ,wifi data transfer and 0 for the end of the array
};
struct sock socks = { .vsock = 0, .rsock = { 0, 0, 0, 0 } };


typedef struct{
        int GMN_FLAG;
        int AUTO_FLAG;
        char RATE_FLAG[10];
}GMN_STRUCT;
GMN_STRUCT gmn_struct;

/*
 * fatch phone ip address
 * char* store the ip address
 * return 0 for success, -1 for failure
 */
int get_3g_ip(char* outip)
{
	int i = 0;
	int sockfd;
	struct ifconf ifconf;
	struct ifreq *ifreq;
	char buf[512];
	char* ip;
	/*
	 * initial ifconf
	 */
	ifconf.ifc_len = 512;
	ifconf.ifc_buf = buf;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		return -1;
	}
	/*
	 * fatch all the possible interface information
	 */
	ioctl(sockfd, SIOCGIFCONF, &ifconf);
	close(sockfd);

	/*
	 * fatch the ip address one by one
	 */
	ifreq = (struct ifreq*) buf;
	for (i = (ifconf.ifc_len / sizeof(struct ifreq)); i > 0; i--)
	{
		ip = inet_ntoa(((struct sockaddr_in*) &(ifreq->ifr_addr))->sin_addr);
		/*
		 *ignore 127.0.0.1
		 */
		if (strcmp(ip, "127.0.0.1") == 0)
		{
			ifreq++;
			continue;
		}
		/*
		 * ignore other interface but 3g
		 */
		if (strcmp(ifreq->ifr_ifrn.ifrn_name, "rmnet0") != 0)
		{
			ifreq++;
			continue;
		}
		strcpy(outip, ip);
		return 1;
	}
	return 0;
}
int get_wifi_ip(char* outip)
{
	int i = 0;
	int sockfd;
	struct ifconf ifconf;
	struct ifreq *ifreq;
	char buf[512];
	char* ip;
	/*
	 * initial ifconf
	 */
	ifconf.ifc_len = 512;
	ifconf.ifc_buf = buf;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		return -1;
	}
	/*
	 * fatch all the possible interface information
	 */
	ioctl(sockfd, SIOCGIFCONF, &ifconf);
	close(sockfd);

	/*
	 * fatch the ip address one by one
	 */
	ifreq = (struct ifreq*) buf;
	for (i = (ifconf.ifc_len / sizeof(struct ifreq)); i > 0; i--)
	{
		ip = inet_ntoa(((struct sockaddr_in*) &(ifreq->ifr_addr))->sin_addr);
		/*
		 *ignore 127.0.0.1
		 */
		if (strcmp(ip, "127.0.0.1") == 0)
		{
			ifreq++;
			continue;
		}
		/*
		 * ignore other interface but 3g
		 */
		if (strcmp(ifreq->ifr_ifrn.ifrn_name, "wlan0") != 0)
		{
			ifreq++;
			continue;
		}
		strcpy(outip, ip);
		return 1;
	}
	return 0;
}


int change_3g_ip()
{
	int sockfd;
	struct ifreq ifr;
	struct sockaddr_in sin;
	char inter[16] = "rmnet0\0";

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		return -1;
	}

	memcpy(&(ifr.ifr_ifrn.ifrn_name), (void *) inter, strlen(inter));
	ioctl(sockfd, SIOCGIFFLAGS, &ifr);

	memset(&sin, 0, sizeof(struct sockaddr));
	sin.sin_family = AF_INET;
	inet_aton("192.168.1.30", &sin.sin_addr);
	memcpy(&ifr.ifr_addr, &sin, sizeof(struct sockaddr));

	ioctl(sockfd, SIOCSIFADDR, &ifr);
	return 0;
}

/*
 * get wifi ip address and port number from getmini through 3g interface
 * 3g port number:44444
 * after which, the 3g interface is used for data transfer
 * warning: the close function is called in route_recover, don't forget otherwise,
 */
void server_for_gemini()
{
	int server_sockfd;
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	struct sockaddr_in set_address;
	socklen_t server_len;
	unsigned char buffer[2048];
	unsigned char cont[2048];

	/*
	 * the information send to gemini (used for start signal)
	 */
	unsigned char start_consig[32] = { 0 };
	unsigned char ip[32]; //wifi ip address
	unsigned char port[8]; //wifi ip port number
	start_consig[0] = 2; //the start signal is "200" according to self_defined protocol
	uint8_t headlen = 0, direction = 0, type = 0;
	uint8_t context_len = 0;
	uint8_t i = 0, j = 0;
	socklen_t alen;

	memset((void *) buffer, '\0', sizeof(buffer));
	memset((void *) ip, '\0', sizeof(ip));
	memset((void *) port, '\0', sizeof(port));
	memset(&server_address, 0, sizeof(server_address));
	alen = sizeof(client_address);

	if ((server_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("error occurred in 3g socket creat with gemini");
		exit(-1);
	}
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(GTG);
	server_len = sizeof(server_address);

	set_address.sin_family = AF_INET;
	set_address.sin_addr.s_addr = inet_addr("192.168.1.254");
	set_address.sin_port = htons(GTG);

	if (bind(server_sockfd, (struct sockaddr *) &server_address, server_len)
			< 0)
	{
		perror("error occurred in 3g socket bind");
		exit(-1);
	}

	tgfd = server_sockfd;

#ifdef DEB_INIT
	printf("[INIT]: 3g socket established\n");
#endif

	/*
	 * confirm information about the start of 3g socket with gemini
	 */
	sendto(server_sockfd, (void *) start_consig, sizeof(start_consig), 0,
			(struct sockaddr*) &set_address, sizeof(set_address));
#ifdef DEB_INIT
	printf("[INIT]: sent beginning string 200\n");
#endif

	/*
	 * udp address information
	 */
	serveraddr.sin_family = set_address.sin_family;
	serveraddr.sin_addr.s_addr = set_address.sin_addr.s_addr;
	serveraddr.sin_port = set_address.sin_port;

#ifdef DEB_INIT
	printf("[INIT]: 3g socket serverip:%s\n", inet_ntoa(serveraddr.sin_addr));
#endif

	/*
	 * fatch wifiip through 3g interface
	 */
	recvfrom(server_sockfd, (void *) buffer, sizeof(buffer), 0,
			(struct sockaddr*) &client_address, &alen);
#ifdef DEB_INIT
	printf("[INIT]: received wifi infomation from 3g\n");
#endif


	headlen = buffer[0];
	direction = buffer[1];
	type = buffer[2];
	context_len = headlen - 2;

	int ss;
	for (ss = 0; ss < context_len; ss++)
	{
		cont[ss] = buffer[ss + 3];
	}

#ifdef DEB_INIT
	printf("[INIT]: [INFO FROM GMN] headlen=%d\n", headlen);
	printf("[INIT]: [INFO FROM GMN] direction=%d\n", direction);
	printf("[INIT]: [INFO FROM GMN] type=%d\n", type);
	printf("[INIT]: [INFO FROM GMN] content len=%d\n", context_len);
#endif

	if (type == 1) //if type field is 1, then establish 3G udp socket interface, else do nothing
	{
		int len = context_len;

#ifdef DEB_INIT
        printf("[INIT]: [INFO FROM GMN] content=%s\n", cont);
#endif

		for (i = 0; cont[i] != ':'; i++)
		{
			ip[i] = cont[i];
		}
		for (j = i + 1; j < context_len; j++)
		{
			port[j - i - 1] = cont[j];
		}

#ifdef DEB_INIT
		printf("[INIT]: wifi ip=%s, wifi port=%s\n", ip, port);
#endif

		/*
		 * get wifi address and port number
		 */
		wifiip.sin_addr.s_addr = inet_addr((char *) ip);


#ifdef DEB_INIT
		printf("[INIT]: server process ended!\n");
#endif

	}
	else
	{
#ifdef ERR
		printf("[ERR]: get wifi info failed\n");
#endif
        exit(1);
	}
}

/*
 * client for gemini, use wifi interface to connect gemini
 * after connection, the wifi interface is used for data transfer
 * warning: the close function is called in route_recover, don't forget otherwise,
 */
void client_for_gemini()
{
	int sockfd;
	int len;
	struct sockaddr_in address;
	int result;
	unsigned char buff[16];
	char ch;
	unsigned char confirm[16];

	//the arguments of new created wifi data transfer socket
	struct sockaddr_in server_address;
	socklen_t server_len;
	unsigned char buffer[2048];

	/*
	 * the information send to gemini (used for start signal)
	 */
	uint8_t headlen = 0, direction = 0, type = 0;
	uint8_t context_len = 0;
	uint8_t i = 0, j = 0;

	memset((void *) buffer, '\0', sizeof(buffer));
	memset(&server_address, 0, sizeof(server_address));

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("error occurred in wifi socket creat with gemini:");
		exit(-1);
	}

	wififd = sockfd;
	bzero((char *) &address, sizeof(address));
	bzero((char *) &confirm, sizeof(confirm));
	bzero((char *) &buff, sizeof(buff));

	address.sin_family = AF_INET;
	address.sin_port = htons(GWIFI);
	address.sin_addr.s_addr = wifiip.sin_addr.s_addr;
	len = sizeof(address);

	result = connect(sockfd, (struct sockaddr *) &address, len);
	if (result == -1)
	{
		perror("error in wifi socket connect with gemini:");
		exit(-1);
	}

	/*
	 * confirm ip with gemini
	 * note that the first several bytes are self_defined protocol head
	 */
	confirm[0] = 2 + strlen((const char *) ip);
	confirm[1] = 2;
	confirm[2] = 2;
	memcpy((char *) &confirm[3], (const char*) ip, sizeof(ip));
	if (send(sockfd, confirm, sizeof(confirm), 0) == -1)
	{
		perror("confirm 3g ip address with gemini error");
		exit(-1);
	}

#ifdef DEB_INIT
	printf("[INIT]: confirm 3g ip addr through wifi interface ended\n");
	printf("[INIT]: 3g ip is:%s\n",confirm+3);
#endif

	memset(buff, '\0', sizeof(buff));
	/*
	 * ignore the self_defined head and fatch the flag directly
	 */
	read(sockfd, buff, 3);
	read(sockfd, &ch, 1);

#ifdef DEB_INIT
	printf("[INIT]: [wifi initial] buff:%d %d %d\n", buff[0], buff[1], buff[2]);
	printf("[INIT]: [wifi initial] ch:%c\n", ch);
#endif

	if (ch == 'S' || ch == 's')
	{
		double_ch_flag_gmn = 1;
		//here initial a new udp socket to transfer data
		//terrible terrible damege!!
		//remember in thread recvwifi the recvfrom() argument sockfd should change to
		//wifi_trans_sockfd
		//not wififd!!!!
		int wifi_trans_sockfd;
		if ((wifi_trans_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		{
			perror(
					"error occurred in wifi data transfer socket creat with gemini");
			exit(-1);
		}
		wifitrfd = wifi_trans_sockfd;

#ifdef DEB_INIT
		printf("[INIT]: wifi data transfer socket created!\n");
#endif
		server_address.sin_family = AF_INET;
		server_address.sin_addr.s_addr = htonl(INADDR_ANY);
		server_address.sin_port = htons(44445);
		server_len = sizeof(server_address);

		if (bind(wifi_trans_sockfd, (struct sockaddr *) &server_address,
				server_len) < 0)
		{
			perror("error occurred in wifi data transfer socket bind");
			exit(-1);
		}

	} else {
#ifdef ERR
		printf("[ERR]: double channel flag response from wifi interface is wrong\n");
#endif
		close(sockfd); //don't enable dual channel
		exit(-1);
	}

#ifdef DEB_INIT
	printf("[wifi initial]client process ended!\n");
#endif

}

/*
 * decide the percentage of data that wifi and 3g interface should offload respectively
 * the information is writen into a global argument
 * return 0 for self_defined percentage, 1 for auto
 * \param serverfd [in] socket file discription of 3g interface
 */
int shunt(int serverfd)
{

	if (autoshunt == 1)
	{
		return 1;
	}
	else
	{
		uint8_t *data;
		//char per[16];
		//bzero((char *) per, sizeof(per));
		char temp[16] = "1:2\0";
		//memcpy((char *) per, (const char*) temp, strlen(temp));

		struct gemini_head hd1;
		/*
		 * deal with self_defined head
		 */
		hd1.headlen = 2 + strlen(temp) + 1;
		hd1.dir = 0;
		hd1.type = 11;
		data = (uint8_t *) malloc(hd1.headlen + 1);
		//hd1.context = (void *) per;

		//write(serverfd, &hd1, sizeof(hd1));
		data[0] = hd1.headlen;
		data[1] = hd1.dir;
		data[2] = hd1.type;
		memcpy(&data[3], temp, strlen(temp) + 1);
		sendto(serverfd, data, hd1.headlen + 1, 0,
				(struct sockaddr *) &serveraddr, sizeof(serveraddr));
		free(data);
#ifdef DEBUG
        printf("the shunt is : %s\n", temp);
		sleep(3);
#endif
		return 0;
	}
}

int shunt_auto(int serverfd)
{

	if (autoshunt == 1)
	{
		return 1;
	}
	else
	{
		uint8_t *data;
		//char per[16];
		//bzero((char *) per, sizeof(per));
		char temp[16] = "1:1\0";
		//memcpy((char *) per, (const char*) temp, strlen(temp));

		struct gemini_head hd1;
		/*
		 * deal with self_defined head
		 */
		hd1.headlen = 2 + strlen(temp) + 1;
		hd1.dir = 0;
		hd1.type = 11;
		data = (uint8_t *) malloc(hd1.headlen + 1);
		//hd1.context = (void *) per;

		//write(serverfd, &hd1, sizeof(hd1));
		data[0] = hd1.headlen;
		data[1] = hd1.dir;
		data[2] = hd1.type;
		memcpy(&data[3], temp, strlen(temp) + 1);
		sendto(serverfd, data, hd1.headlen + 1, 0,
				(struct sockaddr *) &serveraddr, sizeof(serveraddr));
		free(data);
#ifdef DEBUG
		printf("shunt percentage auto ###############\n");

		sleep(3);
#endif
		return 0;
	}
}

/*
 * get the signal strength of wifi interface
 */

int interface_up(char *interface_name)
{
	int s;

	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("interface_up socket creat error");
		return -1;
	}

	struct ifreq ifr;
	strcpy(ifr.ifr_name, interface_name);

	short flag;
	flag = IFF_UP;
	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0)
	{
		perror("interface_up ioctl error");
		return -1;
	}

	ifr.ifr_ifru.ifru_flags |= flag;

	if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0)
	{
		perror("interface_up ioctl error");
		return -1;
	}

	return 0;

}

int set_ipaddr(char *interface_name, char *ip)
{
	int s;

	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("set_ipaddr error up");
		return -1;
	}

	struct ifreq ifr;
	strcpy(ifr.ifr_name, interface_name);

	struct sockaddr_in addr;
	bzero(&addr, sizeof(struct sockaddr_in));
	addr.sin_family = PF_INET;
	inet_aton(ip, &addr.sin_addr);

	memcpy(&ifr.ifr_ifru.ifru_addr, &addr, sizeof(struct sockaddr_in));

	if (ioctl(s, SIOCSIFADDR, &ifr) < 0)
	{
		perror("set_ipaddr error");
		return -1;
	}

	return 0;
}

int tun_create(char *dev, int flags)
{
	struct ifreq ifr;
	int fd, err;

	/*
	 * on PC /dev/net/tun
	 * on android /dev/tun
	 * uncertain...
	 */
	if ((fd = open("/dev/tun", O_RDWR)) < 0)
	{
		perror("tun_create error");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags |= flags;

	if (*dev != '\0')
	{
		memcpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0)
	{
		perror("tun_create ioctl error");
		close(fd);
		return -1;
	}

	strcpy(dev, ifr.ifr_name);
	tunfd = fd;
	return fd;
}

/*
 * Get route information from file
 */
int get_route(FILE * fp, struct rtentry * rt)
{
	unsigned long dst, gateway, mask;
	int flgs, ref, use, metric, mtu, win, ir;
	int res;
	struct sockaddr_in * p;

	if (fp == NULL)
		return -1;
	memset(INTERFACE_NAME, 0, 64);
	memset(rt, 0, sizeof(struct rtentry));
	res = fscanf(fp, "%63s%lx%lx%X%d%d%d%lx%d%d%d\n", INTERFACE_NAME, &dst,
			&gateway, &flgs, &ref, &use, &metric, &mask, &mtu, &win, &ir);
	if (res != 11)
		return -2;

	p = (struct sockaddr_in *) &(rt->rt_dst);
	p->sin_family = AF_INET;
	p->sin_addr.s_addr = dst;
	p = (struct sockaddr_in *) &(rt->rt_gateway);
	p->sin_family = AF_INET;
	p->sin_addr.s_addr = gateway;
	p = (struct sockaddr_in *) &(rt->rt_genmask);
	p->sin_family = AF_INET;
	p->sin_addr.s_addr = mask;
	rt->rt_dev = INTERFACE_NAME;
	rt->rt_flags = flgs;
	//rt->rt_refcnt = ref;
	//rt->rt_use = use;
	rt->rt_pad2 = ref;
	rt->rt_pad3 = use;
	rt->rt_metric = metric;
	rt->rt_mss = mtu;
	rt->rt_window = win;
	rt->rt_irtt = ir;

	return 0;
}

int add_route(struct rtentry *rt)
{
	int skfd;
	int ret;
	ret = 0;
	skfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (ioctl(skfd, SIOCADDRT, rt) < 0)
	{
		perror("Error route add");
		ret = -1;
	}

	close(skfd);
	return ret;
}

int del_route(struct rtentry *rt)
{
	int skfd;
	int ret;
	ret = 0;
	skfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (ioctl(skfd, SIOCDELRT, rt) < 0)
	{
		perror("Error route del ");
		ret = -1;
	}

	close(skfd);
	return ret;
}

int settghostrt()
{
	struct sockaddr_in dst;
	struct sockaddr_in genmask;

	memset(&hostrt, 0, sizeof(hostrt));

	genmask.sin_family = PF_INET;
	genmask.sin_addr.s_addr = inet_addr("255.255.255.255");

	bzero(&dst, sizeof(struct sockaddr_in));

	dst.sin_addr.s_addr = inet_addr("192.168.1.254");
	dst.sin_family = PF_INET;

	hostrt.rt_dst = *(struct sockaddr*) &dst;
	hostrt.rt_genmask = *(struct sockaddr*) &genmask;
	hostrt.rt_flags = RTF_UP | RTF_HOST;
	hostrt.rt_dev = "rmnet0";
	add_route(&hostrt);
	return 1;
}

int setwifihostrt()
{
	struct sockaddr_in dst;
	struct sockaddr_in genmask;

	memset(&hostrt, 0, sizeof(hostrt));

	genmask.sin_family = PF_INET;
	genmask.sin_addr.s_addr = inet_addr("255.255.255.255");

	bzero(&dst, sizeof(struct sockaddr_in));

	dst.sin_addr.s_addr = inet_addr("10.10.10.253");
	dst.sin_family = PF_INET;
	hostrt.rt_dst = *(struct sockaddr*) &dst;
	hostrt.rt_genmask = *(struct sockaddr*) &genmask;

	hostrt.rt_flags = RTF_UP | RTF_HOST;
	hostrt.rt_dev = "wlan0";
	add_route(&hostrt);
	return 1;
}

int get_default_route(struct rtentry * rt)
{
	FILE * source;
	int is_old_route;
	struct sockaddr_in * p, *q;
	if ((source = fopen("/proc/net/route", "r")) == NULL)
		return -1;
	fscanf(source, "%*[^\n]\n"); //skip the first line
	is_old_route = 0;
	while (get_route(source, rt) == 0) //read the default route
	{
		p = (struct sockaddr_in *) &(rt->rt_dst);
		q = (struct sockaddr_in *) &(rt->rt_genmask);
		if (p->sin_addr.s_addr == inet_addr("0.0.0.0")
				&& q->sin_addr.s_addr == inet_addr("0.0.0.0"))
		{
			is_old_route = 1;
			break;
		}
	}
	fclose(source);
	if (is_old_route == 1)
		return 0;
	else
		return -2;
}

int route_change(char * interface_name)
{
	//int skfd;
	struct rtentry rt;

	struct sockaddr_in dst;
	struct sockaddr_in dst_3g;

	//struct sockaddr_in gw;
	struct sockaddr_in genmask;
	struct sockaddr_in genmask_3g;

	memset(&rt, 0, sizeof(rt));

	genmask.sin_family = PF_INET;
	genmask.sin_addr.s_addr = inet_addr("0.0.0.0");

	bzero(&dst, sizeof(struct sockaddr_in));
	dst.sin_addr.s_addr = inet_addr("0.0.0.0");

	dst.sin_family = PF_INET;
	rt.rt_dst = *(struct sockaddr*) &dst;
	rt.rt_genmask = *(struct sockaddr*) &genmask;

	del_route(&rt);
	//del_route(&rt);
	rt.rt_flags = RTF_UP;
	rt.rt_dev = interface_name;
	add_route(&rt);

	dst_3g.sin_family = PF_INET;
	dst_3g.sin_addr.s_addr = inet_addr("192.168.1.0");
	rt.rt_dst = *(struct sockaddr*) &dst_3g;
	genmask_3g.sin_family = PF_INET;
	genmask_3g.sin_addr.s_addr = inet_addr("255.255.255.252");
	rt.rt_genmask = *(struct sockaddr*) &genmask_3g;
	//del_route(&rt);
	return 0;
}



int int2str(char *str, int num)
{
    int i = 0;
    int t = num;
    for(i = 0; t; i++, t = t/10);
    str[i] = 0;
    t = i;
    while(i) {
        str[--i] = num%10 +'0';
        num = num/10;
    }
    return t;
}



void * buff2tun0_process(void *p)
{
    int vsock;
    int32_t len = 0;
    struct sock* arg = NULL;
    arg = (struct sock*) p;
    vsock = arg->vsock;
    //slepp(1);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while (1)
    {
        //lasttg >= firn
        if(lasttg != firn)
        {
            if(DATABUFF[firn][0])
            {
                len = *((uint32_t *)(DATABUFF[firn]+3));
                write(vsock, (void *)(DATABUFF[firn]+9), len-2);
                DATABUFF[firn][0] = 0;
#ifdef DEB_DETAIL
                fprintf(gmn_log,"upload package, which seq = %d, firn = %d, lasttg = %d\n",
                        *(uint16_t*)(DATABUFF[firn]+7), firn, lasttg );
#endif
            } else {
#ifdef DEBUG
                fprintf(gmn_log,"lost data package,firn=%d,lasttg=%d\n",firn,lasttg);
#endif
            }
            firn = (firn + 1) % MAX_SEQ;
        } else {
#ifdef DEBUG
                fprintf(gmn_log,"waitting recv package,firn=%d,lasttg=%d\n",firn,lasttg);
#endif
            pthread_mutex_lock(MTX_RECV);
            pthread_cond_wait(WAIT_RECV, MTX_RECV);
            pthread_mutex_unlock(MTX_RECV);
        }
    }
    return (void *) 0;
}


void * recv_process(void *p)
{
	int vsock;
	int *rsock;
	struct sock* arg = NULL;
	arg = (struct sock*) p;
	vsock = arg->vsock;
	rsock = arg->rsock;

    MTX_RECV = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    WAIT_RECV = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    pthread_mutex_init(MTX_RECV,NULL);
    pthread_cond_init(WAIT_RECV,NULL);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	unsigned char buff[MAX_BUFF];
	struct epoll_event event;
	struct epoll_event events[MAX_IF];
	int epfd = epoll_create(MAX_IF);
    int epfds = 0;
    int i;
    int seq = 0;
	int32_t len = 0;
    int lasttemp = 0;

    tgrecv = 0;
    wifirecv = 0;

	socklen_t alen;
	alen = sizeof(struct sockaddr_in);

	event.events = EPOLLIN;
    event.data.fd = rsock[0];
    epoll_ctl(epfd, EPOLL_CTL_ADD, rsock[0], &event);
    event.data.fd = rsock[2];
    epoll_ctl(epfd, EPOLL_CTL_ADD, rsock[2], &event);
	//use epoll for a lockless mutli-receive
	while ((epfds = epoll_wait(epfd, events, 2, -1)) != -1)
	{
        for(i = 0; i < epfds; i++) {
            if (!(events[i].events & EPOLLIN) )
            {
                fprintf(gmn_log,"epoll_wait");
                continue;
            }

            len = recvfrom(events[i].data.fd, (void *) buff, MAX_BUFF, 0,NULL,NULL);
            if(events[i].data.fd == rsock[0])
            {
                tgrecv++;
            }
            else
            {
                wifirecv++;
            }
            seq = *(uint16_t*)(buff+7);
            if( ((seq<firn) || (seq>(MAX_SEQ-SEQ_TOR) && firn<SEQ_TOR)) && !(seq<SEQ_TOR && firn>(MAX_SEQ-SEQ_TOR)) ) {
#ifdef DEB_DETAIL
                fprintf(gmn_log,"get lost package from fd %d, which seq = %d \n", events[i].data.fd, seq);
#endif
                write(vsock, (void *)(buff+9), len-9);
            } else {
#ifdef DEB_DETAIL
                fprintf(gmn_log,"get new package from fd %d, which seq = %d \n", events[i].data.fd, seq);
#endif
                memcpy((void *) (&DATABUFF[seq]), buff, (size_t) len);

                if(events[i].data.fd == rsock[0]) {
                    if((seq>(MAX_SEQ-SEQ_TOR) && lasttg<SEQ_TOR) || (lasttg>(MAX_SEQ-SEQ_TOR) && seq<SEQ_TOR))
                        lasttg = lasttg>seq?seq:lasttg; // get the little one
                    else
                        lasttg = lasttg<seq?seq:lasttg; // get the bigger one
                } 

                // lastn get the slow one
                if(firn != lasttg)
                    pthread_cond_signal(WAIT_RECV);
            }
        } 
    }
#ifdef ERR
    fprintf(gmn_log,"[ERR]: epoll failed, and stop receiving\n");
    pthread_exit((void *)-1);
#endif
	return (void *) 0;
}
/*
 * determine whether to send data through 3g or wifi interface,
 *
 */
void * send_process(void *p)
{
    int vsock;
    int *rsock;
    int tgflag = 1; //mark if send through 3G channel
    struct sock* arg = NULL;
    arg = (struct sock*) p;

    tgsend = 0;
    wifisend = 0;

    vsock = arg->vsock;
    rsock = arg->rsock;
    uint32_t len;
    char buff[MAX_BUFF];
    uint8_t selfhd[2048];

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    bzero((void *) selfhd, sizeof(selfhd));
    int count;
    while (1)
    {
        if (tgflag == 1) //send through 3g interface
        {
            //tgflag = 0;
            bzero((void *) buff, sizeof(buff));
            len = read(vsock, buff, MAX_BUFF);
            if (len == -1)
            {
                fprintf(gmn_log,"3g read error\n");
                pthread_exit((void *)-1);
            }

            bzero((void *) selfhd, sizeof(selfhd));
            selfhd[0] = 6;
            selfhd[1] = 0;
            selfhd[2] = 21;
            uint32_t *hd3 = (uint32_t *) (&selfhd[3]);
            *hd3 = len;
            memcpy(&selfhd[7], buff, len);

            /*
             * head lenth: ?
             * direction: 0 3g->gemini
             * type: 21 data to IUH
             */
            //self-defined protocol head
            sendto(rsock[0], selfhd, 7 + len, 0,
                    (struct sockaddr *) &serveraddr, sizeof(serveraddr));
            tgsend++;

        }
        else //send through wifi channel
        {
            //tgflag = 1; //change the flag to send through 3g interface
            bzero((void *) buff, sizeof(buff));
            len = read(vsock, buff, MAX_BUFF);
            {
                if (len == -1)
                {
                    fprintf(gmn_log,"3g read error\n");
                    pthread_exit((void *)-1);
                }
                bzero((void *) selfhd, sizeof(selfhd));
                selfhd[0] = 6;
                selfhd[1] = 2;
                selfhd[2] = 21;
                uint32_t *hd3 = (uint32_t *) (&selfhd[3]);
                *hd3 = (len);
                memcpy(&selfhd[7], buff, len);

                /*
                 * head lenth: 6
                 * direction: 0 3g->gemini
                 * type: 21 data to IUH
                 */
                //self-defined protocol head
                send(rsock[1], selfhd, 7 + len, 0);
                wifisend++;
            }
        }
    }
    return (void *) 0;
}

/*
int monitor()
{
	ifconf.ifc_len = 512;
	ifconf.ifc_buf = buf;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		return -1;
	}
	ioctl(sockfd, SIOCGIFCONF, &ifconf);
	close(sockfd);

	ifreq = (struct ifreq*) buf;
	for (i = (ifconf.ifc_len / sizeof(struct ifreq)); i > 0; i--)
	{
		if (strcmp(ifreq->ifr_ifrn.ifrn_name, "rmnet0") != 0)
		{
			ifreq++;
            count++;
			continue;
		}
		if (strcmp(ifreq->ifr_ifrn.ifrn_name, "wlan0") != 0)
		{
			ifreq++;
            count++;
			continue;
		}
	}
    if(count==2)
        return 1;
    else 
        return 0;
}
*/

void insmod()
{
    system("svc wifi disable");
    system("sed -i 's/ctrl_interface=wlan0/#ctrl_interface=wlan0/g' gmn_file");
    system("chown wifi:wifi wpa_supplicant.conf");
    system("insmod /system/lib/modules/wlan.ko");
    system("netcfg wlan0 up");
    system("su -c 'wpa_supplicant -P/data/misc/wifi/wpa_supplicant.pid -iwlan0 -c/data/misc/wifi/wpa_supplicant.conf -B'");
    system("dhcpcd wlan0");
}
   // FILE *wpa = fopen("wpa_supplicant","a+");

void rmmod()
{
    FILE *file = fopen("/data/misc/wifi/wpa_supplicant.pid","r");
    char *cmd1;
    int *wpa_pid;
    system("netcfg wlan0 down");
    system("rmmod /system/lib/modules/wlan.ko");
    
    fscanf(file,"%d",wpa_pid);
    sprintf(cmd1,"kill %d",*wpa_pid);
    system(cmd1);
    system("sed -i 's/#ctrl_interface=wlan0/ctrl_interface=wlan0/g' gmn_file");
    system("chown wifi:wifi wpa_supplicant.conf");
}

void check_wpa_conf()
{
    char str[80];
    char *stc1 = "#ctrl_interface=wlan0";
    char *stc2 = "ctrl_interface=wlan0";
    FILE *wpa_conf = fopen("/data/misc/wifi/wpa_supplicant.conf","r");
    if(wpa_conf == NULL)
    {
        fprintf(gmn_log,"%s\n", "check_wpa_conf open error!"); 
        CONF_FLAG =  0;
    }
    fscanf(wpa_conf,"%s",str);
    while(!feof(wpa_conf))
    {
        if(!strcmp(str,stc1))
        {
            CONF_FLAG =  1;
            return;
        }
        if(!strcmp(str,stc2))
        { 
            CONF_FLAG =  0;
            return;
        }
         continue;

    }
    
}

void vsock_close()
{
    close(socks.vsock);
     
    struct rtentry rt;
	struct sockaddr_in dst;

	//struct sockaddr_in gw;
	struct sockaddr_in gen;

	memset(&rt, 0, sizeof(rt));

	bzero(&gen, sizeof(struct sockaddr_in));
	gen.sin_family = PF_INET;
	gen.sin_addr.s_addr = inet_addr("255.255.255.252");

	bzero(&dst, sizeof(struct sockaddr_in));
	dst.sin_addr.s_addr = inet_addr("192.168.1.0");
	dst.sin_family = PF_INET;
    del_route(&rt);

	memset(&rt, 0, sizeof(rt));

	bzero(&gen, sizeof(struct sockaddr_in));
	gen.sin_family = PF_INET;
	gen.sin_addr.s_addr = inet_addr("255.255.255.255");

	bzero(&dst, sizeof(struct sockaddr_in));
	dst.sin_addr.s_addr = inet_addr("192.168.1.254");
	dst.sin_family = PF_INET;
    del_route(&rt);

	memset(&rt, 0, sizeof(rt));

	bzero(&gen, sizeof(struct sockaddr_in));
	gen.sin_family = PF_INET;
	gen.sin_addr.s_addr = inet_addr("255.255.255.255");

	bzero(&dst, sizeof(struct sockaddr_in));
	dst.sin_addr.s_addr = inet_addr("10.10.10.253");
	dst.sin_family = PF_INET;
    del_route(&rt);
}

void set_3g_default(char *ip)
{
	struct rtentry rt;
	struct sockaddr_in dst;
	struct sockaddr_in gen;

	memset(&rt, 0, sizeof(rt));

	bzero(&gen, sizeof(struct sockaddr_in));
	gen.sin_family = PF_INET;
	gen.sin_addr.s_addr = inet_addr("0.0.0.0");

	bzero(&dst, sizeof(struct sockaddr_in));
	dst.sin_addr.s_addr = inet_addr(ip);
	dst.sin_family = PF_INET;

	rt.rt_dst = *(struct sockaddr*) &dst;
	rt.rt_genmask = *(struct sockaddr*) &gen;
	add_route(&rt);
}


int vsock_open(char *ip)
{
	int tun;
	char tun_name[10];
	bzero(tun_name, sizeof(tun_name));
	tun = tun_create(tun_name, IFF_TUN | IFF_NO_PI);
	if (tun < 0)
	{
		return 1;
	}
	interface_up(tun_name);
	set_ipaddr(tun_name, ip);
	if (get_default_route(&OLD_DEFAULT_ROUTE) != 0)
	{
		printf("get default route failed\n");
		bzero((void *) &OLD_DEFAULT_ROUTE, sizeof(struct rtentry));
	}
	route_change(tun_name);
	puts(tun_name);
	return tun;
}



void double_channel_uicontrol()
{
	int server_sockfd, client_sockfd;
	int server_len, client_len;
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	//int initflag=0;
	char buffer[8]; //receive buff of message

	/*
	 * control information
	 */
	char one[] = "1:1\0";
	char shuntper[] = "auto\0";
	char closeg[] = "close\0";
	char initfun[] = "init\0";

	memset(&buffer, '\0', sizeof(buffer));

	if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("Error in local socket with uplevel ue creat!");
		exit(-1);
	}
	fd = server_sockfd;
	//name the socket
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(6000);
	//strcpy(server_address.sun_path, "server_socket");
	server_len = sizeof(server_address);
	bind(server_sockfd, (struct sockaddr *) &server_address, server_len);

	//establish a connect

	listen(server_sockfd, 5);

	//accept a connect

	int readlen;
	//read and write

#ifdef UIDEBUG
	printf("android UI control panel initialized...\n");
#endif

	while (1)
	{
		client_len = sizeof(client_address);
		client_sockfd = accept(server_sockfd,
				(struct sockaddr *) &client_address, (socklen_t *) &client_len);

		memset((void *) buffer, '\0', sizeof(buffer));
		readlen = read(client_sockfd, buffer, sizeof(buffer));

#ifdef PACKET
		printf("read %d bytes\n", readlen);
#endif

		if (readlen != 0)
		{
			if ((initflag == 0)
					&& (strcmp((const char*) buffer, (const char*) initfun) == 0))
			{
				initflag = 1;
				//autoshunt = 1;
				printf("init\n\n");
				sleep(3);
				close(client_sockfd);
				continue;
			}
			if (strcmp((const char*) buffer, (const char*) one) == 0)
			{
				//autoshunt = 1;
				shunt(tgfd);
				printf("1:1set\n\n");
				sleep(3);
				close(client_sockfd);
				continue;
			}
			if (strcmp((const char*) buffer, (const char*) shuntper) == 0)
			{
				shunt_auto(tgfd);
				printf("auto\n\n");
				sleep(3);
				close(client_sockfd);
				continue;
			}
			if (strcmp((const char*) buffer, (const char*) closeg) == 0)
			{
				printf("function close\n\n");
				sleep(3);
				close(client_sockfd);
				continue;
			}
			else
			{
				printf("communication with ue end with error!\n\n");
				sleep(3);
				close(client_sockfd);
				continue;
			}

		}

	}
}

int send_shunt(char * rate)
{
    uint8_t data[20] = {0};
    struct gemini_head hd1;

    data[0] = 2 + strlen(rate) + 1;
    data[1] = 0;
    data[2] = 11;

    strcpy(data+3, rate);
    sendto(tgfd, data, data[0] + 1, 0, (struct sockaddr *) &serveraddr, sizeof(serveraddr));
#ifdef DEBUG
    printf("modify rate to %s\n", rate);
#endif
    return 0;
}




/*
 * determine whether to send data through 3g or wifi interface,
 *
 */

int gmn_connect()
{

    //signal(SIGTERM, recovery_route);
    //signal(SIGPIPE, SIG_IGN);

    bzero((void *) ip, sizeof(ip));

    char rtgbuff[MAX_BUFF] = "";
    char rwifibuff[MAX_BUFF] = "";
    char sbuff[MAX_BUFF] = "";

    pthread_t tgs;
    pthread_t tgr;
    pthread_t initfl;
    pthread_t b2t;

    bzero((void *) rtgbuff, sizeof(rtgbuff));
    bzero((void *) rwifibuff, sizeof(rwifibuff));
    bzero((void *) sbuff, sizeof(sbuff));

    if (get_default_route(&OLD_DEFAULT_ROUTE) != 0)
    {
        printf("some application has already break the route!!\n");
    }


    settghostrt();
    setwifihostrt();

    if (get_3g_ip((char *) ip) == 0)
    {
        fprintf(gmn_log,"本机3g地址是:%s\n", ip);
    }
    else
    {
        fprintf(gmn_log,"无法获取本机3g地址\n");
    }

    /*
     * establish 3g socket
     */
    server_for_gemini();
    fprintf(gmn_log,"*******server_for_gemini running...\n\n");

    fprintf(gmn_log,"client_for_gemini start...\n");

    /*
     * establish wifi socket
     */
    client_for_gemini();
    fprintf(gmn_log,"*******client_for_gemini running...\n\n");
    send_shunt(RATE_FLAG);
    return double_ch_flag_gmn;
}



int get_tun_route(struct rtentry *rt)
{
	FILE *source;
	int has_tun;
	struct sockaddr_in * p, *q;
	if ((source = fopen("/proc/net/route", "r")) == NULL)
		return -1;
	fscanf(source, "%*[^\n]\n"); //skip the first line
	while (get_route(source, rt) == 0) //read the default route
	{
		p = (struct sockaddr_in *) &(rt->rt_dst);
		q = (struct sockaddr_in *) &(rt->rt_genmask);
		if (p->sin_addr.s_addr == inet_addr("192.168.1.0")
				&& q->sin_addr.s_addr == inet_addr("255.255.255.252"))
		{
		    has_tun = 1;
			break;
		}
	}
	fclose(source);
	if (has_tun == 1)
		return 0;
	else
		return -2;
}

void function1()
{
    fprintf(gmn_log,"cant connect with Station\n");
    strcpy(app_msg,"cant connect with Station");
    rmmod();
    system("cp /data/misc/wifi/wpa_supplicant.conf /data/misc/wifi/wpa_supplicant.conf.gmn");
    system("cp /data/misc/wifi/wpa_supplicant.conf.wifi /data/misc/wifi/wpa_supplicant.conf");
    CONF_FLAG = 0;
    GMN_FLAG = 0;
    READY_FLAG = 0;

}


void *pthread_monitor()
{
    while(1)
    {
        if(GMN_FLAG)
        {
            pthread_cond_signal(&c_env);
        }
    }
}

void wifi_ip_tail()
{
    struct rtentry rt;
    bzero(&rt,sizeof(struct rtentry));
    if(get_tun_route(&rt)==0)
    {
        READY_FLAG = 1;
        pthread_cond_signal(&c_gmn);
        if((int)mnt==0)
            pthread_create(&mnt,NULL,pthread_monitor,NULL);
        pthread_mutex_lock(&m_env);
        pthread_cond_wait(&c_env,&m_env);
        pthread_mutex_unlock(&m_env);
    }
    else
    {
        socks.vsock = vsock_open("192.168.1.100");
        if(gmn_connect())
        {
            READY_FLAG = 1;
            pthread_cond_signal(&c_gmn);
            socks.rsock[0] = tgfd;
            socks.rsock[1] = wififd;
            socks.rsock[2] = wifitrfd;
            socks.rsock[3] = 0;
            if((int)mnt==0)
                pthread_create(&mnt,NULL,pthread_monitor,NULL);

        }
        else
        {
            function1();
        }
        pthread_mutex_lock(&m_env);
        pthread_cond_wait(&c_env,&m_env);
        pthread_mutex_unlock(&m_env);
    
    }
}


void *pthread_env()
{
    char ip[20];
    struct rtentry rt;
    pthread_mutex_init(&m_env,NULL);
    pthread_cond_init(&c_env,NULL);
    while(1)
    {
	    bzero(ip, sizeof(ip));
        strcpy(app_msg,"");
        if(!GMN_FLAG)
        {
            vsock_close(); 
            if(CONF_FLAG)
            {
                rmmod();
                system("cp /data/misc/wifi/wpa_supplicant.conf /data/misc/wifi/wpa_supplicant.conf.gmn");
                system("cp /data/misc/wifi/wpa_supplicant.conf.wifi /data/misc/wifi/wpa_supplicant.conf");
                CONF_FLAG = 0;
            }
            READY_FLAG = 0;
            if(get_3g_ip(ip))
            {
                set_3g_default(ip); 
            }
            pthread_mutex_lock(&m_env);
            pthread_cond_wait(&c_env,&m_env);
            pthread_mutex_unlock(&m_env);

        }
        else
        {
            if(get_wifi_ip(ip) && CONF_FLAG==0)
            {
                fprintf(gmn_log,"%s\n","please turn off the wifi first,and then turn 0n 3g!\n");
                strcpy(app_msg,"please turn off the wifi first,and then turn on 3g!");
                GMN_FLAG = 0;
                READY_FLAG = 0;
                pthread_mutex_lock(&m_env);
                pthread_cond_wait(&c_env,&m_env);
                pthread_mutex_unlock(&m_env);
            }
            else
            {
	            bzero(ip, sizeof(ip));
                if(get_3g_ip(ip))
                {
                    if(CONF_FLAG==1)
                    {
	                    bzero(ip, sizeof(ip));

                        if(get_wifi_ip(ip))
                        {
                            wifi_ip_tail();
                        }
                        else
                        {
	                        bzero(ip, sizeof(ip));
                            insmod();
                            sleep(1);
                            if(get_wifi_ip(ip))
                            {
                                wifi_ip_tail();
                            }
                            else
                            {
                                fprintf(gmn_log,"wifi initailizing failed!\n");
                                strcpy(app_msg,"wifi initailizing failed!");
                                function1();
                                pthread_mutex_lock(&m_env);
                                pthread_cond_wait(&c_env,&m_env);
                                pthread_mutex_unlock(&m_env);
                            }
                        }
                        
                            
                    }
                    else
                    {
                        system("svc wifi diable");
                        system("cp /data/misc/wifi/wpa_supplicant.conf /data/misc/wifi/wpa_supplicant.conf.wifi");
                        system("cp /data/misc/wifi/wpa_supplicant.conf.gmn /data/misc/wifi/wpa_supplicant.conf");
                    }
                }
                else
                {
                    READY_FLAG = 0;
                    GMN_FLAG = 0;
                    fprintf(gmn_log,"%s\n","3g initialing failed ,3g has no ip\n");
                    strcpy(app_msg,"3g initialing failed ,3g has no ip");
                    pthread_cond_wait(&c_env,&m_env);
                }
            }
        }
    }
}

void *pthread_gemini()
{
    unsigned char *close_msg = "204";
	struct sockaddr_in set_address;
	set_address.sin_family = AF_INET;
	set_address.sin_addr.s_addr = inet_addr("192.168.1.254");
	set_address.sin_port = htons(GTG);
    for(;;)
    {
        if(GMN_FLAG)
        {
            if(READY_FLAG)
            {
                if((int)tgs==0)
                {
                    if (pthread_create(&tgs, NULL, send_process, (void *) &socks) != 0)
                    {
                        fprintf(gmn_log,"*****server thread create error:%d\n", errno);
                        exit(-1);
                    }
                }
                if((int)tgr==0)
                {

                    /*
                     * the recv_process need to deal with control information
                     */
                    if (pthread_create(&tgr, NULL, recv_process, (void *) &socks) != 0)
                    {
                        fprintf(gmn_log,"recv_process thread create error\n");
                        exit(-1);
                    }
                }
                if((int)b2t==0)
                {
                    if (pthread_create(&b2t, NULL, buff2tun0_process, (void *) &socks) != 0)
                    {
                        fprintf(gmn_log,"buff2tun0_process thread create error\n");
                        exit(-1);
                    }
                            
                }
                
            }

        }
        else
        {
	        sendto(tgfd, (void *) close_msg, sizeof(close_msg), 0,
			(struct sockaddr*) &set_address, sizeof(set_address));
            pthread_cancel(tgs);
            pthread_cancel(tgr);
            pthread_cancel(b2t);
            pthread_mutex_destroy(MTX_RECV);
            pthread_cond_destroy(WAIT_RECV);
            free(MTX_RECV);
            free(WAIT_RECV);
            
        }
        pthread_mutex_lock(&m_gmn);
        pthread_cond_wait(&c_gmn,&m_gmn);
        pthread_mutex_unlock(&m_gmn);

    }

}

int opt(char *message)
{
    if(!strcmp(message,"G1#"))
        return 1;
    if(!strcmp(message,"G2#"))
        return 2;
    if(!strcmp(message,"G3#"))
        return 3;
    if(!strcmp(message,"G4#"))
        return 4;
    if(!strcmp(message,"G5#"))
        return 5;
    if(!strcmp(message,"G6;1:1#"))
        return 611;
    if(!strcmp(message,"G6;1:0#"))
        return 610;
    if(!strcmp(message,"G6;0:1#"))
        return 601;
    if(!strcmp(message,"G7#"))
        return 7;
    return 0;
}


void *pthread_ui()
{
    int app_sockfd,cli_sockfd;
    struct sockaddr_in server_address;
    int server_len;
    int readlen;
	memset(&server_address, 0, sizeof(server_address));
    if((app_sockfd = socket(AF_INET,SOCK_DGRAM,0)) < 0 )
    {
        fprintf(gmn_log,"error in app socket creating!\n");
        exit(-1);
    }
    server_address.sin_family = AF_INET;
    //inet_pton(AF_INET,"127.0.0.1",);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_address.sin_port = htons(APP);
    server_len = sizeof(server_address);

    if (bind(app_sockfd, (struct sockaddr *) &server_address , server_len) < 0)
    {
        fprintf(gmn_log,"error occurred in wifi data transfer socket bind\n");
        exit(-1);
    }
	listen(app_sockfd, 5);
    for(;;)
    {
        cli_sockfd = accept(app_sockfd,NULL,NULL);
        memset((void *)msg,0,sizeof(msg));
        readlen = read(cli_sockfd,msg,sizeof(msg));
        if(readlen!=0)
        {
            switch(opt(msg))
            {
                case 1:
                {
                    write(cli_sockfd,msg,readlen);
                    break;
                }
                case 2:
                {
                    sprintf(msg,"G2;%d;%d;%s#",GMN_FLAG,AUTO_FLAG,RATE_FLAG);
                    write(cli_sockfd,msg,sizeof(msg));
                    break;
                }    
                case 3:
                {
                    GMN_FLAG = 1;
                    pthread_cond_signal(&c_env);
                    while(1)
                    {
                        if(READY_FLAG==0)
                        {
                            pthread_mutex_lock(&m_ui);
                            pthread_cond_wait(&c_ui,&m_ui);
                            pthread_mutex_unlock(&m_ui);
                        }
                        else if(gmnok==3)
                        {
                           break; 
                        }
                    }
                    
                    if(strcmp(app_msg,""))
                    {
                        sprintf(msg,"G3;Success#");
                    }
                    else
                    { 
                        sprintf(msg,"G3;ERROR:%s#","app_msg");
                    }
                    write(cli_sockfd,msg,sizeof(msg));
                    
                    break;
                }
                case 4:
                {
                    GMN_FLAG = 0;
                    pthread_cond_signal(&c_gmn);
                    pthread_cond_signal(&c_env);
                    sleep(2);
                    sprintf(msg,"G4;Success#");
                    write(cli_sockfd,msg,readlen);
                    break;
                }
                case 5://auto mod
                {
                    AUTO_FLAG = 1;
                    break;
                }
                case 611:
                {
                    AUTO_FLAG = 0;
                    strcpy(RATE_FLAG,"1:1");
                    send_shunt(RATE_FLAG);
                    break;
                }
                case 601:
                {
                    AUTO_FLAG = 0;
                    strcpy(RATE_FLAG,"0:1");
                    send_shunt(RATE_FLAG);
                    break;
                }
                case 610:
                {
                    AUTO_FLAG = 0;
                    strcpy(RATE_FLAG,"1:0");
                    send_shunt(RATE_FLAG);
                    break;
                }
                case 7:
                {
                    sprintf(msg,"G7;%d;%d;%d;%d#",tgrecv,tgsend,wifirecv,wifisend);
                    write(cli_sockfd,msg,sizeof(msg));
                    break;
                    
                }
                
            }
        }
    }
}


int main(int argc, char *argv[])
{
    gmn_log = fopen("/data/misc/wifi/gmn_log","a+");
    gmn_file = fopen("/data/misc/wifi/gmn_file","r");
    if(gmn_file == NULL)
    {
        gmn_struct.GMN_FLAG = 0;
        gmn_struct.AUTO_FLAG = 0;
        strcpy(gmn_struct.RATE_FLAG,"1:1");
    }
    else
    {
        fread(&gmn_struct,sizeof(GMN_STRUCT),1,gmn_file);
        fclose(gmn_file);
    }
    GMN_FLAG = gmn_struct.GMN_FLAG;
    AUTO_FLAG = gmn_struct.AUTO_FLAG;
    strcpy(RATE_FLAG, gmn_struct.RATE_FLAG);
    

    READY_FLAG = 0;
    check_wpa_conf();
    if(pthread_create(&env,NULL,pthread_env,NULL)!=0)
    {
        fprintf(gmn_log,"%s\n", "pthread_env create error!"); 
        exit(1);
    }
    if(pthread_create(&gmn,NULL,pthread_gemini,NULL)!=0)
    {
        fprintf(gmn_log,"%s\n", "pthread_gemini create error!"); 
        exit(1);
    }
    if(pthread_create(&ui,NULL,pthread_ui,NULL)!=0)
    {
        fprintf(gmn_log,"%s\n", "pthread_ui create error!"); 
        exit(1);
    }

    return 0;
}
