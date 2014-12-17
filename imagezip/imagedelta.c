/*
 * Copyright (c) 2000-2014 University of Utah and the Flux Group.
 * 
 * {{{EMULAB-LICENSE
 * 
 * This file is part of the Emulab network testbed software.
 * 
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this file.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * }}}
 */

#define FIXMAP_DEBUG

/*
 * imagedelta [ -S ] image1.ndz image2.ndz delta1to2.ndz
 *
 * Take two images (image1, image2) and produce a delta (delta1to2)
 * based on the differences. The -S option says to use the signature
 * files if possible to determine differences between the images.
 * Otherwise we compare the corresponding areas of both images to
 * determine if they are different.
 *
 * Note that order matters here! We are generating a delta to get from
 * "image1" to "image2"; i.e., doing:
 *
 *  imageunzip image1.ndz /dev/da0
 *  imageunzip delta1to2.ndz /dev/da0
 *
 * would be identical to:
 *
 *  imageunzip image2.ndz /dev/da0
 *
 * Approach:
 *
 * We scan the chunks headers of both images (image1, image2) to produce
 * allocated range lists for both (R1, R2). We use these to produce a
 * range list for the delta (RD) as follows.
 *
 * - Anything that is in R1 but not R2 does not go in RD.
 * - Anything in R2 but not in R1 must go into RD.
 * - For overlapping areas, we read and hash or compare both and,
 *   if different, include in RD.
 * - Using RD, select data from image2 that need to be read, decompressed
 *   and then recompressed into the new image.
 *
 * There is the usual issue of dealing with the difference in granularity
 * and alignment of ranges (arbitrary multiples of 512 byte) vs. hash
 * blocks (64K byte), but that logic exists in imagezip today.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <zlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#ifndef NOTHREADS
#include <pthread.h>
#endif

#include "imagehdr.h"
#include "libndz/libndz.h"

struct ifileinfo {
    struct ndz_file *ndz;
    int sigfd;
    struct ndz_rangemap *map, *sigmap;
} ndz1, ndz2;

struct mergestate {
    unsigned int ochunkno;
    unsigned int nchunkno;
};

int usesigfiles = 0;

static int fixmap(struct ndz_rangemap *map, struct ndz_range *range, void *arg);

void
usage(void)
{
    fprintf(stderr,
	    "Usage: imagedelta [-S] image1.ndz image2.ndz delta1to2.ndz\n"
	    "\n"
	    "Produce a delta image (delta1to2) containing the changes\n"
	    "necessary to get from image1 to image2.\n"
	    "\n"
	    "  -S   Use signature files when computing differences.\n");
    exit(1);
}

/*
 * File must exist and be readable.
 * If usesigfiles is set, signature file must exist as well.
 * Reads in the range map and signature as well.
 */
void
openifile(char *file, struct ifileinfo *info)
{
    int rv;

    info->ndz = ndz_open(file, 0);
    if (info->ndz == NULL) {
	fprintf(stderr, "%s: could not open as NDZ file\n",
		ndz_filename(info->ndz));
	exit(1);
    }

    /* XXX check sigfile */
    if (usesigfiles) {
	;
    }

    /* read range info from image */
    rv = ndz_readranges(info->ndz, &info->map);
    if (rv) {
	fprintf(stderr, "%s: could not read ranges\n",
		ndz_filename(info->ndz));
	exit(1);
    }

    /* read signature info */
    if (usesigfiles) {
	;
    }
}

static void
chunkfunc(void *ptr)
{
    unsigned int chunkno = (int)ptr;
    printf("chunkno=%u", chunkno);
}

static void
twochunkfunc(void *ptr)
{
    struct mergestate *state = (struct mergestate *)ptr;
    printf("ochunkno=%u, nchunkno=%u", state->ochunkno, state->nchunkno);
}

int
main(int argc, char **argv)
{
    int ch, version = 0;
    extern char build_info[];
    int delta, rv;
    struct ndz_rangemap *mmap;

    while ((ch = getopt(argc, argv, "Sv")) != -1)
	switch(ch) {
	case 'S':
	    usesigfiles = 1;
	    break;
	case 'v':
	    version++;
	    break;
	case 'h':
	case '?':
	default:
	    usage();
	}
    argc -= optind;
    argv += optind;

    if (argc < 3)
	usage();

    /*
     * Make sure we can open all the files
     */
    ndz1.ndz = ndz_open(argv[0], 0);
    if (ndz1.ndz == NULL)
	usage();

    ndz2.ndz = ndz_open(argv[1], 0);
    if (ndz2.ndz == NULL)
	usage();

    delta = open(argv[2], O_RDWR|O_CREAT|O_TRUNC, 0666);
    if (delta < 0) {
	perror(argv[2]);
	usage();
    }

    /*
     * Read in the range and signature info.
     */
    ndz1.map = ndz_readranges(ndz1.ndz);
    if (ndz1.map == NULL) {
	fprintf(stderr, "%s: could not read ranges\n",
		ndz_filename(ndz1.ndz));
	exit(1);
    }
#if 1
    printf("==== Old ");
    ndz_rangemap_dump(ndz1.map, chunkfunc);
    fflush(stdout);
#endif
    ndz2.map = ndz_readranges(ndz2.ndz);
    if (ndz2.map == NULL) {
	fprintf(stderr, "%s: could not read ranges\n",
		ndz_filename(ndz2.ndz));
	exit(1);
    }
#if 1
    printf("==== New ");
    ndz_rangemap_dump(ndz2.map, chunkfunc);
    fflush(stdout);
#endif

    /*
     * Roll through the new image map looking for overlaps with
     * the old image.
     */
    mmap = ndz_rangemap_init(ndz2.map->loaddr, ndz2.map->hiaddr);
    assert(mmap != NULL);

    (void) ndz_rangemap_iterate(ndz2.map, fixmap, mmap);
#if 1
    printf("==== Merged ");
    ndz_rangemap_dump(mmap, twochunkfunc);
    fflush(stdout);
#endif

    return 0;
}

static int
fixmap(struct ndz_rangemap *map, struct ndz_range *range, void *arg)
{
    struct ndz_rangemap *mmap = (struct ndz_rangemap *)arg;
    ndz_addr_t addr, eaddr, oaddr, oeaddr;
    struct ndz_range *orange, *oprev;
    int rv;

    addr = range->start;
    eaddr = range->end;

#ifdef FIXMAP_DEBUG
    fprintf(stderr, "fixmap [%lu - %lu]:\n", addr, eaddr);
#endif

    /* Allocate a range in the merge map */
    rv = ndz_rangemap_alloc(mmap, addr, eaddr-addr+1, NULL);
    assert(rv == 0);

    orange = ndz_rangemap_lookup(ndz1.map, addr, &oprev);

    /* Current map address in not in old range */
    if (orange == NULL) {
	if (oprev == NULL)
	    oprev = &ndz1.map->head;
	/* Nothing more in the old map, we are done */
	if ((orange = oprev->next) == NULL) {
#ifdef FIXMAP_DEBUG
	    fprintf(stderr, "  finished old map\n");
#endif
	    return 1;
	}
#ifdef FIXMAP_DEBUG
	fprintf(stderr, "  found oldrange [%lu - %lu]:\n", orange->start, orange->end);
#endif
	/* No overlap at all with current map range, move on */
	if (orange->start > eaddr) {
#ifdef FIXMAP_DEBUG
	    fprintf(stderr, "  no overlap in oldrange\n");
#endif
	    return 0;
	}
    }
#ifdef FIXMAP_DEBUG
    else
	fprintf(stderr, "  found oldrange [%lu - %lu]:\n", orange->start, orange->end);
#endif

    while ((oaddr = orange->start) <= eaddr) {
	struct mergestate *state;

	/* Determine the extent of the overlap */
	if (oaddr < addr)
	    oaddr = addr;
	if ((oeaddr = orange->end) > eaddr)
	    oeaddr = eaddr;

#ifdef FIXMAP_DEBUG
	fprintf(stderr, "    found overlap [%lu - %lu]:\n", oaddr, oeaddr);
#endif
	/* Dealloc this part of the range and realloc with flag set */
	rv = ndz_rangemap_dealloc(mmap, oaddr, oeaddr-oaddr+1);
	assert(rv == 0);
	state = calloc(1, sizeof *state);
	assert(state);
	state->ochunkno = (unsigned int)orange->data;
	state->nchunkno = (unsigned int)range->data;
	rv = ndz_rangemap_alloc(mmap, oaddr, oeaddr-oaddr+1, state);
	assert(rv == 0);

	/* on to the next old map entry */
	orange = orange->next;

	/* end of old map, we are done */
	if (orange == NULL) {
#ifdef FIXMAP_DEBUG
	    fprintf(stderr, "  finished old map\n");
#endif
	    return 1;
	}
#ifdef FIXMAP_DEBUG
	fprintf(stderr, "  next oldrange [%lu - %lu]:\n", orange->start, orange->end);
#endif

    }

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * End:
 */

