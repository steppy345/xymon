/*----------------------------------------------------------------------------*/
/* Big Brother message daemon.                                                */
/*                                                                            */
/* This module receives messages from one channel of the bbgend master daemon.*/
/* These messages are then forwarded to the actual worker process via stdin;  */
/* the worker process can process the messages without having to care about   */
/* the tricky details in the bbgend/bbgend_channel communications.            */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitd_channel.c,v 1.23 2004-11-22 14:57:26 henrik Exp $";

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "libbbgen.h"

#include "bbgend_ipc.h"


/* For our in-memory queue of messages received from bbgend via IPC */
typedef struct msg_t {
	char *buf;
	char *bufp;
	int buflen;
	struct msg_t *next;
} msg_t;

msg_t *head = NULL;
msg_t *tail = NULL;

static volatile int running = 1;
static volatile int gotalarm = 0;
static volatile int dologswitch = 0;
static int childexit = -1;
bbgend_channel_t *channel = NULL;

void sig_handler(int signum)
{
	switch (signum) {
	  case SIGTERM:
	  case SIGINT:
	  case SIGPIPE:
		/* We lost the pipe to the worker child. Shutdown. */
		running = 0;
		break;

	  case SIGCHLD:
		/* Our worker child died. Follow it to the grave */
		wait(&childexit);
		running = 0;
		break;

	  case SIGALRM:
		gotalarm = 0;
		break;

	  case SIGHUP:
		dologswitch = 1;
		break;
	}
}

int main(int argc, char *argv[])
{
	int argi, n;

	struct sembuf s;
	char buf[SHAREDBUFSZ];
	msg_t *newmsg;
	int daemonize = 0;
	char *logfn = NULL;
	char *pidfile = NULL;

	int cnid = -1;
	int pfd[2];
	pid_t childpid = 0;
	char *childcmd = "";
	char **childargs = NULL;
	int canwrite;
	struct sigaction sa;

	/* Dont save the error buffer */
	save_errbuf = 0;

	for (argi=1; (argi < argc); argi++) {
		if (argnmatch(argv[argi], "--debug")) {
			debug = 1;
		}
		else if (argnmatch(argv[argi], "--channel=")) {
			char *cn = strchr(argv[argi], '=') + 1;

			for (cnid = 0; (channelnames[cnid] && strcmp(channelnames[cnid], cn)); cnid++) ;
		}
		else if (argnmatch(argv[argi], "--daemon")) {
			daemonize = 1;
		}
		else if (argnmatch(argv[argi], "--no-daemon")) {
			daemonize = 0;
		}
		else if (argnmatch(argv[argi], "--pidfile=")) {
			char *p = strchr(argv[argi], '=');
			pidfile = strdup(p+1);
		}
		else if (argnmatch(argv[argi], "--log=")) {
			char *p = strchr(argv[argi], '=');
			logfn = strdup(p+1);
		}
		else {
			int i = 0;
			childcmd = argv[argi];
			childargs = (char **) calloc((1 + argc - argi), sizeof(char *));
			while (argi < argc) { childargs[i++] = argv[argi++]; }
		}
	}

	if (cnid == -1) {
		errprintf("No channel specified\n");
		return 1;
	}

	/* Go daemon */
	if (daemonize) {
		/* Become a daemon */
		childpid = fork();
		if (childpid < 0) {
			/* Fork failed */
			errprintf("Could not fork child\n");
			exit(1);
		}
		else if (childpid > 0) {
			/* Parent exits */
			FILE *fd = NULL;
			if (pidfile) fd = fopen(pidfile, "w");
			if (fd) {
				fprintf(fd, "%d\n", (int)childpid);
				fclose(fd);
			}
			exit(0);
		}
		/* Child (daemon) continues here */
		setsid();
	}

	/* Catch signals */
	setup_signalhandler("bbgend_channel");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	/* Switch stdout/stderr to the logfile, if one was specified */
	freopen("/dev/null", "r", stdin);	/* bbgend_channel's stdin is not used */
	if (logfn) {
		freopen(logfn, "a", stdout);
		freopen(logfn, "a", stderr);
	}

	/* Start the channel handler */
	n = pipe(pfd);
	if (n == -1) {
		errprintf("Could not get a pipe: %s\n", strerror(errno));
		return 1;
	}
	childpid = fork();
	if (childpid == -1) {
		errprintf("Could not fork channel handler: %s\n", strerror(errno));
		return 1;
	}
	else if (childpid == 0) {
		/* The channel handler child */
		n = dup2(pfd[0], STDIN_FILENO);
		close(pfd[0]); close(pfd[1]);
		n = execvp(childcmd, childargs);
	}
	/* Parent process continues */
	close(pfd[0]);

	/* We dont want to block when writing to the worker */
	fcntl(pfd[1], F_SETFL, O_NONBLOCK);

	/* Attach to the channel */
	channel = setup_channel(cnid, CHAN_CLIENT);
	if (channel == NULL) {
		errprintf("Channel not available\n");
		return 1;
	}

	while (running) {

		if (dologswitch) {
			freopen(logfn, "a", stdout);
			freopen(logfn, "a", stderr);
			dologswitch = 0;
		}

		/* 
		 * Wait for GOCLIENT to go up.
		 *
		 * Note that we use IPC_NOWAIT if there are messages in the
		 * queue, because then we just want to pick up a message if
		 * there is one, and if not we want to continue pushing the
		 * queued data to the worker.
		 */
		s.sem_num = GOCLIENT; s.sem_op  = -1; s.sem_flg = (head ? IPC_NOWAIT : 0);
		n = semop(channel->semid, &s, 1);

		if (n == 0) {
			/*
			 * GOCLIENT went high, and so we got alerted about a new
			 * message arriving. Copy the message to our own buffer queue.
			 */
			strcpy(buf, channel->channelbuf);

			/* 
			 * Now we have safely stored the new message in our buffer.
			 * Wait until any other clients on the same channel have picked up 
			 * this message (GOCLIENT reaches 0).
			 *
			 * We wrap this into an alarm handler, because it can occasionally
			 * fail, causing the whole system to lock up. We dont want that....
			 */
			gotalarm = 0; signal(SIGALRM, sig_handler); alarm(2);
			do {
				s.sem_num = GOCLIENT; s.sem_op  = 0; s.sem_flg = 0;
				n = semop(channel->semid, &s, 1);
			} while ((n == -1) && (errno == EAGAIN) && running && (!gotalarm));
			signal(SIGALRM, SIG_IGN);

			if (gotalarm) {
				errprintf("Broke deadlock waiting for GOCLIENT to go low.\n");
			}

			/* 
			 * Let master know we got it by downing BOARDBUSY.
			 * This should not block, since BOARDBUSY is upped
			 * by the master just before he ups GOCLIENT.
			 */
			s.sem_num = BOARDBUSY; s.sem_op  = -1; s.sem_flg = 0;
			n = semop(channel->semid, &s, 1);

			/*
			 * Put the new message on our outbound queue.
			 */
			newmsg = (msg_t *) malloc(sizeof(msg_t));
			newmsg->buf = strdup(buf);
			newmsg->bufp = newmsg->buf;
			newmsg->buflen = strlen(buf);
			newmsg->next = NULL;
			if (head == NULL) {
				head = tail = newmsg;
			}
			else {
				tail->next = newmsg;
				tail = newmsg;
			}
		}
		else {
			if (errno != EAGAIN) {
				dprintf("Semaphore wait aborted: %s\n", strerror(errno));
				continue;
			}
		}

		/* 
		 * We've picked up messages from the master. Now we 
		 * must push them to the worker process. Since there 
		 * is no way to hang off both a semaphore and select(),
		 * this boils down to doing a kind of busy waiting.
		 * In practice, the queue will be empty most of the
		 * time and then we will just wait on the GOCHILD 
		 * semaphore, so it isn't really much of a concern.
		 *
		 * Maybe it could be optimized via SIGIO ... 
		 */
		canwrite = 1;
		while (head && canwrite) {
			n = write(pfd[1], head->bufp, head->buflen);
			if (n >= 0) {
				head->bufp += n;
				head->buflen -= n;
				if (head->buflen == 0) {
					msg_t *tmp = head;
					head = head->next;
					free(tmp->buf);
					free(tmp);
				}
			}
			else if (errno == EAGAIN) {
				/*
				 * Write would block ... stop for now. 
				 * Wait just a little while before continuing, so we
				 * dont do busy-waiting when the worker child is not
				 * accepting more data.
				 */
				canwrite = 0;
				usleep(2500);
			}
			else {
				msg_t *tmp;

				/* Write failed */
				errprintf("Our child has failed and will not talk to us\n");
				tmp = head;
				head = head->next;
				free(tmp->buf);
				free(tmp);
				canwrite = 0;
			}
		}
	}

	if (childexit != -1) {
		errprintf("Worker process died with exit code %d, terminating\n", childexit);
	}
	else {
		if (childpid > 0) kill(childpid, SIGTERM);
	}

	/* Detach from channels */
	close_channel(channel, CHAN_CLIENT);

	if (pidfile) unlink(pidfile);
	return (childexit != -1) ? 1 : 0;
}

