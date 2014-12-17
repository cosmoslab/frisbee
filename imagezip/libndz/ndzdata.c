/*
 * Copyright (c) 2014 University of Utah and the Flux Group.
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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>

#include "../imagehdr.h"
#include "libndz.h"

static int init_chunkmap(struct ndz_file *ndz);
static void dump_chunkmap(struct ndz_file *ndz);

/*
 * Read uncompessed data from an imagefile.
 * Returns however many bytes of data we found in the image. 
 */
ssize_t
ndz_readdata(struct ndz_file *ndz, void *buf, size_t bytes, off_t offset)
{
    ndz_addr_t ssect, esect;
    struct ndz_range *range;
    ssize_t rbytes = 0;
    ndz_chunk_t chunkno;

    if (ndz->rangemap == NULL && ndz_readranges(ndz) == NULL) {
	errno = EINVAL;
	return -1;
    }

    assert(ndz->sectsize != 0);
    ssect = offset / ndz->sectsize;
    esect = (offset + bytes - 1) / ndz->sectsize;

    while (ssect <= esect) {
	range = ndz_rangemap_lookup(ndz->rangemap, ssect, NULL);
	if (range == NULL)
	    return rbytes;

	chunkno = (ndz_chunkno)range->data;
	assert(chunkno > 0);
	chunkno--;

	/* read the chunk */
	/* decompress til we got what we want */

	ssect = range->end + 1;
    }

    return 0;
}

#if 0
static int
init_chunkmap(struct ndz_file *ndz)
{
    struct stat sb;
    ndz_chunk_t chunkno;

    /*
     * XXX for now we don't handle streaming an image (fd == stdin).
     * We could do it, we would just have to construct the chunkmap
     * on the fly.
     */
    if (fstat(ndz->fd, &sb) < 0) {
	perror(ndz->fname);
	return 1;
    }

    ndz->nchunks = (sb.st_size + ndz->chunksize - 1) / ndz->chunksize;
    if (ndz->nchunks == 0)
	return 0;

    ndz->chunkmap = calloc(ndz->nchunks, sizeof(struct chunkmap));
    if (ndz->chunkmap == NULL) {
	fprintf(stderr, "%s: could not allocate chunkmap\n", ndz->fname);
	ndz->nchunks = 0;
	return 1;
    }

    for (chunkno = 0; chunkno < ndz->nchunks; chunkno++) {
	struct ndz_chunkhdr head;
	blockhdr_t *hdr;
	struct region *reg;
	ndz_addr_t lo, hi;
	int i;

	if (ndz_readchunkheader(ndz, chunkno, &head) != 0) {
	    free(ndz->chunkmap);
	    ndz->chunkmap = NULL;
	    ndz->nchunks = 0;
	    return 1;
	}

	/* null header pointer indicates EOF */
	if ((hdr = head.header) == NULL) {
	    fprintf(stderr, "%s: unexpected EOF!?\n", ndz->fname);
	    free(ndz->chunkmap);
	    ndz->chunkmap = NULL;
	    ndz->nchunks = 0;
	    return 1;
	}

	reg = head.region;
	assert(reg != NULL || hdr->regioncount == 0);
	lo = NDZ_HIADDR;
	hi = NDZ_LOADDR;
	for (i = 0; i < hdr->regioncount; i++) {
	    if (reg->start < lo)
		lo = reg->start;
	    if (reg->start+reg->size-1 > hi)
		hi = reg->start + reg->size - 1;
	    reg++;
	}
	ndz->chunkmap[chunkno].start = lo;
	ndz->chunkmap[chunkno].end = hi;
    }

    return 0;
}

static void
dump_chunkmap(struct ndz_file *ndz)
{
    ndz_chunk_t chunkno;

    printf("%s (%d chunks):\n", ndz->fname, ndz->nchunks);
    if (ndz->chunkmap == NULL)
	return;
    for (chunkno = 0; chunkno < ndz->nchunks; chunkno++)
	printf("  %u: [%lu - %lu]\n", chunkno+1,
	       ndz->chunkmap[chunkno].start, ndz->chunkmap[chunkno].end);
}
#endif

#ifdef NDZDATA_TEST

int
main(int argc, char **argv)
{
    struct ndz_file *ndz;
    char buf[SECSIZE];
    ssize_t cc;

    if (argc != 2) {
	fprintf(stderr, "%s <ndzfile>\n", argv[0]);
	exit(1);
    }
    
    ndz = ndz_open(argv[1], 0);
    assert(ndz != NULL);

    cc = ndz_readdata(ndz, buf, sizeof buf, 0);

    dump_chunkmap(ndz);

    ndz_close(ndz);
    exit(0);
}
#endif

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * End:
 */
