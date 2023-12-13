/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * slcand.c - userspace daemon for serial line CAN interface driver SLCAN
 *
 * Copyright (c) 2009 Robert Haddon <robert.haddon@verari.com>
 * Copyright (c) 2009 Verari Systems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Send feedback to <linux-can@vger.kernel.org>
 *
 */

#include <asm-generic/ioctls.h>
#include <asm-generic/termbits.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <linux/sockios.h>
#include <linux/tty.h>
#include <net/if.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

/* Change this to whatever your daemon is called */
#define DAEMON_NAME "h_slcand"

/* Change this to the user under which to run */
#define RUN_AS_USER "root"

/* The length of ttypath buffer */
#define TTYPATH_LENGTH 256

/* UART flow control types */
#define FLOW_NONE 0
#define FLOW_HW 1
#define FLOW_SW 2

static void fake_syslog(int priority, const char * format, ...)
{
  va_list ap;

  printf("[%d] ", priority);
  va_start(ap, format);
  vprintf(format, ap);
  va_end(ap);
  printf("\n");
}

typedef void (*syslog_t)(int priority, const char * format, ...);
static syslog_t syslogger = syslog;

void print_usage(char * prg)
{
  fprintf(stderr, "%s - userspace daemon for serial line CAN interface driver SLCAN.\n", prg);
  fprintf(stderr, "\nUsage: %s [options] <tty> [canif-name]\n\n", prg);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "         -o          (send open command 'O\\r')\n");
  fprintf(stderr, "         -c          (send close command 'C\\r')\n");
  fprintf(stderr, "         -f          (read status flags with 'F\\r' to reset error states)\n");
  fprintf(stderr, "         -l          (send listen only command 'L\\r', overrides -o)\n");
  fprintf(stderr, "         -s <speed>  (set CAN speed 0..8)\n");
  fprintf(stderr, "         -S <speed>  (set UART speed in baud)\n");
  fprintf(stderr, "         -t <type>   (set UART flow control type 'hw' or 'sw')\n");
  fprintf(stderr, "         -b <btr>    (set bit time register value)\n");
  fprintf(stderr, "         -F          (stay in foreground; no daemonize)\n");
  fprintf(stderr, "         -h          (show this help page)\n");
  fprintf(stderr, "\nExamples:\n");
  fprintf(stderr, "h_slcand -o -c -f -s6 ttyUSB0\n\n");
  fprintf(stderr, "h_slcand -o -c -f -s6 ttyUSB0 can0\n\n");
  fprintf(stderr, "h_slcand -o -c -f -s6 /dev/ttyUSB0\n\n");
  exit(EXIT_FAILURE);
}

static int slcand_running;
static volatile sig_atomic_t exit_code;
static char ttypath[TTYPATH_LENGTH];

static void child_handler(int signum)
{
  switch (signum) {
    case SIGUSR1:
      /* exit parent */
      exit(EXIT_SUCCESS);
      break;
    case SIGINT:
    case SIGTERM:
    case SIGALRM:
    case SIGCHLD:
      syslogger(LOG_NOTICE, "received signal %i on %s", signum, ttypath);
      exit_code = 128 + signum;
      slcand_running = 0;
      break;
  }
}

int main(int argc, char * argv[])
{
  char * tty = NULL;
  char const * devprefix = "/dev/";
  char * name = NULL;
  char buf[20];
  static struct ifreq ifr;
  struct termios2 tios;
  struct termios2 tios_old;

  int opt;
  int send_open = 0;
  int send_close = 0;
  int send_listen = 0;
  int send_read_status_flags = 0;
  char * speed = NULL;
  char * uart_speed_str = NULL;
  unsigned int uart_speed = 0;
  int flow_type = FLOW_NONE;
  char * btr = NULL;
  int run_as_daemon = 1;
  char * pch;
  int ldisc = N_SLCAN;
  int fd;

  ttypath[0] = '\0';

  while ((opt = getopt(argc, argv, "ocfls:S:t:b:?hF")) != -1) {
    switch (opt) {
      case 'o':
        send_open = 1;
        break;
      case 'c':
        send_close = 1;
        break;
      case 'f':
        send_read_status_flags = 1;
        break;
      case 'l':
        send_listen = 1;
        break;
      case 's':
        speed = optarg;
        if (strlen(speed) > 1) print_usage(argv[0]);
        break;
      case 'S':
        uart_speed_str = optarg;
        errno = 0;
        uart_speed = strtol(uart_speed_str, NULL, 10);
        if (errno) print_usage(argv[0]);
        if (uart_speed > 6000000) {
          fprintf(stderr, "Unsupported UART speed (%u)\n", uart_speed);
          exit(EXIT_FAILURE);
        }
        break;
      case 't':
        if (!strcmp(optarg, "hw")) {
          flow_type = FLOW_HW;
        } else if (!strcmp(optarg, "sw")) {
          flow_type = FLOW_SW;
        } else {
          fprintf(stderr, "Unsupported flow type (%s)\n", optarg);
          exit(EXIT_FAILURE);
        }
        break;
      case 'b':
        btr = optarg;
        if (strlen(btr) > 8) print_usage(argv[0]);
        break;
      case 'F':
        run_as_daemon = 0;
        break;
      case 'h':
      case '?':
      default:
        print_usage(argv[0]);
        break;
    }
  }

  if (!run_as_daemon) syslogger = fake_syslog;

  /* Initialize the logging interface */
  openlog(DAEMON_NAME, LOG_PID, LOG_LOCAL5);

  /* Parse serial device name and optional can interface name */
  tty = argv[optind];
  if (NULL == tty) print_usage(argv[0]);

  name = argv[optind + 1];
  if (name && (strlen(name) > sizeof(ifr.ifr_newname) - 1)) print_usage(argv[0]);

  /* Prepare the tty device name string */
  pch = strstr(tty, devprefix);
  if (pch != tty)
    snprintf(ttypath, TTYPATH_LENGTH, "%s%s", devprefix, tty);
  else
    snprintf(ttypath, TTYPATH_LENGTH, "%s", tty);

  syslogger(LOG_INFO, "starting on TTY device %s", ttypath);

  fd = open(ttypath, O_RDWR | O_NONBLOCK | O_NOCTTY);
  if (fd < 0) {
    syslogger(LOG_NOTICE, "failed to open TTY device %s\n", ttypath);
    perror(ttypath);
    exit(EXIT_FAILURE);
  }

  /* Configure baud rate */
  memset(&tios, 0, sizeof(tios));
  if (ioctl(fd, TCGETS2, &tios) < 0) {
    syslogger(LOG_NOTICE, "failed to get attributes for TTY device %s: %s\n", tty, strerror(errno));
    exit(EXIT_FAILURE);
  }

  /* Save old configuration to later restore it*/
  memset(&tios_old, 0, sizeof(tios_old));
  if (ioctl(fd, TCGETS2, &tios_old) < 0) {
    fprintf(stderr, "failed to get attributes for TTY device %s: %s\n", tty, strerror(errno));
    exit(EXIT_FAILURE);
  }

  // Because of a recent change in linux - https://patchwork.kernel.org/patch/9589541/
  // we need to set low latency flag to get proper receive latency
  struct serial_struct snew;
  if (ioctl(fd, TIOCGSERIAL, &snew) < 0) {
    syslogger(
      LOG_NOTICE, "failed to get latency flags for device \"%s\": %s!\n", tty, strerror(errno));
  }

  snew.flags |= ASYNC_LOW_LATENCY;
  if (ioctl(fd, TIOCSSERIAL, &snew) < 0) {
    syslogger(
      LOG_NOTICE, "failed to set latency flags for device \"%s\": %s!\n", tty, strerror(errno));
  }

  /* Reset UART settings */
  tios.c_iflag &= ~IXOFF;
  tios.c_cflag &= ~CRTSCTS;

  /* Baud Rate */
  tios.c_cflag &= ~CBAUD;
  tios.c_cflag |= BOTHER;
  tios.c_ispeed = uart_speed;
  tios.c_ospeed = uart_speed;

  /* Flow control */
  if (flow_type == FLOW_HW)
    tios.c_cflag |= CRTSCTS;
  else if (flow_type == FLOW_SW)
    tios.c_iflag |= (IXON | IXOFF);

  /* apply changes */
  if (ioctl(fd, TCSETS2, &tios) < 0) {
    syslogger(LOG_NOTICE, "Cannot set attributes for device \"%s\": %s!\n", tty, strerror(errno));
    close(fd);
    exit(EXIT_FAILURE);
  }

  if (speed) {
    sprintf(buf, "C\rS%s\r", speed);
    if (write(fd, buf, strlen(buf)) <= 0) {
      perror("write");
      exit(EXIT_FAILURE);
    }
  }

  if (btr) {
    sprintf(buf, "C\rs%s\r", btr);
    if (write(fd, buf, strlen(buf)) <= 0) {
      perror("write");
      exit(EXIT_FAILURE);
    }
  }

  if (send_read_status_flags) {
    sprintf(buf, "F\r");
    if (write(fd, buf, strlen(buf)) <= 0) {
      perror("write");
      exit(EXIT_FAILURE);
    }
  }

  if (send_listen) {
    sprintf(buf, "L\r");
    if (write(fd, buf, strlen(buf)) <= 0) {
      perror("write");
      exit(EXIT_FAILURE);
    }
  } else if (send_open) {
    sprintf(buf, "O\r");
    if (write(fd, buf, strlen(buf)) <= 0) {
      perror("write");
      exit(EXIT_FAILURE);
    }
  }

  /* set slcan like discipline on given tty */
  if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
    perror("ioctl TIOCSETD");
    exit(EXIT_FAILURE);
  }

  /* retrieve the name of the created CAN netdevice */
  if (ioctl(fd, SIOCGIFNAME, ifr.ifr_name) < 0) {
    perror("ioctl SIOCGIFNAME");
    exit(EXIT_FAILURE);
  }

  syslogger(LOG_NOTICE, "attached TTY %s to netdevice %s\n", ttypath, ifr.ifr_name);

  /* try to rename the created netdevice */
  if (name) {
    int s = socket(PF_INET, SOCK_DGRAM, 0);

    if (s < 0)
      perror("socket for interface rename");
    else {
      /* current slcan%d name is still in ifr.ifr_name */
      memset(ifr.ifr_newname, 0, sizeof(ifr.ifr_newname));
      strncpy(ifr.ifr_newname, name, sizeof(ifr.ifr_newname) - 1);

      if (ioctl(s, SIOCSIFNAME, &ifr) < 0) {
        syslogger(LOG_NOTICE, "netdevice %s rename to %s failed\n", buf, name);
        perror("ioctl SIOCSIFNAME rename");
        exit(EXIT_FAILURE);
      } else
        syslogger(LOG_NOTICE, "netdevice %s renamed to %s\n", buf, name);

      close(s);
    }
  }

  /* Daemonize */
  if (run_as_daemon) {
    if (daemon(0, 0)) {
      syslogger(LOG_ERR, "failed to daemonize");
      exit(EXIT_FAILURE);
    }
  } else {
    /* Trap signals that we expect to receive */
    signal(SIGINT, child_handler);
    signal(SIGTERM, child_handler);
  }

  slcand_running = 1;

  /* The Big Loop */
  while (slcand_running) sleep(1); /* wait 1 second */

  /* Reset line discipline */
  syslogger(LOG_INFO, "stopping on TTY device %s", ttypath);
  ldisc = N_TTY;
  if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
    perror("ioctl TIOCSETD");
    exit(EXIT_FAILURE);
  }

  if (send_close) {
    sprintf(buf, "C\r");
    if (write(fd, buf, strlen(buf)) <= 0) {
      perror("write");
      exit(EXIT_FAILURE);
    }
  }

  /* Reset old rates */
  if (ioctl(fd, TCSETS2, &tios_old) < 0) {
    syslogger(
      LOG_NOTICE, "sailed to reset attributes for device \"%s\": %s!\n", tty, strerror(errno));
    exit(EXIT_FAILURE);
  }

  /* Finish up */
  syslogger(LOG_NOTICE, "terminated on %s", ttypath);
  closelog();
  printf("Netdevice %s attached to device \'%s\' stopped gracefully.\n", name, tty);
  return exit_code;
}
