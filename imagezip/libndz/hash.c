/*
 * Copyright (c) 2014-2015 University of Utah and the Flux Group.
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

/*
 * Hashing-related functions.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "libndz.h"
#include "imagehash.h"

struct hashdata {
    ndz_chunkno_t chunkno;
    uint32_t hashlen;
    uint8_t hash[HASH_MAXSIZE];
};

char *
ndz_hash_dump(unsigned char *h, int hlen)
{
	static char hbuf[HASH_MAXSIZE*2+1];
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < hlen; i++) {
		hbuf[i*2] = hex[h[i] >> 4];
		hbuf[i*2+1] = hex[h[i] & 0xf];
	}
	hbuf[i*2] = '\0';
	return hbuf;
}

static void
printhashdata(struct ndz_rangemap *map, void *ptr)
{
    struct hashdata *h = ptr;

    /* upper bit of chunkno indicates chunkrange */
    if (HASH_CHUNKDOESSPAN(h->chunkno)) {
	int chunkno = HASH_CHUNKNO(h->chunkno);
	printf("chunkno=%d-%d, ", chunkno, chunkno + 1);
    } else
	printf("chunkno=%d, ", (int)h->chunkno);
    printf("hash=%s", ndz_hash_dump(h->hash, h->hashlen));
}

void
ndz_hashmap_dump(struct ndz_rangemap *map, int summaryonly)
{
    if (map)
	ndz_rangemap_dump(map, summaryonly, printhashdata);
}

void
ndz_hash_data(struct ndz_file *ndz, unsigned char *data, unsigned long count,
	      unsigned char *hash)
{
    assert(ndz != NULL && ndz->hashtype != 0);
    assert(data != NULL && hash != NULL);

    if (ndz->hashtype == HASH_TYPE_SHA1)
	SHA1(data, count, hash);
    else if (ndz->hashtype == HASH_TYPE_MD5)
	MD5(data, count, hash);
}


/*
 * Read the hash info from a signature file into a region map associated
 * with the ndz file.
 */
struct ndz_rangemap *
ndz_readhashinfo(struct ndz_file *ndz, char *sigfile)
{
    struct hashinfo hi;
    struct hashregion hr;
    int fd, cc, rv, i;
    unsigned hashlen, blksize, hashtype;
    struct ndz_rangemap *map;
    struct hashdata *hashdata = NULL;

    unsigned lhblock,lstart,lsize;

    if (ndz == NULL)
	return NULL;
    if (ndz->hashmap)
	return ndz->hashmap;

    fd = open(sigfile, O_RDONLY);
    if (fd < 0) {
	perror(sigfile);
	return NULL;
    }
    cc = read(fd, &hi, sizeof(hi));
    if (cc != sizeof(hi)) {
	if (cc < 0)
	    perror(sigfile);
	else
	    fprintf(stderr, "%s: too short\n", sigfile);
	close(fd);
	return NULL;
    }
    if (strcmp((char *)hi.magic, HASH_MAGIC) != 0 ||
	!(hi.version == HASH_VERSION_1 || hi.version == HASH_VERSION_2)) {
	fprintf(stderr, "%s: not a valid signature file\n", sigfile);
	close(fd);
	return NULL;
    }

    map = ndz_rangemap_init(NDZ_LOADDR, NDZ_HIADDR-NDZ_LOADDR);
    if (map == NULL) {
	fprintf(stderr, "%s: could not allocate rangemap\n",
		ndz->fname);
	close(fd);
	return NULL;
    }

    /* allocate the hash data elements all in one piece for convienience */
    if (hi.nregions) {
	hashdata = malloc(hi.nregions * sizeof(struct hashdata));
	if (hashdata == NULL) {
	    fprintf(stderr, "%s: could not allocate hashmap data\n",
		    ndz->fname);
	    close(fd);
	    return NULL;
	}
    }

    hashtype = hi.hashtype;
    hashlen = (hashtype == HASH_TYPE_MD5) ? 16 : 20;
    blksize = (hi.version == HASH_VERSION_1) ?
	(HASHBLK_SIZE / ndz->sectsize) : hi.blksize;

    lhblock = -1;
    for (i = 0; i < hi.nregions; i++) {
	cc = read(fd, &hr, sizeof(hr));
	if (cc != sizeof(hr)) {
	    fprintf(stderr, "%s: incomplete sig entry\n", sigfile);
	    free(hashdata);
	    close(fd);
	    return NULL;
	}
	hashdata[i].chunkno = hr.chunkno;
	hashdata[i].hashlen = hashlen;
	memcpy(hashdata[i].hash, hr.hash, HASH_MAXSIZE);

#if 0
	/* Sanity check the ranges */
	if (1) {
	    unsigned sb = hr.region.start / blksize;
	    unsigned eb = (hr.region.start+hr.region.size-1) / blksize;
	    if (sb != eb)
		fprintf(stderr, "*** [%u-%u]: range spans hash blocks\n",
			hr.region.start, hr.region.start+hr.region.size-1);
	    if (sb == lhblock)
		fprintf(stderr, "*** [%u-%u]: range in same hash block ([%u-%u]) as [%u-%u]\n",
			hr.region.start, hr.region.start+hr.region.size-1,
			sb*blksize, sb*blksize+blksize-1,
			lstart, lstart+lsize-1);
	    lhblock = sb;
	    lstart = hr.region.start;
	    lsize = hr.region.size;
	}
#endif

	rv = ndz_rangemap_alloc(map, (ndz_addr_t)hr.region.start,
				(ndz_size_t)hr.region.size,
				(void *)&hashdata[i]);
	if (rv) {
	    fprintf(stderr, "%s: bad hash region [%u-%u]\n",
		    ndz->fname,
		    (unsigned)hr.region.start,
		    (unsigned)hr.region.start+hr.region.size-1);
	    ndz_rangemap_deinit(map);
	    free(hashdata);
	    close(fd);
	    return NULL;
	}
    }
    close(fd);

    ndz->hashmap = map;
    ndz->hashdata = hashdata;
    ndz->hashblksize = blksize;
    ndz->hashtype = hashtype;

#if 0
    /* Compensate for partition offset */
    for (i = 0; i < hinfo->nregions; i++) {
	struct hashregion *hreg = &hinfo->regions[i];
	assert(hreg->region.size <= hashblksize);

	hreg->region.start += poffset;
    }
#endif

    return map;
}

void
ndz_freehashmap(struct ndz_file *ndz)
{
    if (ndz->hashmap) {
	ndz_rangemap_deinit(ndz->hashmap);
	ndz->hashmap = NULL;
    }
    if (ndz->hashdata) {
	free(ndz->hashdata);
	ndz->hashdata = NULL;
    }
    ndz->hashblksize = 0;
}

struct deltainfo {
    struct ndz_rangemap *omap;
    struct ndz_rangemap *dmap;
    int omapdone;
};

static int
compdelta(struct ndz_rangemap *nmap, struct ndz_range *range, void *arg)
{
    struct deltainfo *dinfo = arg;
    struct ndz_rangemap *omap = dinfo->omap;
    struct ndz_rangemap *dmap = dinfo->dmap;
    ndz_addr_t addr, eaddr;
    struct ndz_range *orange, *oprev;
    struct hashdata *odata, *ndata;
    int rv;

    addr = range->start;
    eaddr = range->end;

#ifdef COMPDELTA_DEBUG
    fprintf(stderr, "compdelta [%lu-%lu]:\n", addr, eaddr);
#endif

    /*
     * We are past the end of the old map. Just allocate the range in
     * the merge map and continue.
     */
    if (dinfo->omapdone) {
#ifdef COMPDELTA_DEBUG
	fprintf(stderr, "  already finished old map, adding\n");
#endif
	rv = ndz_rangemap_alloc(dmap, addr, eaddr-addr+1, NULL);
	assert(rv == 0);
	return 0;
    }

    /*
     * Look up the corresponding start address in the old map.
     */
    orange = ndz_rangemap_lookup(omap, addr, &oprev);
    if (orange == NULL) {
	/*
	 * Start address was not found. If we are at the end of the
	 * old list, then there can be no overlap with any following
	 * entries so we just add this range. As an optimization,
	 * we also note that we have finished with the old map so
	 * that we will just add all following new map entries.
	 */
	if (oprev == NULL)
	    oprev = &omap->head;
	if ((orange = oprev->next) == NULL) {
#ifdef COMPDELTA_DEBUG
	    fprintf(stderr, "  past end of old map, adding\n");
#endif
	    rv = ndz_rangemap_alloc(dmap, addr, eaddr-addr+1, NULL);
	    assert(rv == 0);
	    dinfo->omapdone = 1;
	    return 0;
	}
	/*
	 * Likewise, if the start address of the next old range entry
	 * is beyond our end address, there is no overlap.
	 */
	if (orange->start > eaddr) {
#ifdef COMPDELTA_DEBUG
	    fprintf(stderr, "  no overlap with old map, adding\n");
#endif
	    rv = ndz_rangemap_alloc(dmap, addr, eaddr-addr+1, NULL);
	    assert(rv == 0);
	    return 0;
	}
    } else {
	/*
	 * Start address was found. Check for an exact overlap, in
	 * which case we can just compare the hashes to determine
	 * whether to add the range or not.
	 */
	if (addr == orange->start && eaddr == orange->end) {
#ifdef COMPDELTA_DEBUG
	    fprintf(stderr, "  exact overlap with old map, ");
#endif
	    odata = orange->data;
	    ndata = range->data;
	    assert(odata->hashlen == ndata->hashlen);
	    if (memcmp(odata->hash, ndata->hash, ndata->hashlen) != 0) {
#ifdef COMPDELTA_DEBUG
		fprintf(stderr, "hash differs, adding\n");
#endif
		rv = ndz_rangemap_alloc(dmap, addr, eaddr-addr+1, NULL);
		assert(rv == 0);
	    } else {
#ifdef COMPDELTA_DEBUG
		fprintf(stderr, "hash same, skipping\n");
#endif
	    }
	    return 0;
	}
    }

    /*
     * If we get here, some portion of the new range overlaps with
     * one or more old ranges. We just have to add the whole range
     * since we don't have comparible hashes.
     */
#ifdef COMPDELTA_DEBUG
    fprintf(stderr, "  partial overlap with oldrange [%lu-%lu], adding\n",
	    orange->start, orange->end);
#endif
    rv = ndz_rangemap_alloc(dmap, addr, eaddr-addr+1, NULL);
    assert(rv == 0);

    return 0;
}

struct ndz_rangemap *
ndz_compute_delta(struct ndz_rangemap *omap, struct ndz_rangemap *nmap)
{
    struct ndz_rangemap *dmap;
    struct deltainfo dinfo;

    if (omap == NULL || nmap == NULL)
	return NULL;

    dmap = ndz_rangemap_init(nmap->loaddr, nmap->hiaddr);
    if (dmap == NULL) {
	fprintf(stderr, "Could not allocate delta map\n");
	return NULL;
    }

    dinfo.omap = omap;
    dinfo.dmap = dmap;
    dinfo.omapdone = 0;
    (void) ndz_rangemap_iterate(nmap, compdelta, &dinfo);

    return dmap;
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * End:
 */
