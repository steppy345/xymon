/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2004 Henrik Storner <henrik@hswn.dk>                         */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char bbgen_rcsid[] = "$Id: do_bbgen.c,v 1.7 2005-02-06 08:49:02 henrik Exp $";

static char *bbgen_params[] = { "rrdcreate", rrdfn, "DS:runtime:GAUGE:600:0:U", rra1, rra2, rra3, rra4, NULL };

int do_bbgen_larrd(char *hostname, char *testname, char *msg, time_t tstamp) 
{ 
	char	*p;
	float	runtime;

	p = strstr(msg, "TIME TOTAL");
	if (p && (sscanf(p, "TIME TOTAL %f", &runtime) == 1)) {
		if (strcmp("bbgen", testname) != 0) {
			sprintf(rrdfn, "bbgen.%s.rrd", testname);
		}
		else {
			strcpy(rrdfn, "bbgen.rrd");
		}
		sprintf(rrdvalues, "%d:%.2f", (int)tstamp, runtime);
		return create_and_update_rrd(hostname, rrdfn, bbgen_params, update_params);
	}

	return 0;
}
