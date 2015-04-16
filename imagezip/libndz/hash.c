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

#include "libndz.h"
#include "imagehash.h"

struct hashdata {
    ndz_chunkno_t chunkno;
    unsigned char hash[HASH_MAXSIZE];
};

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
    struct ndz_rangemap *map;
    struct hashdata *hashdata = NULL;

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

    for (i = 0; i < hi.nregions; i++) {
	cc = read(fd, &hr, sizeof(hr));
	if (cc != sizeof(hr)) {
	    fprintf(stderr, "%s: incomplete sig entry\n", sigfile);
	    free(hashdata);
	    close(fd);
	    return NULL;
	}
	hashdata[i].chunkno = hr.chunkno;
	memcpy(hashdata[i].hash, hr.hash, HASH_MAXSIZE);
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
    ndz->hashblksize = (hi.version == HASH_VERSION_1) ?
	(HASHBLK_SIZE / ndz->sectsize) : hi.blksize;

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

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * End:
 */
