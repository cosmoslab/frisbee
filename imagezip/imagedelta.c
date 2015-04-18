/*
 * Copyright (c) 2000-2015 University of Utah and the Flux Group.
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

#define CHUNKIFY_DEBUG

/*
 * imagedelta [ -S -f ] image1.ndz image2.ndz delta1to2.ndz
 *
 * Take two images (image1, image2) and produce a delta (delta1to2)
 * based on the differences. The -S option says to use the signature
 * files: image1.ndz.sig and image2.ndz.sig, if possible to determine
 * differences between the images. Signature files will be rejected
 * unless they can be positively matched with the image (right now,
 * via the modtime!) Using -f will force it to use a questionable
 * signature file.
 *
 * Without signature files, we compare the corresponding areas of both
 * images to determine if they are different.
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
#include "imagehash.h"
#include "libndz/libndz.h"

struct fileinfo {
    struct ndz_file *ndz;
    char *sigfile;
    struct ndz_rangemap *map, *sigmap;
} ndz1, ndz2, delta;

struct hashdata {
    ndz_chunkno_t chunkno;
    uint32_t hashlen;
    uint8_t hash[HASH_MAXSIZE];
};

static int usesigfiles = 0;
static int forcesig = 0;
static int debug = 0;
static int verify = 0;
static int hashtype = HASH_TYPE_SHA1;
static int hashlen = 20;
static long hashblksize = HASHBLK_SIZE / 512;
static ndz_chunk_t chunkobj;
static ndz_chunkno_t chunkno;
static char *chunkdatabuf;

void
usage(void)
{
    fprintf(stderr,
	    "Usage: imagedelta [-SVfd] [-b blksize] [-D hashfunc] image1.ndz image2.ndz delta1to2.ndz\n"
	    "\n"
	    "Produce a delta image (delta1to2) containing the changes\n"
	    "necessary to get from image1 to image2.\n"
	    "\n"
	    "-S         Use signature files when computing differences.\n"
	    "-V         Verify consistency of image and signature.\n"
	    "-f         Force imagedelta to use a questionable sigfile.\n"
	    "-d         Enable debugging.\n"
	    "-D hfunc   Hash function to use (md5 or sha1).\n"
	    "-b blksize Size of hash blocks (512 <= size <= 32M).\n");
    exit(1);
}

/*
 * Iterator for ranges in the image map.
 * Validate that entries match up with those in the signature map.
 */
static int
verifyfunc(struct ndz_rangemap *imap, struct ndz_range *range, void *arg)
{
    struct ndz_range **smnext = arg;
    struct ndz_range *srange = *smnext;
    ndz_addr_t addr, eaddr;

    addr = range->start;
    eaddr = range->end;

    /*
     * Every image range should correspond to an integral number of
     * signature map entries.
     */
    while (addr <= eaddr && srange) {
	if (srange->start != addr || srange->end > eaddr) {
	    /*
	     * XXX argh! One anomaly is when an image region gets
	     * split across chunks, in which case it appears as distinct
	     * ranges in our image map. Here we look ahead behind to
	     * identify those cases...
	     */
	    if (srange->data) {
		struct hashdata *hd = (struct hashdata *)srange->data;

		if (HASH_CHUNKDOESSPAN(hd->chunkno)) {
		    /*
		     * If starts line up then make sure following
		     * image map entry is contiguous with us. If so,
		     * assume this is the special case and return
		     * without incrementing the sigmap entry.
		     */
		    if (srange->start == addr && range->next &&
			range->next->start == range->end + 1) {
			*smnext = srange;
			return 0;
		    }
		    /*
		     * See if we are on the other side of the anomaly.
		     * Here the srange start will be before the image
		     * map range start and the previous image range
		     * should be contiguous with us. If so, advance to
		     * the next srange and continue.
		     */
		    if (addr == range->start &&
			srange->start < addr && srange->end <= eaddr &&
			ndz_rangemap_lookup(imap, addr-1, NULL) != NULL) {
			addr = srange->end + 1;
			srange = srange->next;
			continue;
		    }
		}
	    }
	    fprintf(stderr, "  *** [%lu-%lu]: bad sigentry [%lu-%lu]\n",
		    range->start, eaddr, srange->start, srange->end);
	    return 1;
	}
	addr = srange->end + 1;
	srange = srange->next;
    }
    if (addr <= eaddr) {
	fprintf(stderr, "  *** [%lu-%lu]: signature map too short!\n",
		range->start, range->end);
	return 1;
    }

    *smnext = srange;
    return 0;
}

/*
 * File must exist and be readable.
 * If usesigfiles is set, signature file must exist as well.
 * Reads in the range map and signature as well.
 */
void
openifile(char *file, struct fileinfo *info)
{
    int sigfd;

    info->ndz = ndz_open(file, 0);
    if (info->ndz == NULL) {
	fprintf(stderr, "%s: could not open as NDZ file\n",
		ndz_filename(info->ndz));
	exit(1);
    }

    if (usesigfiles) {
	struct stat sb1, sb2;

	info->sigfile = malloc(strlen(file) + 5);
	assert(info->sigfile != NULL);
	strcpy(info->sigfile, file);
	strcat(info->sigfile, ".sig");
	sigfd = open(info->sigfile, 0);
	if (sigfd < 0) {
	    fprintf(stderr, "%s: could not find signature file %s\n",
		    file, info->sigfile);
	    exit(1);
	}
	if (fstat(info->ndz->fd, &sb1) < 0 || fstat(sigfd, &sb2) < 0) {
	    fprintf(stderr, "%s: could stat image or signature file\n", file);
	    exit(1);
	}
	if (!forcesig && labs(sb1.st_mtime - sb2.st_mtime) > 2) {
	    fprintf(stderr, "%s: image and signature disagree (%ld != %ld), "
		    "use -f to override.\n", file, sb1.st_mtime, sb2.st_mtime);
	    exit(1);
	}
	close(sigfd);
    }
}

void
openofile(char *file, struct fileinfo *info)
{
    int sigfd;

    info->ndz = ndz_open(file, 1);
    if (info->ndz == NULL) {
	perror(file);
	exit(1);
    }
    info->sigfile = malloc(strlen(file) + 5);
    assert(info->sigfile != NULL);
    strcpy(info->sigfile, file);
    strcat(info->sigfile, ".sig");

    /* check early that we can write to the sigfile! */
    sigfd = open(info->sigfile, O_WRONLY|O_CREAT|O_TRUNC);
    if (sigfd < 0) {
	perror(info->sigfile);
	exit(1);
    }
    close(sigfd);
}

void
readifile(struct fileinfo *info)
{
    /* read range info from image */
    info->map = ndz_readranges(info->ndz);
    if (info->map == NULL) {
	fprintf(stderr, "%s: could not read ranges\n",
		ndz_filename(info->ndz));
	exit(1);
    }

    /* read signature info */
    if (usesigfiles) {
	info->sigmap = ndz_readhashinfo(info->ndz, info->sigfile);
	if (info->sigmap == NULL) {
	    fprintf(stderr, "%s: could not read signature info\n",
		    ndz_filename(info->ndz));
	    exit(1);
	}
	if (verify) {
	    struct ndz_range *next = ndz_rangemap_first(info->sigmap);
	    int rv;

	    /*
	     * Perform a sanity check, ensuring that ranges in the image
	     * map exactly correspond to those in the signature.
	     */
	    rv = ndz_rangemap_iterate(info->map, verifyfunc, &next);
	    if (rv != 0 || next != NULL) {
		if (rv == 0)
		    fprintf(stderr,
			    "  *** image map too short at sig [%lu-%lu]\n",
			    next->start, next->end);
		fprintf(stderr, "%s: error while validating range/hash maps\n",
			ndz_filename(info->ndz));
#if 1
		printf("==== Image ");
		ndz_rangemap_dump(info->map, (debug==0), NULL);
		printf("==== Hash ");
		ndz_hashmap_dump(info->sigmap, (debug==0));
		fflush(stdout);
#endif
		exit(1);
	    }
	}
    } else
	info->sigmap = NULL;
}

/*
 * Iterator for ranges in the delta map.
 * Read and chunkify the data from the full image, hashing the data as
 * we go.
 */
static int
chunkify(struct ndz_rangemap *mmap, struct ndz_range *range, void *arg)
{
    ndz_addr_t rstart = range->start;
    ndz_size_t rsize = range->end + 1 - rstart, sc;
    uint32_t offset, hsize;
    unsigned char hashbuf[HASH_MAXSIZE], *hash;
    struct ndz_range *hrange;

#ifdef CHUNKIFY_DEBUG
    fprintf(stderr, "chunkify [%lu-%lu]:\n", range->start, range->end);
#endif

#if 0
    if (chunkobj == NULL) {
	chunkno = 0;
	chunkobj = ndz_chunk_create(ndz2.ndz, chunkno);
	chunkdatabuf = malloc(hashblksize * ndz->sectsize);
	if (chunkobj == NULL || chunkdatabuf == NULL) {
	    fprintf(stderr, "could not initialize chunkify data structs\n");
	    return 1;
	}
    }
#endif

    offset = rstart % hashblksize;
    while (rsize > 0) {
	if (offset) {
	    hsize = hashblksize - offset;
	    if (hsize > rsize)
		hsize = rsize;
	    offset = 0;
	} else if (rsize > hashblksize)
	    hsize = hashblksize;
	else
	    hsize = rsize;
#ifdef CHUNKIFY_DEBUG
	fprintf(stderr, "  [%lu-%lu]: ", rstart, rstart + hsize - 1);
#endif

	/* XXX read/decompress data range */
	sc = ndz_readdata(ndz2.ndz, chunkdatabuf, hsize, rstart);

	/*
	 * See if we have an existing hash for the hash block
	 */
	hrange = ndz_rangemap_lookup(ndz2.sigmap, rstart, NULL);
	if (hrange && hrange->data &&
	    hrange->start == rstart && hrange->end == rstart + hsize - 1) {
	    struct hashdata *hd = (struct hashdata *)hrange->data;
	    hash = hd->hash;
#ifdef CHUNKIFY_DEBUG
	    fprintf(stderr, " found hash=%s\n",
		    ndz_hash_dump(hash, hashlen));
#endif
	} else {
	    /* XXX compute hash over data */
#ifdef CHUNKIFY_DEBUG
	    fprintf(stderr, " no hash found\n");
#endif
	}

	/* XXX add range/hashinfo to new sigmap */

	/* XXX compress/write data range */

	/* XXX deal with redo logic when nearing end-of-chunk */

	/* XXX deal with switching chunks */

#if 0
	/*
	 * If no hash was given, we have to compute it
	 */
	if ((hash = rhash) == NULL) {
	    if (hash_range(rstart, hsize, hashbuf)) {
		fprintf(stderr, "Error hashing image data\n");
		return -1;
	    }
	    hash = hashbuf;
	}

	if (addhash(hinfop, rstart, hsize, hash) != 0) {
	    fprintf(stderr, "Out of memory for new hash map\n");
	    return -1;
	}
#endif

	rstart += hsize;
	rsize -= hsize;
    }

    return 0;
}

static void
chunkfunc(struct ndz_rangemap *map, void *ptr)
{
    unsigned int chunkno = (int)ptr;
    printf("chunkno=%u", chunkno);
}

int
main(int argc, char **argv)
{
    int ch;

    while ((ch = getopt(argc, argv, "SfdVb:D:")) != -1)
	switch(ch) {
	case 'S':
	    usesigfiles = 1;
	    break;
	case 'b':
	    hashblksize = atol(optarg);
	    if (hashblksize < 512 || hashblksize > (32*1024*1024) ||
		(hashblksize & 511) != 0) {
		fprintf(stderr, "Invalid hash block size\n");
		usage();
	    }
	    break;
	case 'D':
	    if (strcmp(optarg, "md5") == 0)
		hashtype = HASH_TYPE_MD5;
	    else if (strcmp(optarg, "sha1") == 0)
		hashtype = HASH_TYPE_SHA1;
	    else {
		fprintf(stderr, "Invalid digest type `%s'\n",
			optarg);
		usage();
	    }
	    break;
	case 'f':
	    forcesig = 1;
	    break;
	case 'V':
	    verify = 1;
	    break;
	case 'd':
	    debug++;
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
    openifile(argv[0], &ndz1);
    openifile(argv[1], &ndz2);
    openofile(argv[2], &delta);

    /*
     * Read in the range and signature info.
     */
    readifile(&ndz1);
    readifile(&ndz2);

#if 1
    printf("==== Old range ");
    ndz_rangemap_dump(ndz1.map, (debug==0), chunkfunc);
    printf("==== Old hash ");
    ndz_hashmap_dump(ndz1.sigmap, (debug==0));
    printf("==== New range ");
    ndz_rangemap_dump(ndz2.map, (debug==0), chunkfunc);
    printf("==== New hash ");
    //    ndz_hashmap_dump(ndz2.sigmap, (debug==0));
    ndz_hashmap_dump(ndz2.sigmap, 0);
    fflush(stdout);
#endif

    /*
     * Compute a delta map from the image signature maps.
     * First make sure the hash block size and function are consistent.
     */
    if (usesigfiles) {
	if (ndz1.ndz->hashtype != ndz2.ndz->hashtype ||
	    ndz1.ndz->hashblksize != ndz2.ndz->hashblksize) {
	    fprintf(stderr, "Incomparible signature files for %s (%u/%u) and %s (%u/%u)\n",
		    argv[0], ndz1.ndz->hashtype, ndz1.ndz->hashblksize,
		    argv[1], ndz2.ndz->hashtype, ndz2.ndz->hashblksize);
	    exit(1);
	}
	delta.map = ndz_compute_delta(ndz1.sigmap, ndz2.sigmap);
	if (delta.map == NULL) {
	    fprintf(stderr, "Could not compute delta for %s and %s\n",
		    argv[0], argv[1]);
	    exit(1);
	}
#if 1
	printf("==== Delta hash ");
	ndz_hashmap_dump(delta.map, (debug==0));
	printf("==== Old hashmap stats:");
	ndz_rangemap_dumpstats(ndz1.sigmap);
	printf("==== New hashmap stats:");
	ndz_rangemap_dumpstats(ndz2.sigmap);
	fflush(stdout);
#endif
    }
    else {
	fprintf(stderr, "No can do without sigfiles right now!\n");
	exit(2);
    }

    /*
     * Done with the old file.
     */
    ndz_close(ndz1.ndz);
    ndz1.sigmap = NULL;
    ndz1.map = NULL;
    ndz1.ndz = NULL;

    /*
     * Iterate through the produced map hashing (if necessary) and
     * chunking the data.
     */
    delta.sigmap = ndz_rangemap_init(NDZ_LOADDR, NDZ_HIADDR-NDZ_LOADDR);
    if (delta.sigmap == NULL) {
	fprintf(stderr, "%s: could not create signature map for delta image\n",
		argv[2]);
	exit(1);
    }
    if (ndz_rangemap_iterate(delta.map, chunkify, NULL) != 0) {
	fprintf(stderr, "%s: error while creating new delta image\n",
		argv[2]);
	exit(1);
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
