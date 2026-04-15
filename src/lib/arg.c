/*
 * ============================================================================
 *
 *       Filename:  arg.c
 *
 *    Description:  Configuration argument processing — UCI + command line.
 *                  Handles interface MAC/IP detection and argument validation.
 *
 *        Version:  2.0
 *       Created:  2014年08月19日
 *       Revision:  2026-04-15 — fixed strncpy, added __getaddr
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <errno.h>
#include <linux/if_ether.h>

#include "arg.h"
#include "log.h"

struct arg_t argument = {0};
int debug = 0;
int daemon_mode = 0;

/*
 * __getmac — get MAC address of the specified network interface
 */
static void __getmac(char *nic, char *mac)
{
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		sys_err("Create sock for get mac failed: %s\n",
			strerror(errno));
		exit(-1);
	}

	struct ifreq req;
	strncpy(req.ifr_name, nic, IFNAMSIZ - 1);
	req.ifr_name[IFNAMSIZ - 1] = '\0';

	if (ioctl(sockfd, SIOCGIFHWADDR, &req) < 0) {
		sys_err("ioctl SIOCGIFHWADDR failed for %s: %s\n",
			nic, strerror(errno));
		close(sockfd);
		exit(-1);
	}
	memcpy(mac, req.ifr_hwaddr.sa_data, ETH_ALEN);
	close(sockfd);
	pr_mac(mac);
}

/*
 * __getaddr — get IPv4 address of the specified network interface
 */
static void __getaddr(char *nic, struct sockaddr_in *addr)
{
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		sys_err("Create sock for get addr failed: %s\n",
			strerror(errno));
		exit(-1);
	}

	struct ifreq req;
	strncpy(req.ifr_name, nic, IFNAMSIZ - 1);
	req.ifr_name[IFNAMSIZ - 1] = '\0';

	if (ioctl(sockfd, SIOCGIFADDR, &req) < 0) {
		sys_err("ioctl SIOCGIFADDR failed for %s: %s\n",
			nic, strerror(errno));
		close(sockfd);
		exit(-1);
	}
	memcpy(addr, &req.ifr_addr, sizeof(struct sockaddr_in));
	close(sockfd);
	pr_ipv4(addr);
}

/*
 * __check_arg — validate configuration and populate missing fields
 */
static void __check_arg(void)
{
	if (argument.nic[0] == 0) {
		sys_err("No nic specified\n");
		help();
		exit(-1);
	}
	__getmac(&argument.nic[0], &argument.mac[0]);
	__getaddr(&argument.nic[0], &argument.addr);
	argument.port = (argument.port == 0) ? ACPORT : argument.port;
#ifdef SERVER
	argument.reschkitv = (argument.reschkitv == 0) ? 300 : argument.reschkitv;
	/* default ac broadcast interval */
	argument.brditv = (argument.brditv == 0) ? 30 : argument.brditv;
	argument.msgitv = (argument.msgitv == 0) ? argument.brditv / 10 : argument.msgitv;
	argument.addr.sin_port = htons((uint16_t)argument.port);
	pr_ipv4(&argument.addr);
#endif
#ifdef CLIENT
	argument.reportitv = (argument.reportitv == 0) ? 30 : argument.reportitv;
	argument.msgitv = (argument.msgitv == 0) ? argument.reportitv / 10 : argument.msgitv;
	argument.acaddr.sin_port = htons((uint16_t)argument.port);
	pr_ipv4(&argument.acaddr);
#endif
}

void proc_arg(int argc, char *argv[])
{
	proc_cfgarg();
	proc_cmdarg(argc, argv);
	__check_arg();
}
