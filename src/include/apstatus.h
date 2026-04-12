/*
 * =====================================================================================
 *       Filename:  apstatus.h
 * =====================================================================================
 */
#ifndef __APSTATUS_H__
#define __APSTATUS_H__

#define SSID_MAXLEN  256

struct ssid_t {
	char ssid[SSID_MAXLEN];
	int  power;        /* signal strength in dBm */
};

struct apstatus_t {
	int  ssidnum;
	struct ssid_t ssid0;
	struct ssid_t ssid1;
	struct ssid_t ssid2;
};

struct apstatus_t *get_apstatus(void);
unsigned long get_uptime(void);
unsigned long get_memfree(void);
unsigned int  get_cpu_usage(void);

#endif /* __APSTATUS_H__ */
