/*
 * ============================================================================
 *
 *       Filename:  main.c
 *
 *    Description:  AC Controller server main entry point.
 *                  - Initializes SQLite database
 *                  - Loads configuration from UCI
 *                  - Starts IP pool manager
 *                  - Generates AC UUID
 *                  - Sets up AP hash table
 *                  - Starts message processing threads
 *                  - Initializes network layer (datalink + TCP)
 *                  - Runs as daemon
 *
 *        Version:  2.0
 *        Created:  2026-04-12
 *       Revision:  complete rewrite — daemon mode, signal handling, UCI config
 *       Compiler:  gcc
 *
 *         Author:  jianxi sun (jianxi), ycsunjane@gmail.com
 *   Organization:  OpenWrt AC Controller Project
 *
 * ============================================================================
 */
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "arg.h"
#include "net.h"
#include "msg.h"
#include "aphash.h"
#include "process.h"
#include "netlayer.h"
#include "resource.h"
#include "dllayer.h"
#include "db.h"
#include "sec.h"
#include "log.h"
#include <sys/resource.h>

volatile int g_running = 1;  /* global shutdown flag */

/*
 * Signal handler — graceful shutdown
 */
static void signal_handler(int sig)
{
	if (sig == SIGINT || sig == SIGTERM) {
		sys_info("Received signal %d, shutting down...\n", sig);
		g_running = 0;
	}
}

/*
 * daemonize — fork into background if not in debug mode
 */
static void daemonize(void)
{
	if (!daemon_mode || debug)
		return;

	/* Already daemonized? */
	if (getppid() == 1)
		return;

	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork() failed: %s\n", strerror(errno));
		exit(-1);
	}
	if (pid > 0)
		exit(0);  /* parent exits */

	/* Child continues — become session leader */
	if (setsid() < 0) {
		fprintf(stderr, "setsid() failed: %s\n", strerror(errno));
		exit(-1);
	}

	/* Second fork to prevent acquiring a controlling terminal */
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork() second failed: %s\n", strerror(errno));
		exit(-1);
	}
	if (pid > 0)
		exit(0);

	/* Redirect stdin/stdout/stderr to /dev/null */
	int fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > 2)
			close(fd);
	}

	chdir("/");
	umask(0);
}

/*
 * Print banner and version info
 */
static void print_banner(void)
{
	printf("\n");
	printf("  ╔═══════════════════════════════════════════╗\n");
	printf("  ║       OpenWrt AC Controller  v2.0        ║\n");
	printf("  ║       Build: %-29s║\n", __DATE__);
	printf("  ║       DB: %s                     ║\n", DBNAME);
	printf("  ╚═══════════════════════════════════════════╝\n");
	printf("\n");
}

/*
 * check_prerequisites — verify required files and directories exist
 */
static int check_prerequisites(void)
{
	/* Ensure /etc/acctl directory exists */
	if (access("/etc/acctl", F_OK) != 0) {
		if (mkdir("/etc/acctl", 0755) != 0) {
			sys_err("Cannot create /etc/acctl: %s\n", strerror(errno));
			return -1;
		}
		sys_info("Created /etc/acctl directory\n");
	}

	/* Ensure database file parent directory exists */
	if (strncmp(DBNAME, "/etc/acctl/", 11) == 0) {
		/* Database goes in /etc/acctl — already created above */
	}

	return 0;
}

/*
 * print_status — dump current system status
 */
static void print_status(void)
{
	int ap_count = hash_ap_count();
	sys_info("AC Controller status:\n");
	sys_info("  UUID:        %.36s\n", ac.acuuid);
	sys_info("  NIC:         %s\n", argument.nic);
	sys_info("  Port:        %d\n", argument.port);
	sys_info("  BrdItv:      %ds\n", argument.brditv);
	sys_info("  ResChkItv:   %ds\n", argument.reschkitv);
	sys_info("  AP online:   %d\n", ap_count);
}

/*
 * cleanup — release all resources on shutdown
 */
static void cleanup(void)
{
	sys_info("Cleaning up resources...\n");

	/* Close database */
	if (db)
		db_close(db);

	/* Clear IP pool */
	if (ippool)
		res_ip_clear();

	/* Note: hash table entries are leaked on exit — acceptable for
	 * daemon mode since they persist across reloads */

	closelog();
}

int main(int argc, char *argv[])
{
	/* 0. Open syslog for logging */
	openlog("acser", LOG_PID, LOG_USER);

	/* 1. Parse arguments (UCI config + command line) */
	proc_arg(argc, argv);

	/* 2. Daemonize if requested */
	daemonize();

	/* 3. Print banner in foreground/debug mode */
	if (!daemon_mode || debug)
		print_banner();

	/* 4. Check prerequisites */
	if (check_prerequisites() != 0) {
		sys_err("Prerequisites check failed\n");
		exit(-1);
	}

	/* 5. Install signal handlers */
	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP,  signal_handler);  /* reload config */

	/* 6. Initialize security layer (password + rate limiting + replay) */
	if (sec_init() != 0) {
		sys_err("Security layer initialization failed\n");
		exit(-1);
	}
	if (sec_password_check() != 0) {
		sys_err("Password not configured. Run: "
			"uci set acctl.@acctl[0].password='your_secure_password'\n");
		exit(-1);
	}

	/* 7. Initialize JSON database */
	if (db_init(NULL) != 0) {
		sys_err("JSON database initialization failed\n");
		exit(-1);
	}
	sys_info("JSON database initialized: %s\n", DBNAME);

	/* 8. Initialize IP address pool (loads from DB) */
	resource_init();
	sys_info("IP pool initialized\n");

	/* 9. Set AC UUID (from DMI UUID, /proc filesystem, or generated) */
	ac_init();
	sys_info("AC UUID: %.36s\n", ac.acuuid);

	/* 10. Initialize AP hash table */
	hash_init();
	sys_info("AP hash table initialized (size=%d)\n", AP_HASH_SIZE);

	/* 11. Start AC message processing thread */
	message_travel_init();
	sys_info("Message travel thread started\n");

	/* 12. Initialize and start network layer */
	net_init();
	sys_info("Network layer initialized (TCP port=%d, ETH protocol=0x%04x)\n",
		argument.port, (unsigned int)ETH_INNO);

	/* 13. Print startup status */
	print_status();

	/* 14. Main daemon loop */
	sys_info("AC Controller started successfully, running...\n");
	while (g_running) {
		sleep(1);
	}

	/* 15. Graceful shutdown */
	sys_info("AC Controller shutting down...\n");
	cleanup();

	return 0;
}
