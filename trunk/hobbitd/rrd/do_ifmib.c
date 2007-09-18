/*----------------------------------------------------------------------------*/
/* Hobbit RRD handler module.                                                 */
/*                                                                            */
/* Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char ifmib_rcsid[] = "$Id: do_ifmib.c,v 1.2 2007-09-11 14:29:04 henrik Exp $";

static char *ifmib_params[] = { 
	                        "DS:ifInNUcastPkts:DERIVE:600:0:U", 
	                        "DS:ifInDiscards:DERIVE:600:0:U", 
	                        "DS:ifInErrors:DERIVE:600:0:U", 
				"DS:ifInUnknownProtos:DERIVE:600:0:U", 
	                        "DS:ifOutNUcastPkts:DERIVE:600:0:U", 
	                        "DS:ifOutDiscards:DERIVE:600:0:U", 
	                        "DS:ifOutErrors:DERIVE:600:0:U", 
	                        "DS:ifOutQLen:GAUGE:600:0:U", 
				"DS:ifInMcastPkts:DERIVE:600:0:U",
				"DS:ifInBcastPkts:DERIVE:600:0:U",
				"DS:ifOutMcastPkts:DERIVE:600:0:U",
				"DS:ifOutBcastPkts:DERIVE:600:0:U",
				"DS:ifHCInMcastPkts:DERIVE:600:0:U",
				"DS:ifHCInBcastPkts:DERIVE:600:0:U",
				"DS:ifHCOutMcastPkts:DERIVE:600:0:U",
				"DS:ifHCOutBcastPkts:DERIVE:600:0:U",
				"DS:InOctets:DERIVE:600:0:U",
	                        "DS:OutOctets:DERIVE:600:0:U", 
				"DS:InUcastPkts:DERIVE:600:0:U", 
	                        "DS:OutUcastPkts:DERIVE:600:0:U", 
			 	NULL };
static char *ifmib_tpl      = NULL;

static char *ifmib_valnames[] = {
	/* These are in the standard interface MIB */
	"ifInNUcastPkts", 	/*  0 */
	"ifInDiscards", 
	"ifInErrors",
	"ifInUnknownProtos",
	"ifOutNUcastPkts",  	/*  4 */
	"ifOutDiscards", 
	"ifOutErrors",
	"ifOutQLen", 
	/* The following are the 64-bit counters in the extended MIB */
	"ifInMulticastPkts",   	/*  8 */
	"ifInBroadcastPkts",
	"ifOutMulticastPkts",
	"ifOutBroadcastPkts",
	"ifHCInMulticastPkts", 	/* 12 */
	"ifHCInBroadcastPkts",
	"ifHCOutMulticastPkts",
	"ifHCOutBroadcastPkts",
	/* The following counters may be in both 32- (standard) and 64-bit (extended) versions. */
	"ifInOctets", 		/* 16 */
	"ifHCInOctets",
	"ifOutOctets", 
	"ifHCOutOctets",
	"ifInUcastPkts", 	/* 20 */
	"ifHCInUcastPkts",
	"ifOutUcastPkts", 
	"ifHCOutUcastPkts",
	NULL
};

static void ifmib_flush_data(char *devname, time_t tstamp, char *hostname, char *testname, char **values, 
			     int inidx, int outidx, int inUcastidx, int outUcastidx)
{
	setupfn("ifmib.%s.rrd", devname);
	sprintf(rrdvalues, "%d:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s",
		(int)tstamp,
		values[0], values[1], values[2], values[3],
		values[4], values[5], values[6], values[7],
		values[8], values[9], values[10], values[11],
		values[12], values[13], values[14], values[15],
		values[inidx], values[outidx], values[inUcastidx], values[outUcastidx]);
	create_and_update_rrd(hostname, testname, ifmib_params, ifmib_tpl);
}

int do_ifmib_rrd(char *hostname, char *testname, char *msg, time_t tstamp)
{
	char *datapart = msg;
	char *bol, *eoln;
	char *devname = NULL;
	char *values[sizeof(ifmib_valnames)/sizeof(ifmib_valnames[0])];
	int valcount = 0;
	int incountidx = 16, outcountidx = 18, inUcastidx = 20, outUcastidx = 22;

	if (ifmib_tpl == NULL) ifmib_tpl = setup_template(ifmib_params);

	if ((strncmp(msg, "status", 6) == 0) || (strncmp(msg, "data", 4) == 0)) {
		/* Skip the first line of full status- and data-messages. */
		datapart = strchr(msg, '\n');
		if (datapart) datapart++; else datapart = msg;
	}

	memset(values, 0, sizeof(values));
	valcount = 0;
	devname = NULL;

	bol = datapart;
	while (bol) {
		eoln = strchr(bol, '\n'); if (eoln) *eoln = '\0';
		bol += strspn(bol, " \t");
		if (*bol == '\0') {
			/* Nothing */
		}
		else if (*bol == '[') {
			/* New interface data begins */
			if (devname && (valcount == 24)) {
				ifmib_flush_data(devname, tstamp, hostname, testname, values, 
						 incountidx, outcountidx, inUcastidx, outUcastidx);
				memset(values, 0, sizeof(values));
				valcount = 0;
				devname = NULL;
				incountidx = 16; outcountidx = 18; inUcastidx = 20; outUcastidx = 22;
			}

			devname = bol+1; bol = strchr(bol, ']'); if (bol) *bol = '\0';
		}
		else {
			char *valnam, *valstr = NULL;

			valnam = strtok(bol, " =");
			if (valnam) valstr = strtok(NULL, " =");

			if (valnam && valstr) {
				int validx;
				for (validx = 0; (ifmib_valnames[validx] && strcmp(ifmib_valnames[validx], valnam)); validx++) ;
				if (ifmib_valnames[validx]) {
					values[validx] = (isdigit(*valstr) ? valstr : "U");
					valcount++;
					
					/* See if this is one of the high-speed in/out counts */
					if (*values[validx] != 'U') {
						if (validx == 17) incountidx = validx;
						if (validx == 19) outcountidx = validx;
						if (validx == 21) inUcastidx = validx;
						if (validx == 23) outUcastidx = validx;
					}
				}
			}
		}

nextline:
		bol = (eoln ? eoln+1 : NULL);
	}

	/* Flush the last device */
	if (devname && (valcount == 24)) {
		ifmib_flush_data(devname, tstamp, hostname, testname, values, 
				 incountidx, outcountidx, inUcastidx, outUcastidx);
		valcount = 0;
	}

	return 0;
}
