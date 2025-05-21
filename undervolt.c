/*  undervolt.c
 *  copyright 2011	Thierry Goubier <thierry <point> goubier <at> gmail>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *    Vincent Stehlé <vincent.stehle@laposte.net>
 *    Add frequency support.
 *
 *    Antonio Paiva
 *    C-60 support.
 *
 *****************************************************************************/
 /* This program manipulates Vid values for CPU P-states on AMD Family
  * 14h processors (Model names C-30, C-50, C-60, E-240, E-350, E-450).
  * Reference documentation:
  * [1] BIOS and Kernel Developers Guide for AMD Family 14h Models
  *      00h-0Fh Processors, 43170 Rev 3.06 - March 16, 2011.
  */

#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdint.h>



int wrmsr(int cpu, off_t msr, uint64_t val);
int rdmsr(int cpu, off_t msr, uint64_t * val);

static int verbose = 0, ncpu = 0;

/** voltage
 * 
 * Returns a voltage out of a vid. The formula is said to come from a
 * document under NDA, but the formula for the svid encoding in the
 * AMD Family 10h family BIOS and Kernel developers guide seems
 * to be the right one.
 */
static double voltage(long SviVid) {
	if(SviVid <= 0x7F && SviVid >= 0x7C)
		return 0;
	else return 1.550 - 0.0125 * SviVid;
}

/** usage
 *
 * Display a text describing all options to the program and exit.
 */
static void usage(const char * progName) {
	fprintf(stderr, "Usage: %s [-c] [-r] [-v] [-p <P-state no>:<Vid>]\n"
	"\t-c\tDisplay information on the current P-state for all cpu cores.\n"
	"\t-h\tDisplay this information.\n"
	"\t-r\tRead information from all valid P-states.\n"
	"\t-v\tVerbose. Display information on all reads and writes to\n"
	"\t\tregisters.\n"
	"\t-p <P-state no>:<Vid>[,<div>]\n"
	"\t\tSet Vid (and if supplied, div) for the P-state no for all cores.\n", progName);
	exit(1);
}

/* cpuIdCheck
 *
 * This function ensures we are on the right type of CPU. Uses /proc/cpuinfo
 * to retrieve the information about cpus without using the cpuid instruction.
 * Could check if the CPU has hwpstate in the power management line.
 */
static int cpuIdCheck() {
	FILE * stream;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	char * vendor_id = "AuthenticAMD";
	char * s;
	int vendorChecked = 0, familyChecked = 0;

		/* open /dev/cpuinfo */
	s = malloc(512);
	if((stream = fopen("/proc/cpuinfo", "r")) == NULL) {
		perror("Opening /proc/cpuinfo");
		exit(1);
	}
		/* read each line with getline() */
	while((read = getline(&line, &len, stream)) != -1) {
		/* match first part of line with target and update data */
		/* check data */
		if (strncmp(line, "vendor_id", strlen("vendor_id")) == 0) {
			sscanf(line, "vendor_id : %s", s);
			if(strncmp(s, vendor_id, strlen(vendor_id)) != 0) {
				fprintf(stderr, "vendor_id %s is not supported\n", s);
				return(1);
			}
			else {
				if(verbose) printf("vendor_id checked\n");
				vendorChecked = 1;
			}
		}
		if (strncmp(line, "cpu family", strlen("cpu family")) == 0) {
			int f, r;
			r = sscanf(line, "cpu family : %d", &f);
			if((r != 1) || (f != 0x14)) {
				fprintf(stderr, "cpu family %xd is not supported\n", f);
				return(1);
			}
			else {
				if(verbose) printf("cpu family checked\n");
				familyChecked = 1;
			}
		}
			/* Model check: 1 is B0 stepping (C-30, C-50, E-350), 2 is
			 * C0 stepping (C-60, E-450). */
		if(strncmp(line, "model\t\t:", strlen("model\t\t:")) == 0) {
			int m, r;
			r = sscanf(line, "model : %d", &m);
			if((r != 1) || ((m != 1) && (m != 2))) {
				fprintf(stderr, "cpu model %xd is not supported\n", m);
				return(1);
			}
			else {
				if(verbose) printf("cpu model checked\n");
			}
		}
		if(strncmp(line, "cpu cores\t:", strlen("cpu cores\t:")) == 0) {
			int r;
			r = sscanf(line, "cpu cores : %d", &ncpu);
			if(r != 1 || ncpu == 0) {
				fprintf(stderr, "Error reading number of cores\n");
				exit(1);
			}
			else {
				if(verbose) printf("retrieved number of cores: %d\n", ncpu);
			}
		}
			/** End the scanning once we have done the first cpu. It's not a
			 * a problem to do the whole cpuinfo for two cores, but once you
			 * end up on a 48 cores system, scanning the whole /proc/cpuinfo
			 * is not very efficient :wink: */
		if(vendorChecked && familyChecked && ncpu)
			break;
	}
	free(s);
	free(line);
	fclose(stream);
	return 0;
}

/* Check DidMSD + DidLSD and complain if necessary. */
void checkdid(unsigned msd, unsigned lsd)
{

    if(verbose) printf("msd %u, lsd %u\n", msd, lsd);

    if(msd > 0x19)
        printf("Strange DidMSD %x > 0x19?\n", msd);

    if(lsd > 3)
        printf("Strange DidLSD %x > 3?\n", lsd);
}

/* Compute div from MSR values (DidMSD + DidLSD). */
float msrtodiv(uint64_t val)
{
    unsigned didmsd, didlsd;

    // DID is in two parts.
    didmsd = (val >> 4) & 0x1f;
    didlsd = val & 0xf;
    checkdid(didmsd, didlsd);

    // Divisor.
    return (float)didmsd + ((float)didlsd * 0.25) + 1;
}

/* Compute MSR values (DidMSD + DidLSD) from div. */
void divtomsr(float div, uint64_t *msr)
{
    unsigned didmsd, didlsd;

    // DID is in two parts.
    didmsd = div - 1;
    div -= (int)div;
    didlsd = div * 4;
    checkdid(didmsd, didlsd);

    *msr = (*msr & ~(uint64_t)0x1ff) | (didmsd << 4) | didlsd;
}

/** main
 *
 * setup, scan command line options, check the validity of the command
 * line options, and apply the commands. */
int main (int argc, char **argv)
{
	int i, j, o, pstateId, vid, n = 0, maxPstate, minPstate, read = 0, current = 0;
	uint64_t val,
			/** There is a max of 8 P-states in Family 14h. */
		vidToSet[8] = {0, 0, 0, 0, 0, 0, 0, 0},
			/** A placeholder to hold the MSR values read. */
		oMSR[8];
			/** The MSR register addresses. */
	off_t aMSR[8] = {0xC0010064, 0xC0010065, 0xC0010066, 0xC0010067, 0xC0010068, 0xC0010069, 0xC001006A, 0xC001006B};
    float div,
		divToSet[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	
	while((o = getopt(argc, argv, "hcvrp:")) != -1){
 		switch(o){
 		case 'h':
 			usage(argv[0]);
 			exit(0);
 		case 'v':
 			verbose = 1;
 			break;
 		case 'r':
 			read = 1;
 			break;
 		case 'c':
 			current = 1;
 			break;
 		case 'p':
 			div = 0.0;
			n = sscanf(optarg, "%1d:%i,%f", &pstateId, &vid, &div);
			if(n != 2 && n != 3) {
				fprintf(stderr, "Error parsing '%s', it should be coreid:vid[,div]\n", optarg);
				exit(1);
			}
 			if(pstateId < 0 || pstateId >= 8) {
 				fprintf(stderr, "P-state %d is out of bounds\n", pstateId);
 				exit(1);
 			}
 			if(vidToSet[pstateId] != 0) {
 				fprintf(stderr, "Duplicate -p %d: option\n", pstateId);
 				exit(1);
 			}
 			vidToSet[pstateId] = vid;
 			divToSet[pstateId] = div;
 			if(verbose) printf("vid 0x%x/%d / %.4fV, div %.02f to set for pstate %d\n", vid, vid, voltage(vid), div, pstateId);
 			break;
 		default:
 			if(optind != (argc -1)){
 				fprintf(stderr, "Invalid argument %s\n", argv[optind]);
 				usage(argv[0]);
 				exit(EXIT_FAILURE);
 			}
 			else {
	 			usage(argv[0]);
	 			exit(0);
 			}
 		}
    }
	cpuIdCheck();
		/** Get maxPstate and minPstate. */
	if(rdmsr(0, 0xC0010061, &val)) {
		fprintf(stderr, "Failed reading msr register. Is the msr module loaded?\n");
		exit(1);
	}
	maxPstate = (val & 0x70)>> 4;
	minPstate = val & 0x07;
	if(minPstate != 0) {
		if(verbose) printf("Beware! Highest performance P-states are desactivated.\n");
	}
		/* Now check to see if the input is correct. */
	for(i = 0; i < 8; i++) {
		if(vidToSet[i] != 0 && (i > maxPstate || i < minPstate)) {
			fprintf(stderr, "Error: P-state %d is not valid\n", i);
		}
	}
		/* read command : read the MSR registers and display all */
	if(read) {
		printf("P-state\t\tVid\t\tVoltage\t\tdiv\n");
		for(i = minPstate; i <= maxPstate; i++) {
			if(rdmsr(0, aMSR[i], &(oMSR[i]))) {
				fprintf(stderr, "Error reading msr registers\n");
				exit(1);
			}
			printf("  %d\t\t0x%" PRIX64 "\t\t%.4fV\t\t%.02f\n", i, (oMSR[i] >> 9) & 0x7F, voltage((oMSR[i] >> 9) & 0x7F), msrtodiv(oMSR[i]));
		}
	}
		/* write new Vid values in MSR registers, if any has been set. */
	for(i = minPstate; i <= maxPstate; i++) {
		long vid;
		if(vidToSet[i] != 0) {
				/* Interesting : writing to a single cpu MSR register change the
				 * other, so the loop for all cpus should not be necessary ? */
			for(j = 0; j < ncpu; j++) {
				if(rdmsr(j, aMSR[i], &(oMSR[i]))) {
					fprintf(stderr, "Error reading MSR register\n");
					exit(1);
				}
				vid = (oMSR[i] >> 9) & 0x7F;
				val = (oMSR[i] & (~(0x7F << 9))) | (vidToSet[i] << 9);
				printf("P-state: %d, cpu: %d, changing vid: 0x%lX/%.4fV", i, j, vid, voltage(vid));
				if(divToSet[i] != 0.0)
					printf(", div: %.02f\n", msrtodiv(oMSR[i]));
				printf(" to 0x%" PRIX64 "/%.4fV", vidToSet[i], voltage(vidToSet[i]));
                if(divToSet[i] != 0.0){
				   printf(", div: %.02f\n", div);
                    divtomsr(divToSet[i], &val);
                }
                else
                	printf("\n");
				if(wrmsr(j, aMSR[i], val)) {
					fprintf(stderr, "Error writing MSR register\n");
					exit(1);
				}
			}
		}
	}
		/* Command -c : read the current state of the cpu cores. */
	if(current) {
		for(i = 0; i < ncpu; i++) {
			if(rdmsr(i, 0xC0010071, &val)) {
				fprintf(stderr, "Error reading MSR register 0x%X\n", 0xC0010071);
				exit(1);
			}
			printf("CPU %d: current P-state: %" PRIu64 ", current Vid: 0x%" PRIX64 "/%.4fV, current div: %.02f\n", i, (val >> 16) & 0x03, (val >> 9) & 0x7F, voltage((val >> 9) & 0x7F), msrtodiv(val));
		}
	}
	exit(0);
}

/** wrmsr
 *
 * This function writes an msr register according to its parameters. Uses
 * /dev/cpu/cpu_no/msr. Requires root privileges. */
int wrmsr(int cpu, off_t msr, uint64_t val) {
	int fd;
	ssize_t error;
	char path[512];

	snprintf(path, 512, "/dev/cpu/%d/msr", cpu);
	if(verbose)
		printf("cpu %d msr %" PRIX64 " value %" PRIX64 " path %s\n", cpu, msr, val, path);
	if ((fd = open(path, O_RDWR)) < 0) {
		perror("Accessing msr device");
		return (1);
	}
	if (lseek(fd, msr, SEEK_SET) < 0) {
		perror("Seek in msr");
		return (1);
	}
	error = write(fd, &val, sizeof(val));
	if (error < (ssize_t)sizeof(val)) {
		perror("Write msr register");
		return (1);
	}
	if(verbose)
		printf("msr %" PRIX64 " = %" PRIX64 "\n", msr, val);
	if(close(fd)) {
		perror("Closing msr device");
		return(1);
	}
	return (0);
}

/** rdmsr
 *
 * This function reads an msr register according to its parameters. Uses
 * /dev/cpu/cpu_no/msr. Requires root privileges. */
int rdmsr(int cpu, off_t msr, uint64_t * pVal) {
	int fd;
	ssize_t error;
	char path[512];

	snprintf(path, 512, "/dev/cpu/%d/msr", cpu);
	if(verbose)
		printf("cpu %d msr %" PRIX64 " path %s\n", cpu, msr, path);
	if ((fd = open(path, O_RDWR)) < 0) {
		perror("Open msr device");
		return (1);
	}
	if (lseek(fd, msr, SEEK_SET) < 0) {
		perror("Seek to msr register");
		return (1);
	}
	error = read(fd, pVal, sizeof(* pVal));
	if (error < (ssize_t)sizeof(* pVal)) {
		perror("Read msr register");
		return (1);
	}
	if(verbose)
		printf("msr %" PRIX64 " = %" PRIX64 "\n", msr, *pVal);
	if(close(fd)) {
		perror("Closing msr device");
		return (1);
	}
	return (0);
}
