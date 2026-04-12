/*
 * ============================================================================
 *
 *       Filename:  main.c
 *
 *    Description:  AP (Access Point) client main entry point.
 *                  - Parses configuration (UCI + command line)
 *                  - Initializes network layer
 *                  - Listens for AC broadcast probes
 *                  - Registers with AC
 *                  - Periodically reports status
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Compiler:  gcc
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "arg.h"
#include "message.h"
#include "process.h"
#include "msg.h"
#include "net.h"
#include "netlayer.h"
#include "log.h"
#include "link.h"
#include "thread.h"
#include "sec.h"

volatile int g_running = 1;

static void signal_handler(int sig)
{
	(void)sig;
	sys_info("AP shutting down on signal...\n");
	g_running = 0;
}

static void print_banner(void)
{
	printf("\n");
	printf("  ╔═══════════════════════════════════════════╗\n");
	printf("  ║       OpenWrt AP Controller Client v2.0 ║\n");
	printf("  ║       Build: %-28s║\n", __DATE__);
	printf("  ╚═══════════════════════════════════════════╝\n");
	printf("\n");
}

int main(int argc, char *argv[])
{
	openlog("apctl", LOG_PID, LOG_USER);

	/* Parse arguments (UCI + command line) */
	proc_arg(argc, argv);

	if (!daemon_mode || debug)
		print_banner();

	/* Install signal handlers */
	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);

	/* Initialize security layer */
	if (sec_init() != 0) {
		sys_err("Security init failed\n");
		return -1;
	}

	/* Verify password is configured */
	if (sec_password_check() != 0) {
		sys_err("No password configured for AP. "
			"Set 'option password' in /etc/config/acctl\n");
		return -1;
	}

	/* Initialize network (epoll + datalink layer) */
	net_init();

	/* Initialize message queue */
	message_init();

	/* Initialize and start reporting */
	init_report();

	sys_info("AP client started (MAC="
		MAC_FMT", NIC=%s)\n",
		argument.mac[0], argument.mac[1],
		argument.mac[2], argument.mac[3],
		argument.mac[4], argument.mac[5],
		argument.nic);

	/* Main loop — wait for signals */
	while (g_running) {
		sleep(1);
	}

	sys_info("AP client stopped\n");
	closelog();
	return 0;
}
