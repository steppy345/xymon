/*----------------------------------------------------------------------------*/
/* Big Brother webpage generator tool.                                        */
/*                                                                            */
/* This is a replacement for the "mkbb.sh" and "mkbb2.sh" scripts from the    */
/* "Big Brother" monitoring tool from BB4 Technologies.                       */
/*                                                                            */
/* Primary reason for doing this: Shell scripts perform badly, and with a     */
/* medium-sized installation (~150 hosts) it takes several minutes to         */
/* generate the webpages. This is a problem, when the pages are used for      */
/* 24x7 monitoring of the system status.                                      */
/*                                                                            */
/* Copyright (C) 2002 Henrik Storner <henrik@storner.dk>                      */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc-trends.c,v 1.21 2003-05-17 09:25:42 henrik Exp $";

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>

#include "bbgen.h"
#include "util.h"
#include "loaddata.h"
#include "larrdgen.h"

char    *larrdcol = "larrd";
int 	enable_larrdgen = 0;
int 	larrd_update_interval = 300; /* Update LARRD pages every N seconds */
int     log_nohost_rrds = 0;

rrdlayout_t rrdnames[] = {
	{ "la",      NULL,        0 },
	{ "disk",    "disk_part", 5 },
	{ "memory",  NULL,        0 },
	{ "tcp",     NULL,        0 },
	{ "citrix",  NULL,        0 },
	{ "users",   NULL,        0 },
	{ "vmstat",  NULL,        0 },
	{ "netstat", NULL,        0 },
	{ "iostat",  NULL,        0 },
	{ "ntpstat", NULL,        0 },
	{ NULL,      NULL,        0 }
};


static char *rrdlink_url(char *hostname, rrd_t *rrd, int larrd043)
{
	static char rrdurl[4096];
	char svcurl[4096];
	const char *linkfmt = "<p><A HREF=\"%s\"><IMG SRC=\"%s&graph=hourly\" ALT=\"larrd is accumulating %s\" BORDER=0></A></p>\n";

	if (larrd043 && rrd->rrdname->partname) {
		char rrdparturl[4096];
		int first = 0;

		rrdurl[0] = '\0';

		do {
			int last = first+rrd->rrdname->maxgraphs;

			if (last > rrd->count) last = rrd->count;
			sprintf(svcurl, "%s/larrd-grapher.cgi?host=%s&service=%s&%s=%d..%d", 
				getenv("CGIBINURL"), hostname, rrd->rrdname->name,
				rrd->rrdname->partname, first, last);

			sprintf(rrdparturl, linkfmt, svcurl, svcurl, rrd->rrdname->name);
			strcat(rrdurl, rrdparturl);
			first = last+1;
		} while (first < rrd->count);
	}
	else {
		sprintf(svcurl, "%s/larrd-grapher.cgi?host=%s&service=%s", 
			getenv("CGIBINURL"), hostname, rrd->rrdname->name);
		sprintf(rrdurl, linkfmt, svcurl, svcurl, rrd->rrdname->name);
	}


	return rrdurl;
}

static char *rrdlink_text(host_t *host, rrd_t *rrd, int larrd043)
{
	static char rrdlink[4096];
	char *graphdef, *p;

	/* If no larrdgraphs definition, include all with default links */
	if (host->larrdgraphs == NULL) {
		return rrdlink_url(host->hostname, rrd, larrd043);
	}

	/* Find this rrd definition in the larrdgraphs */
	graphdef = strstr(host->larrdgraphs, rrd->rrdname->name);

	/* If not found ... */
	if (graphdef == NULL) {
		/* Do we include all by default ? */
		if (*(host->larrdgraphs) == '*') 
			/* Yes, return default link for this RRD */
			return rrdlink_url(host->hostname, rrd, larrd043);
		else
			/* No, return empty string */
			return "";
	}

	/* We now know that larrdgraphs explicitly define what to do with this RRD */

	/* Does he want to explicitly exclude this RRD ? */
	if ((graphdef > host->larrdgraphs) && (*(graphdef-1) == '!')) return "";


	/* It must be included. */
	rrdlink[0] = '\0';

	p = graphdef + strlen(rrd->rrdname->name);
	if (*p == ':') {
		/* There is an explicit list of graphs to add for this RRD. */
		char savechar;
		char *enddef;
		rrd_t *myrrd;

		myrrd = malloc(sizeof(rrd_t));
		myrrd->rrdname = malloc(sizeof(rrdlayout_t));

		/* First, null-terminate this graph definition so we only look at the active RRD */
		enddef = strchr(graphdef, ',');
		if (enddef) *enddef = '\0';

		graphdef = (p+1);
		do {

			p = strchr(graphdef, '|');			/* Ends at '|' ? */
			if (p == NULL) p = graphdef + strlen(graphdef);	/* Ends at end of string */
			savechar = *p; *p = '\0'; 

			myrrd->rrdname->name = graphdef;
			myrrd->rrdname->partname = NULL;
			myrrd->rrdname->maxgraphs = 999;
			myrrd->count = 1;
			myrrd->next = NULL;
			strcat(rrdlink, rrdlink_url(host->hostname, myrrd, larrd043));
			*p = savechar;

			graphdef = p;
			if (*graphdef != '\0') graphdef++;

		} while (*graphdef);

		if (enddef) *enddef = ',';
		free(myrrd->rrdname);
		free(myrrd);

		return rrdlink;
	}
	else {
		/* It is included with the default graph */
		return rrdlink_url(host->hostname, rrd, larrd043);
	}

	return "";
}


int generate_larrd(char *rrddirname, char *larrdcolumn, int larrd043)
{
	DIR *rrddir;
	struct dirent *d;
	char fn[MAX_PATH];
	hostlist_t *hostwalk;
	rrd_t *rwalk;
	int i;
	char *allrrdlinks;
	int allrrdlinksize;
	time_t now;
	struct utimbuf logfiletime;

	if (!run_columngen("larrd", larrd_update_interval, enable_larrdgen))
		return 1;

	allrrdlinksize = 16384;
	allrrdlinks = malloc(allrrdlinksize);

	now = time(NULL);
	i = atoi(getenv("PURPLEDELAY"));
	logfiletime.actime = logfiletime.modtime = now + i*60;

	/*
	 * General idea: Scan the RRD directory for all RRD files, and 
	 * pick up which RRD's are present for each host.
	 * Since there are only a limited set of possible RRD links to
	 * generate, this does not take up a huge hunk of memory.
	 * Then, loop over the list of hosts, and generate a log
	 * file and an html file for the larrd column.
	 */

	chdir(rrddirname);
	rrddir = opendir(rrddirname);
	if (!rrddir) {
		printf("Cannot access RRD directory\n");
		return 1;
	}

	while ((d = readdir(rrddir))) {
		strcpy(fn, d->d_name);

		if ((strlen(fn) > 4) && (strcmp(fn+strlen(fn)-4, ".rrd") == 0)) {
			char *p, *rrdname;
			rrdlayout_t *r = NULL;
			int found, hostfound;
			int i;

			/* Logfiles use ',' instead of '.' in FQDN hostnames */
			for (p=fn; *p; p++) {
				if (*p == ',') *p = '.';
			}

			/* Is this a known host? */
			hostwalk = hosthead; found = hostfound = 0;
			while (hostwalk && (!found)) {
				if (strncmp(hostwalk->hostentry->hostname, fn, strlen(hostwalk->hostentry->hostname)) == 0) {

					p = fn + strlen(hostwalk->hostentry->hostname);
					hostfound = ( (*p == '.') || (*p = ',') );

					/* First part of filename matches.
					 * Now check that there is a valid RRD id next -
					 * if not, then we may have hit a partial hostname 
					 */

					rrdname = fn + strlen(hostwalk->hostentry->hostname) + 1;
					p = strchr(rrdname, '.');
					if (p) *p = '\0';

					for (i=0; (rrdnames[i].name && (strcmp(rrdnames[i].name, rrdname) != 0)); i++) ;
					if (rrdnames[i].name) {
						found = 1;
						r = &rrdnames[i];
					}
				}

				if (!found) {
					hostwalk = hostwalk->next;
				}
			}

			if (found) {
				/* hostwalk now points to the host owning this RRD */
				for (rwalk = hostwalk->hostentry->rrds; (rwalk && (rwalk->rrdname != r)); rwalk = rwalk->next) ;
				if (rwalk == NULL) {
					rrd_t *newrrd = malloc(sizeof(rrd_t));

					newrrd->rrdname = r;
					newrrd->count = 1;
					newrrd->next = hostwalk->hostentry->rrds;
					hostwalk->hostentry->rrds = newrrd;
				}
				else rwalk->count++;

				/* printf("Host\t%-40s\t\trrd %s\n", hostwalk->hostentry->hostname, r); */
			}

			if (!hostfound && log_nohost_rrds) {
				/* This rrd file has no matching host. */
				printf("No host record for rrd %s\n", d->d_name);
			}
		}
	}

	chdir(getenv("BBLOGS"));

	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
		char logfn[MAX_PATH], htmlfn[MAX_PATH];
		char *rrdlink;
		FILE *fd;
		int i;

		sprintf(logfn, "%s/%s.%s", getenv("BBLOGS"), 
			commafy(hostwalk->hostentry->hostname), larrdcolumn);
		if (getenv("BBHTML")) {
			sprintf(htmlfn,"%s/%s.%s.html", getenv("BBHTML"), 
				hostwalk->hostentry->hostname, larrdcolumn);
		}
		else {
			sprintf(htmlfn,"%s/www/html/%s.%s.html", getenv("BBHOME"), 
				hostwalk->hostentry->hostname, larrdcolumn);
		}


		strcpy(allrrdlinks, "");

		for (i=0; rrdnames[i].name; i++) {
			for (rwalk = hostwalk->hostentry->rrds; (rwalk && (rwalk->rrdname->name != rrdnames[i].name)); rwalk = rwalk->next) ;
			if (rwalk) {
				rrdlink = rrdlink_text(hostwalk->hostentry, rwalk, larrd043);
				if ((strlen(allrrdlinks) + strlen(rrdlink)) >= allrrdlinksize) {
					allrrdlinksize += 4096;
					allrrdlinks = realloc(allrrdlinks, allrrdlinksize);
				}
				strcat(allrrdlinks, rrdlink);
			}
		}


		if (strlen(allrrdlinks) > 0) {
			fd = fopen(logfn, "w");
			if (!fd) {
				perror("Cannot open logfile");
				exit(1);
			}

			fprintf(fd, "green %s - larrd is accumulating <center><BR>\n", timestamp);
			fprintf(fd, "%s\n", allrrdlinks);
			fprintf(fd, "</center>\n");
			fclose(fd);
			utime(logfn, &logfiletime);

			/* HTML files generated only if we use BBLOGSTATUS=STATIC */
			if (generate_static()) {
				sethostenv(hostwalk->hostentry->hostname, hostwalk->hostentry->ip, larrdcolumn, "green");
				fd = fopen(htmlfn, "w");
 				headfoot(fd, "hostsvc", "", "header", COL_GREEN);

				fprintf(fd, "<!-- Start of code generated by Big Brother Systems and Network Monitor -->\n");
				fprintf(fd, "\n");
				fprintf(fd, "<A NAME=begindata>&nbsp;</A>\n");
				fprintf(fd, "\n");
				fprintf(fd, "<CENTER><TABLE ALIGN=CENTER BORDER=0>\n");
				fprintf(fd, "<TR><TH><FONT %s>\n", getenv("MKBBROWFONT"));
				fprintf(fd, "%s - %s<BR><HR WIDTH=60%%></TH>\n", hostwalk->hostentry->hostname, larrdcolumn);
				fprintf(fd, "<TR><TD><H3>\n");
				fprintf(fd, "green %s - larrd is accumulating <center><BR>\n", timestamp);
				fprintf(fd, "</H3><PRE>\n");

				fprintf(fd, "%s\n", allrrdlinks);
				fprintf(fd, "\n");

				fprintf(fd, "</PRE>\n");
				fprintf(fd, "</TD></TR></TABLE>\n");
				fprintf(fd, "\n");
				fprintf(fd, "<TABLE ALIGN=CENTER BORDER=0>\n");
				fprintf(fd, "<TR><TD ALIGN=CENTER>\n");
				fprintf(fd, "<FONT COLOR=teal SIZE=-1>\n");
				fprintf(fd, "<BR></center></FONT></TD></TR>\n");
				fprintf(fd, "</TABLE>\n");
				fprintf(fd, "\n");
				fprintf(fd, "</CENTER>\n");
				fprintf(fd, "\n");
				fprintf(fd, "<!-- End of code generated by Big Brother Systems and Network Monitor -->\n");

 				headfoot(fd, "hostsvc", "", "footer", COL_GREEN);
				fclose(fd);
			}
		}
	}

	closedir(rrddir);
	free(allrrdlinks);
	return 0;
}

