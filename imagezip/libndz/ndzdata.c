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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>

#include "libndz.h"

static int init_chunkmap(struct ndz_file *ndz);
static void dump_chunkmap(struct ndz_file *ndz);

/*
 * Find the first range for a particular chunk.
 *
 * XXX awkward because we need to pass in the chunkno and return the
 * range entry. We could do this just using the arg pointer, but let's
 * be a little less obscure!
 */
struct fcarg {
    ndz_chunkno_t in_chunkno;
    struct ndz_range *out_range;
};

static int
findchunk(struct ndz_rangemap *map, struct ndz_range *range, void *arg)
{
    struct fcarg *fcarg = arg;

    if ((ndz_chunkno_t)range->data == fcarg->in_chunkno) {
	fcarg->out_range = range;
	return 1;
    }
    return 0;
}

/*
 * Read uncompessed data from an imagefile.
 *
 * Right now it returns an error if it cannot read the indicated number of
 * contiguous bytes.
 *
 * It should probably return as many contiguous bytes as it can get at the
 * indicated location, or an error if there are no data at the indicated location.
 */
ssize_t
ndz_readdata(struct ndz_file *ndz, void *buf, size_t bytes, off_t offset)
{
    ndz_addr_t ssect, esect, csect, resect;
    struct ndz_range *range, *crange;
    ndz_chunkno_t chunkno, lchunkno;
    ndz_chunk_t chunk;
    struct fcarg fcarg;
    ssize_t gotbytes, rbytes, cc;

    if (ndz->rangemap == NULL && ndz_readranges(ndz) == NULL) {
	fprintf(stderr, "%s could not read sector ranges\n", ndz->fname);
	return -1;
    }

    /*
     * Find the range entry corresponding to the desired offset.
     * If the offset isn't included in the image, return zero.
     */
    assert(ndz->sectsize != 0);
    if ((offset % ndz->sectsize) != 0 || (bytes % ndz->sectsize) != 0) {
	fprintf(stderr, "%s: only handle %d-byte aligned reads\n",
		ndz->fname, ndz->sectsize);
	return -1;
    }
    ssect = offset / ndz->sectsize;
    esect = (offset + bytes) / ndz->sectsize;
    range = ndz_rangemap_lookup(ndz->rangemap, ssect, NULL);
    if (range == NULL)
	return 0;

    chunkno = (ndz_chunkno_t)range->data;
    assert(chunkno > 0);
    chunkno--;

    /*
     * If we already have a decompression object for the chunk, see if it is
     * currently before the sector we want. If so, we can just continue
     * decompression in the context of that stream.
     */
#ifdef STATS
    ndz->chunkuses++;
#endif
    chunk = ndz->chunkobj;
    if (chunk && ndz_chunk_chunkno(chunk) == chunkno && ndz->chunksect <= ssect) {
#ifdef DEBUG
	fprintf(stderr, "%s: reusing chunk %d object, sect=%ld\n",
		ndz->fname, chunkno, ndz->chunksect);
#endif
	csect = ndz->chunksect;
	if (csect == ssect)
	    crange = range;
	else {
	    crange = ndz_rangemap_lookup(ndz->rangemap, csect, NULL);
	    assert(crange != NULL);
	}
#ifdef STATS
	ndz->chunkhits++;
#endif
    }
    /*
     * Otherwise we have to open a new stream and work forward from the
     * first range entry in that chunk.
     */
    else {
	if (chunk) {
#ifdef DEBUG
	    fprintf(stderr, "%s: could not reuse chunk %d object, sect=%ld;"
		    " requesting chunk %d, sect=%ld\n",
		    ndz->fname, ndz_chunk_chunkno(chunk), ndz->chunksect,
		    chunkno, ssect);
#endif
	    ndz_chunk_close(chunk);
	}
	chunk = ndz->chunkobj = ndz_chunk_open(ndz, chunkno);
	if (chunk == NULL) {
	    fprintf(stderr, "%s: could not access chunk %d\n",
		    ndz->fname, chunkno);
	    return -1;
	}

	/* note: chunk numbers are 1-based in range map */
	fcarg.in_chunkno = chunkno + 1;
	fcarg.out_range = NULL;
	(void) ndz_rangemap_iterate(ndz->rangemap, findchunk, &fcarg);
	crange = fcarg.out_range;
	assert(crange != NULL);
	csect = ndz->chunksect = crange->start;
#ifdef DEBUG
	fprintf(stderr, "%s: opened chunk %d object, sect=%ld\n",
		ndz->fname, chunkno, ndz->chunksect);
#endif
    }
    assert(csect <= ssect);

    /*
     * Read/uncompress data til we get to the desired start sector.
     */
    if (csect < ssect) {
	size_t tossbufsize = 128 * 1024;
	char *tossbuf;

	tossbuf = malloc(tossbufsize);
	if (tossbuf == NULL) {
	    fprintf(stderr, "%s: could not allocate toss buffer\n",
		    ndz->fname);
	    ndz_chunk_close(chunk);
	    ndz->chunkobj = NULL;
	    return -1;
	}

	while (csect < ssect) {
	    /*
	     * If we are not in the target range yet, we may need to
	     * decompress our way through a number of other ranges in
	     * the same chunk til we get where we need to be.
	     */
	    if (range != crange) {
		assert(crange->end < ssect);
		resect = crange->end + 1;
	    } else
		resect = ssect;
	    rbytes = (resect - csect) * ndz->sectsize;
	    if (rbytes > tossbufsize)
		rbytes = tossbufsize;
	    cc = ndz_chunk_read(chunk, tossbuf, rbytes);
	    if (cc != rbytes) {
		fprintf(stderr,
			"%s: unexpected return from ndz_chunk_read (%lu != %lu)\n",
			ndz->fname, (long unsigned)cc, (long unsigned)rbytes);
		ndz_chunk_close(chunk);
		ndz->chunkobj = NULL;
		return -1;
	    }
	    csect += (cc / ndz->sectsize);
	    if (range != crange && csect == crange->end + 1) {
		assert(crange->next != NULL);
		crange = crange->next;
		csect = crange->start;
	    }
	    ndz->chunksect = csect;
	}
    }
    
    gotbytes = 0;
    lchunkno = chunkno;
    while (ssect < esect) {
	resect = range->end + 1;
	if (esect < resect)
	    resect = esect;
	rbytes = (resect - ssect) * ndz->sectsize;
	cc = ndz_chunk_read(chunk, buf, rbytes);
	if (cc != rbytes) {
	    fprintf(stderr,
		    "%s: unexpected return from ndz_chunk_read (%lu != %lu)\n",
		    ndz->fname, (long unsigned)cc, (long unsigned)rbytes);
	    ndz_chunk_close(chunk);
	    ndz->chunkobj = NULL;
	    return -1;
	}
	gotbytes += cc;
	ndz->chunksect = resect;
	ssect = resect;

	/*
	 * Our request might span ranges and even chunks.
	 */
	if (ssect < esect && ssect == range->end + 1) {
	    range = range->next;
	    /*
	     * If we hit the end of the file, chunk, or just the
	     * end of contiguous data, return what we read.
	     */
	    if (range == NULL || range->start != ssect) {
#ifdef DEBUG
		fprintf(stderr, "%s: hit end-of-%s\n",
			ndz->fname, range ? "contiguous-data" : "file");
#endif
		if (range)
		    ndz->chunksect = range->start;
		else {
		    ndz_chunk_close(chunk);
		    ndz->chunkobj = NULL;
		}
		return gotbytes;
	    }
	    chunkno = (ndz_chunkno_t)range->data;
	    assert(chunkno != 0);
	    chunkno--;
	    if (chunkno != lchunkno) {
		assert(chunkno == lchunkno + 1);
#ifdef DEBUG
		fprintf(stderr, "%s: finished chunk %d, opening chunk %d, sect=%ld\n",
			ndz->fname, lchunkno, chunkno, range->start);
#endif
		ndz_chunk_close(chunk);
		chunk = ndz->chunkobj = ndz_chunk_open(ndz, chunkno);
		if (chunk == NULL) {
		    fprintf(stderr, "%s: could not access chunk %d\n",
			    ndz->fname, chunkno);
		    return -1;
		}
		ndz->chunksect = range->start;
		lchunkno = chunkno;
	    }
	}
    }

    /*
     * We could have ended at the end of a range or even the end
     * of the chunk. Adjust chunksect accordingly.
     */
    assert(ssect == esect);
    if (ssect == range->end + 1) {
	if (range->next)
	    ndz->chunksect = range->next->start;
	else {
	    ndz_chunk_close(chunk);
	    ndz->chunkobj = NULL;
	}
    }

    return gotbytes;
}

#ifdef MAYBE_NOTNEEDED
static int
init_chunkmap(struct ndz_file *ndz)
{
    struct stat sb;
    ndz_chunkno_t chunkno;

    /*
     * XXX for now we don't handle streaming an image (fd == stdin).
     * We could do it, we would just have to construct the chunkmap
     * on the fly.
     */
    if (ndz->seekable == 0) {
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

#ifdef NDZDATA_TEST
static void
dump_chunkmap(struct ndz_file *ndz)
{
    ndz_chunkno_t chunkno;

    printf("%s (%d chunks):\n", ndz->fname, ndz->nchunks);
    if (ndz->chunkmap == NULL)
	return;
    for (chunkno = 0; chunkno < ndz->nchunks; chunkno++)
	printf("  %u: [%lu - %lu]\n", chunkno+1,
	       ndz->chunkmap[chunkno].start, ndz->chunkmap[chunkno].end);
}
#endif
#endif

#ifdef NDZDATA_TEST

static int
readrange(struct ndz_rangemap *map, struct ndz_range *range, void *arg)
{
    static char dbuf[1*1024*1024];
    struct ndz_file *ndz = arg;
    ndz_addr_t ssect, rsize;
    ssize_t bytes, cc, excc;
    off_t offset;

    /* just read up to 1M of every range */
    ssect = range->start;
    rsize = range->end + 1 - range->start;
    offset = (off_t)ssect * ndz->sectsize;
    bytes = rsize * ndz->sectsize;
    if (bytes > sizeof(dbuf)) {
	bytes = sizeof(dbuf);
	rsize = bytes / ndz->sectsize;
    }
    excc = bytes;
    cc = ndz_readdata(ndz, dbuf, bytes, offset);
    fprintf(stderr,
	    "  read [%lu-%lu] from [%d:%lu-%lu] returned %ld of %ld bytes\n",
	    ssect, ssect+rsize-1, (int)range->data, range->start, range->end,
	    cc, excc);
    if (cc != excc) {
	fprintf(stderr, "*** short read!\n");
	return 1;
    }

    /* try spanning the end of the range and see what we get */
    if (range->next) {
	rsize = sizeof(dbuf) / ndz->sectsize;
	ssect = range->end - rsize / 2;
	if (ssect < range->start)
	    ssect = range->start;
	offset = (off_t)ssect * ndz->sectsize;
	bytes = rsize * ndz->sectsize;
	excc = ((off_t)range->end + 1 - ssect) * ndz->sectsize;
	if (range->end + 1 == range->next->start) {
	    excc += ((off_t)range->next->end + 1 - range->next->start) *
		ndz->sectsize;
	    if (excc > bytes)
		excc = bytes;
	}
	cc = ndz_readdata(ndz, dbuf, bytes, offset);
	fprintf(stderr,
		"  read [%lu-%lu] from [%d:%lu-%lu][%d:%lu-%lu] returned %ld of %ld bytes\n",
		ssect, ssect+rsize-1,
		(int)range->data, range->start, range->end,
		(int)range->next->data, range->next->start, range->next->end,
		cc, excc);
	if (cc != excc) {
	    fprintf(stderr, "*** short read!\n");
	    return 1;
	}
    }
    return 0;
}

int
main(int argc, char **argv)
{
    struct ndz_file *ndz;
    struct ndz_rangemap *map;
    char buf[SECSIZE];
    ssize_t cc;

    if (argc != 2) {
	fprintf(stderr, "%s <ndzfile>\n", argv[0]);
	exit(1);
    }
    
    fprintf(stderr, "%s: opening ...\n", argv[1]);
    ndz = ndz_open(argv[1], 0);
    assert(ndz != NULL);
    fprintf(stderr, "%s: reading ranges ...\n", argv[1]);
    map = ndz_readranges(ndz);
    assert(map != NULL);

    /* for now just make sure we can read all data */
    fprintf(stderr, "%s: testing data reads (could take minutes) ...\n", argv[1]);
    cc = ndz_rangemap_iterate(map, readrange, ndz);
    if (cc != 0)
	fprintf(stderr, "%s: FAILED\n", argv[1]);

#ifdef STATS
    fprintf(stderr, "Chunk object uses %u, hits %u (%.2f%%)\n",
	    ndz->chunkuses, ndz->chunkhits,
	    (double)ndz->chunkhits / (ndz->chunkuses ?: 1) * 100);
#endif
    if (ndz->chunkobj)
	ndz_chunk_close(ndz->chunkobj);
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
