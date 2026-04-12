/*
 * =====================================================================================
 *
 *       Filename:  dllayer.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2014年08月19日 14时40分14秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  
 *
 * =====================================================================================
 */
#ifndef __DLLLAYER_H__
#define __DLLLAYER_H__
#include <linux/if_ether.h>

#define ETH_INNO 		(0x8d8d)
#define DLL_PKT_MAXLEN 		(1514)  /* Ethernet maximum frame size (1500 MTU + header) */
#define DLL_PKT_DATALEN 	(DLL_PKT_MAXLEN - sizeof(struct ethhdr))
void dll_init(char *nic, int *rcvsock, int *sdrsock, int *brdsock);
int dll_brdcast(char *data, int size);
int dll_sendpkt(char *dmac, char *data, int size);
int dll_rcv(char *data, int size, char *src_mac_out);

#endif /* __DLLAYER_H__ */
