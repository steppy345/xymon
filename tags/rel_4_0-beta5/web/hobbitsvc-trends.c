/*----------------------------------------------------------------------------*/
/* bbgen toolkit                                                              */
/*                                                                            */
/* This is a standalone tool for generating data for the LARRD overview page. */
/*                                                                            */
/* Copyright (C) 2002-2004 Henrik Storner <henrik@storner.dk>                 */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: hobbitsvc-trends.c,v 1.56 2005-01-15 17:38:55 henrik Exp $";

#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <utime.h>

#include "libbbgen.h"

int		log_nohost_rrds = 0;
int		sendmetainfo = 0;
namelist_t	*hosthead = NULL;
char		*larrdgraphs_default = NULL;;

typedef struct graph_t {
	larrdgraph_t *gdef;
	int count;
	struct graph_t *next;
} graph_t;

typedef struct larrd_dirstack_t {
	char *dirname;
	DIR *rrddir;
	struct larrd_dirstack_t *next;
} larrd_dirstack_t;
larrd_dirstack_t *dirs = NULL;

static larrd_dirstack_t *larrd_opendir(char *dirname)
{
	larrd_dirstack_t *newdir;
	DIR *d;

	d = opendir(dirname);
	if (d == NULL) return NULL;

	newdir = (larrd_dirstack_t *)xmalloc(sizeof(larrd_dirstack_t));
	newdir->dirname = xstrdup(dirname);
	newdir->rrddir = d;
	newdir->next = NULL;

	if (dirs == NULL) {
		dirs = newdir;
	}
	else {
		newdir->next = dirs;
		dirs = newdir;
	}

	return newdir;
}

static void larrd_closedir(void)
{
	larrd_dirstack_t *tmp = dirs;

	if (dirs && dirs->rrddir) {
		dirs = dirs->next;

		closedir(tmp->rrddir);
		xfree(tmp->dirname);
		xfree(tmp);
	}
}

static char *larrd_readdir(void)
{
	static char fname[PATH_MAX];
	struct dirent *d;
	struct stat st;

	if (dirs == NULL) return NULL;

	do {
		d = readdir(dirs->rrddir);
		if (d == NULL) {
			larrd_closedir();
		}
		else if (*(d->d_name) == '.') {
			d = NULL;
		}
		else {
			sprintf(fname, "%s/%s", dirs->dirname, d->d_name);
			if ((stat(fname, &st) == 0) && (S_ISDIR(st.st_mode))) {
				larrd_opendir(fname);
				d = NULL;
			}
		}
	} while (dirs && (d == NULL));

	if (d == NULL) return NULL;

	if (strncmp(fname, "./", 2) == 0) return (fname + 2); else return fname;
}


static char *rrdlink_text(namelist_t *host, graph_t *rrd, int larrd043, int hobbitd, int wantmeta)
{
	static char *rrdlink = NULL;
	static int rrdlinksize = 0;
	char *graphdef, *p;
	char *hostdisplayname, *hostlarrdgraphs;

	hostdisplayname = bbh_item(host, BBH_DISPLAYNAME);
	hostlarrdgraphs = bbh_item(host, BBH_LARRD);
	if (hostlarrdgraphs == NULL) hostlarrdgraphs = larrdgraphs_default;

	dprintf("rrdlink_text: host %s, rrd %s, larrd043=%d\n", host->bbhostname, rrd->gdef->larrdrrdname, larrd043);

	/* If no larrdgraphs definition, include all with default links */
	if (hostlarrdgraphs == NULL) {
		dprintf("rrdlink_text: Standard URL (no larrdgraphs)\n");
		return larrd_graph_data(host->bbhostname, hostdisplayname, NULL, rrd->gdef, rrd->count, larrd043, hobbitd, wantmeta);
	}

	/* Find this rrd definition in the larrdgraphs */
	graphdef = strstr(hostlarrdgraphs, rrd->gdef->larrdrrdname);

	/* If not found ... */
	if (graphdef == NULL) {
		dprintf("rrdlink_text: NULL graphdef\n");

		/* Do we include all by default ? */
		if (*(hostlarrdgraphs) == '*') {
			dprintf("rrdlink_text: Default URL included\n");

			/* Yes, return default link for this RRD */
			return larrd_graph_data(host->bbhostname, hostdisplayname, NULL, rrd->gdef, rrd->count, larrd043, hobbitd, wantmeta);
		}
		else {
			dprintf("rrdlink_text: Default URL NOT included\n");
			/* No, return empty string */
			return "";
		}
	}

	/* We now know that larrdgraphs explicitly define what to do with this RRD */

	/* Does he want to explicitly exclude this RRD ? */
	if ((graphdef > hostlarrdgraphs) && (*(graphdef-1) == '!')) {
		dprintf("rrdlink_text: This graph is explicitly excluded\n");
		return "";
	}

	/* It must be included. */
	if (rrdlink == NULL) {
		rrdlinksize = 4096;
		rrdlink = (char *)xmalloc(rrdlinksize);
	}

	*rrdlink = '\0';

	p = graphdef + strlen(rrd->gdef->larrdrrdname);
	if (*p == ':') {
		/* There is an explicit list of graphs to add for this RRD. */
		char savechar;
		char *enddef;
		graph_t *myrrd;
		char *partlink;

		myrrd = (graph_t *) xmalloc(sizeof(graph_t));
		myrrd->gdef = (larrdgraph_t *) xcalloc(1, sizeof(larrdgraph_t));

		/* First, null-terminate this graph definition so we only look at the active RRD */
		enddef = strchr(graphdef, ',');
		if (enddef) *enddef = '\0';

		graphdef = (p+1);
		do {
			p = strchr(graphdef, '|');			/* Ends at '|' ? */
			if (p == NULL) p = graphdef + strlen(graphdef);	/* Ends at end of string */
			savechar = *p; *p = '\0'; 

			myrrd->gdef->larrdrrdname = graphdef;
			myrrd->gdef->larrdpartname = NULL;
			myrrd->gdef->maxgraphs = 999;
			myrrd->count = 1;
			myrrd->next = NULL;
			partlink = larrd_graph_data(host->bbhostname, hostdisplayname, NULL, myrrd->gdef, myrrd->count, larrd043, hobbitd, wantmeta);
			if ((strlen(rrdlink) + strlen(partlink) + 1) >= rrdlinksize) {
				rrdlinksize += strlen(partlink) + 4096;
				rrdlink = (char *)xrealloc(rrdlink, rrdlinksize);
			}
			strcat(rrdlink, partlink);
			*p = savechar;

			graphdef = p;
			if (*graphdef != '\0') graphdef++;

		} while (*graphdef);

		if (enddef) *enddef = ',';
		xfree(myrrd->gdef);
		xfree(myrrd);

		return rrdlink;
	}
	else {
		/* It is included with the default graph */
		return larrd_graph_data(host->bbhostname, hostdisplayname, NULL, rrd->gdef, rrd->count, larrd043, hobbitd, wantmeta);
	}

	return "";
}


int generate_larrd(char *rrddirname, char *larrdcolumn, int larrd043, int hobbitd)
{
	char *fn;
	namelist_t *hostwalk;
	graph_t *rwalk;
	char *allrrdlinks, *allrrdlinksend;
	unsigned int allrrdlinksize;
	char *allmeta, *allmetaend;
	unsigned int allmetasize;

	dprintf("generate_larrd(rrddirname=%s, larrcolumn=%s, larrd043=%d\n",
		 rrddirname, larrdcolumn, larrd043);

	allrrdlinksize = 16384;
	allrrdlinks = (char *) xmalloc(allrrdlinksize);
	allrrdlinksend = allrrdlinks;
	allmetasize = 16384;
	allmeta = (char *) xmalloc(allmetasize);
	allmetaend = allmeta;

	/*
	 * General idea: Scan the RRD directory for all RRD files, and 
	 * pick up which RRD's are present for each host.
	 * Since there are only a limited set of possible RRD links to
	 * generate, this does not take up a huge hunk of memory.
	 * Then, loop over the list of hosts, and generate a log
	 * file and an html file for the larrd column.
	 */

	chdir(rrddirname);
	larrd_opendir(".");

	while ((fn = larrd_readdir())) {
		if ((strlen(fn) > 4) && (strcmp(fn+strlen(fn)-4, ".rrd") == 0)) {
			char *p, *rrdname;
			larrdgraph_t *r = NULL;
			int found, hostfound;

			dprintf("Got RRD %s\n", fn);

			/* Some logfiles use ',' instead of '.' in FQDN hostnames */
			p = fn; while ( (p = strchr(p, ',')) != NULL) *p = '.';

			/* Is this a known host? */
			hostwalk = hosthead; found = hostfound = 0;
			while (hostwalk && (!found)) {
				if (strncmp(hostwalk->bbhostname, fn, strlen(hostwalk->bbhostname)) == 0) {

					p = fn + strlen(hostwalk->bbhostname);
					hostfound = ( (*p == '.') || (*p == ',') || (*p == '/') );

					/* First part of filename matches.
					 * Now check that there is a valid RRD id next -
					 * if not, then we may have hit a partial hostname 
					 */

					rrdname = fn + strlen(hostwalk->bbhostname) + 1;
					r = find_larrd_graph(rrdname);
					found = (r != NULL);
				}

				if (!found) {
					hostwalk = hostwalk->next;
				}
			}

			if (found) {
				/* hostwalk now points to the host owning this RRD */
				for (rwalk = (graph_t *)hostwalk->data; (rwalk && (rwalk->gdef != r)); rwalk = rwalk->next) ;
				if (rwalk == NULL) {
					graph_t *newrrd = (graph_t *) xmalloc(sizeof(graph_t));

					newrrd->gdef = r;
					newrrd->count = 1;
					newrrd->next = (graph_t *)hostwalk->data;
					hostwalk->data = (void *)newrrd;
					rwalk = newrrd;
					dprintf("larrd: New rrd for host:%s, rrd:%s\n",
						hostwalk->bbhostname, r->larrdrrdname);
				}
				else {
					rwalk->count++;

					dprintf("larrd: Extra RRD for host %s, rrd %s   count:%d\n", 
						hostwalk->bbhostname, 
						rwalk->gdef->larrdrrdname, rwalk->count);
				}
			}

			if (!hostfound && log_nohost_rrds) {
				/* This rrd file has no matching host. */
				errprintf("No host record for rrd %s\n", fn);
			}
		}
	}

	chdir(getenv("BBLOGS"));

	if (hobbitd) {
		combo_start();
		if (sendmetainfo) meta_start();
	}

	for (hostwalk=hosthead; (hostwalk); hostwalk = hostwalk->next) {
		larrdgraph_t *graph;
		char *rrdlink;

		*allrrdlinks = '\0';
		allrrdlinksend = allrrdlinks;
		*allmeta = '\0';
		allmetaend = allmeta;

		graph = larrdgraphs;
		while (graph->larrdrrdname) {
			for (rwalk = (graph_t *)hostwalk->data; (rwalk && (rwalk->gdef->larrdrrdname != graph->larrdrrdname)); rwalk = rwalk->next) ;
			if (rwalk) {
				int buflen;

				buflen = (allrrdlinksend - allrrdlinks);
				rrdlink = rrdlink_text(hostwalk, rwalk, larrd043, hobbitd, 0);
				if ((buflen + strlen(rrdlink)) >= allrrdlinksize) {
					allrrdlinksize += (strlen(rrdlink) + 4096);
					allrrdlinks = (char *) xrealloc(allrrdlinks, allrrdlinksize);
					allrrdlinksend = allrrdlinks + buflen;
				}
				allrrdlinksend += sprintf(allrrdlinksend, "%s", rrdlink);

				if (hobbitd && sendmetainfo) {
					buflen = (allrrdlinksend - allrrdlinks);
					rrdlink = rrdlink_text(hostwalk, rwalk, larrd043, hobbitd, 1);
					if ((buflen + strlen(rrdlink)) >= allmetasize) {
						allmetasize += (strlen(rrdlink) + 4096);
						allmeta = (char *) xrealloc(allmeta, allmetasize);
						allmetaend = allmeta + buflen;
					}
					allmetaend += sprintf(allmetaend, "%s", rrdlink);
				}
			}

			graph++;
		}

		if (strlen(allrrdlinks) > 0) {
			do_savelog(hostwalk->bbhostname, hostwalk->ip, 
				larrdcolumn, allrrdlinks, hobbitd);
		}

		if (sendmetainfo && (strlen(allmeta) > 0)) {
			do_savemeta(hostwalk->bbhostname, larrdcolumn, "Graphs", allmeta);
		}
	}

	if (hobbitd) {
		combo_end();
		if (sendmetainfo) meta_end();
	}

	larrd_closedir();
	xfree(allrrdlinks);
	xfree(allmeta);
	return 0;
}


int main(int argc, char *argv[])
{
	int argi;
	char *bbhostsfn = NULL;
	char *rrddir = NULL;
	char *larrdcol = NULL;
	int usehobbitd = 0;

	getenv_default("USEHOBBITD", "FALSE", NULL);
	usehobbitd = (strcmp(getenv("USEHOBBITD"), "TRUE") == 0);

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--debug") == 0) {
			debug = dontsendmessages = 1;
		}
		else if (argnmatch(argv[argi], "--larrdgraphs=")) {
			char *p = strchr(argv[argi], '=');
			larrdgraphs_default = xstrdup(p+1);
		}
		else if (argnmatch(argv[argi], "--bbhosts=")) {
			char *p = strchr(argv[argi], '=');
			bbhostsfn = xstrdup(p+1);
		}
		else if (argnmatch(argv[argi], "--rrddir=")) {
			char *p = strchr(argv[argi], '=');
			rrddir = xstrdup(p+1);
		}
		else if (argnmatch(argv[argi], "--column=")) {
			char *p = strchr(argv[argi], '=');
			larrdcol = xstrdup(p+1);
		}
		else if (strcmp(argv[argi], "--hobbitd") == 0) {
			usehobbitd = 1;
		}
		else if (strcmp(argv[argi], "--meta") == 0) {
			sendmetainfo = 1;
		}
		else if (strcmp(argv[argi], "--no-update") == 0) {
			dontsendmessages = 1;
		}
	}

	if (bbhostsfn == NULL) bbhostsfn = getenv("BBHOSTS");
	if (rrddir == NULL) {
		char dname[PATH_MAX];

		sprintf(dname, "%s/rrd", getenv("BBVAR"));
		rrddir = xstrdup(dname);
	}
	if (larrdcol == NULL) larrdcol = "trends";

	hosthead = load_hostnames(bbhostsfn, NULL, get_fqdn(), NULL);

	/* First, generate the "trends" column for all hosts */
	generate_larrd(rrddir, larrdcol, 1, usehobbitd);

	/* Next, generate the per-host-per-status meta-info about graphs */
	if (usehobbitd && sendmetainfo) {
		char *statuslist = NULL;

		if (sendmessage("hobbitdlist", NULL, NULL, &statuslist, 1, BBTALK_TIMEOUT) == BB_OK) {
			char *curr, *next;
			char *host, *test, *graphlinks;
			larrdrrd_t *larrd = NULL;
			larrdgraph_t *graph = NULL;
			char *msgfmt = "%s";
			namelist_t *hostwalk;
			graph_t *rwalk;
			int linecount;

			meta_start();

			curr = statuslist;
			while (curr && (*curr != '\0')) {
				next = strchr(curr, '\n'); if (next) { *next = '\0'; next++; }

				host = curr;
				test = strchr(curr, '|');
				curr = next;

				if (test == NULL) continue;
				*test = '\0'; test++;

				larrd = find_larrd_rrd(test, NULL);
				if (larrd == NULL) continue;
				graph = find_larrd_graph(larrd->larrdrrdname);
				if (graph == NULL) continue;

				/* Lookup the data we collected previously for this host */
				rwalk = NULL;
				for (hostwalk=hosthead; (hostwalk && strcmp(hostwalk->bbhostname, host)); hostwalk = hostwalk->next) ;
				if (hostwalk) for (rwalk = (graph_t *)hostwalk->data; (rwalk && (rwalk->gdef != graph)); rwalk = rwalk->next) ;
				if (rwalk) linecount = rwalk->count; else linecount = 1;

				graphlinks =  larrd_graph_data(host, host, test, graph, linecount, 1, 1, 1);
				if (strlen(graphlinks) > 0) {
					char *msg = (char *)xmalloc(strlen(graphlinks) + strlen(msgfmt) + 1);

					sprintf(msg, msgfmt, graphlinks);
					do_savemeta(host, test, "Graphs", msg);
					xfree(msg);
				}
			}

			meta_end();
		}
	}

	return 0;
}

