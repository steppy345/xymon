/*----------------------------------------------------------------------------*/
/* Hobbit bb-hosts file grep'er                                               */
/*                                                                            */
/* This tool will pick out the hosts from a bb-hosts file that has one of the */
/* tags given on the command line. This allows an extension script to deal    */
/* with only the relevant parts of the bb-hosts file, instead of having to    */
/* parse the entire file.                                                     */
/*                                                                            */
/* Copyright (C) 2003-2005 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char rcsid[] = "$Id: bbhostgrep.c,v 1.29 2005-08-15 05:59:12 henrik Exp $";

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "version.h"
#include "libbbgen.h"

int main(int argc, char *argv[])
{ 
	namelist_t *hostlist = NULL, *hwalk;
	char *bbhostsfn = NULL;
	char *netstring = NULL;
	char *include2 = NULL;
	int extras = 1;
	int testuntagged = 0;
	char *p;
	char **lookv;
	int argi, lookc;

	if ((argc <= 1) || (strcmp(argv[1], "--help") == 0)) {
		printf("Usage:\n%s test1 [test1] [test2] ... \n", argv[0]);
		exit(1);
	}

	lookv = (char **)malloc(argc*sizeof(char *));
	lookc = 0;

	bbhostsfn = xgetenv("BBHOSTS");

	for (argi=1; (argi < argc); argi++) {
		if (strcmp(argv[argi], "--noextras") == 0) {
			extras = 0;
		}
		else if (strcmp(argv[argi], "--test-untagged") == 0) {
			testuntagged = 1;
		}
		else if (strcmp(argv[argi], "--version") == 0) {
			printf("bbhostgrep version %s\n", VERSION);
			exit(0);
		}
		else if (strcmp(argv[argi], "--bbnet") == 0) {
			include2 = "netinclude";
		}
		else if (strcmp(argv[argi], "--bbdisp") == 0) {
			include2 = "dispinclude";
		}
		else if (argnmatch(argv[argi], "--bbhosts=")) {
			bbhostsfn = strchr(argv[argi], '=') + 1;
		}
		else {
			lookv[lookc] = strdup(argv[argi]);
			lookc++;
		}
	}
	lookv[lookc] = NULL;

	if ((bbhostsfn == NULL) || (strlen(bbhostsfn) == 0)) {
		errprintf("Environment variable BBHOSTS is not set - aborting\n");
		exit(2);
	}

	hostlist = load_hostnames(bbhostsfn, include2, get_fqdn());
	if (hostlist == NULL) {
		errprintf("Cannot load bb-hosts, or file is empty\n");
		exit(3);
	}

	/* Each network test tagged with NET:locationname */
	p = xgetenv("BBLOCATION");
	if (p && strlen(p)) netstring = strdup(p);

	hwalk = hostlist;
	while (hwalk) {
		char *curnet = bbh_item(hwalk, BBH_NET);
		char *curname = bbh_item(hwalk, BBH_HOSTNAME);

		/* Only look at the hosts whose NET: definition matches the wanted one */
		if ( (netstring == NULL) || 
		     (curnet && netstring && (strcmp(curnet, netstring) == 0)) || 
		     (testuntagged && (curnet == NULL)) ) {
			char *item;
			char *wantedtags = NULL;
			int wantedtagsz;

			for (item = bbh_item_walk(hwalk); (item); item = bbh_item_walk(NULL)) {
				int i;
				char *realitem = item + strspn(item, "!~?");

				for (i=0; lookv[i]; i++) {
					char *outitem = NULL;

					if (lookv[i][strlen(lookv[i])-1] == '*') {
						if (strncasecmp(realitem, lookv[i], strlen(lookv[i])-1) == 0) {
							outitem = (extras ? item : realitem);
						}
					}
					else if (strcasecmp(realitem, lookv[i]) == 0) {
						outitem = (extras ? item : realitem);
					}

					if (outitem) {
						int needquotes = ((strchr(outitem, ' ') != NULL) || (strchr(outitem, '\t') != NULL));
						addtobuffer(&wantedtags, &wantedtagsz, " ");
						if (needquotes) addtobuffer(&wantedtags, &wantedtagsz, "\"");
						addtobuffer(&wantedtags, &wantedtagsz, outitem);
						if (needquotes) addtobuffer(&wantedtags, &wantedtagsz, "\"");
					}
				}
			}

			if (wantedtags && (*wantedtags != '\0') && extras) {
				if (bbh_item(hwalk, BBH_FLAG_DIALUP)) addtobuffer(&wantedtags, &wantedtagsz, " dialup");
				if (bbh_item(hwalk, BBH_FLAG_TESTIP)) addtobuffer(&wantedtags, &wantedtagsz, " testip");
				if ((item = bbh_item(hwalk, BBH_DOWNTIME)) != NULL) {
					if (within_sla(item, 0))
						addtobuffer(&wantedtags, &wantedtagsz, " OUTSIDESLA");
					else
						addtobuffer(&wantedtags, &wantedtagsz, " INSIDESLA");
				}
			}

			if (wantedtags) {
				if (*wantedtags != '\0') {
					printf("%s %s #%s\n", bbh_item(hwalk, BBH_IP), bbh_item(hwalk, BBH_HOSTNAME), wantedtags);
				}

				xfree(wantedtags);
			}
		}

		do { hwalk = hwalk->next; } while (hwalk && (strcmp(curname, hwalk->bbhostname) == 0));
	}

	return 0;
}

