/*----------------------------------------------------------------------------*/
/* Hobbit message daemon.                                                     */
/*                                                                            */
/* Client backend module for Linux                                            */
/*                                                                            */
/* Copyright (C) 2005-2006 Henrik Storner <henrik@hswn.dk>                    */
/*                                                                            */
/* This program is released under the GNU General Public License (GPL),       */
/* version 2. See the file "COPYING" for details.                             */
/*                                                                            */
/*----------------------------------------------------------------------------*/

static char linux_rcsid[] = "$Id: linux.c,v 1.19 2006-06-04 10:52:52 henrik Exp $";

void handle_linux_client(char *hostname, char *clienttype, enum ostype_t os, 
			 namelist_t *hinfo, char *sender, time_t timestamp,
			 char *clientdata)
{
	char *timestr;
	char *uptimestr;
	char *clockstr;
	char *whostr;
	char *psstr;
	char *topstr;
	char *dfstr;
	char *freestr;
	char *msgsstr;
	char *netstatstr;
	char *vmstatstr;
	char *ifstatstr;
	char *portsstr;

	char fromline[1024];

	sprintf(fromline, "\nStatus message received from %s\n", sender);

	splitmsg(clientdata);

	timestr = getdata("date");
	uptimestr = getdata("uptime");
	clockstr = getdata("clock");
	whostr = getdata("who");
	psstr = getdata("ps");
	topstr = getdata("top");
	dfstr = getdata("df");
	freestr = getdata("free");
	msgsstr = getdata("msgs");
	netstatstr = getdata("netstat");
	ifstatstr = getdata("ifstat");
	vmstatstr = getdata("vmstat");
	portsstr = getdata("ports");

	unix_cpu_report(hostname, clienttype, os, hinfo, fromline, timestr, uptimestr, clockstr, whostr, psstr, topstr);
	unix_disk_report(hostname, clienttype, os, hinfo, fromline, timestr, "Available", "Capacity", "Mounted", dfstr);
	unix_procs_report(hostname, clienttype, os, hinfo, fromline, timestr, "CMD", NULL, psstr);
	unix_ports_report(hostname, clienttype, os, hinfo, fromline, timestr, 3, 4, 5, portsstr);

	msgs_report(hostname, clienttype, os, hinfo, fromline, timestr, msgsstr);
	file_report(hostname, clienttype, os, hinfo, fromline, timestr);
	linecount_report(hostname, clienttype, os, hinfo, fromline, timestr);

	unix_netstat_report(hostname, clienttype, os, hinfo, fromline, timestr, netstatstr);
	unix_ifstat_report(hostname, clienttype, os, hinfo, fromline, timestr, ifstatstr);
	unix_vmstat_report(hostname, clienttype, os, hinfo, fromline, timestr, vmstatstr);

	if (freestr) {
		char *p;
		long memphystotal, memphysused, memphysfree,
		     memactused, memactfree,
		     memswaptotal, memswapused, memswapfree;

		memphystotal = memswaptotal = memphysused = memswapused = memactused = memactfree = -1;
		p = strstr(freestr, "\nMem:");
		if (p && (sscanf(p, "\nMem: %ld %ld %ld", &memphystotal, &memphysused, &memphysfree) == 3)) {
			memphystotal /= 1024;
			memphysused /= 1024;
			memphysfree /= 1024;
		}
		p = strstr(freestr, "\nSwap:");
		if (p && (sscanf(p, "\nSwap: %ld %ld %ld", &memswaptotal, &memswapused, &memswapfree) == 3)) {
			memswaptotal /= 1024;
			memswapused /= 1024;
			memswapfree /= 1024;
		}
		p = strstr(freestr, "\n-/+ buffers/cache:");
		if (p && (sscanf(p, "\n-/+ buffers/cache: %ld %ld", &memactused, &memactfree) == 2)) {
			memactused /= 1024;
			memactfree /= 1024;
		}

		unix_memory_report(hostname, clienttype, os, hinfo, fromline, timestr,
				   memphystotal, memphysused, memactused, memswaptotal, memswapused);
	}
}

