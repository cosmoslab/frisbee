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
 * Chunk-oriented IO routines.
 *
 * Since chunks are independently compressed, we can manipulate them
 * independently.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <zlib.h>
#include <sys/stat.h>

#include "libndz.h"

#define CINPUTSIZE	(128*1024)

struct ndz_chunk {
    struct ndz_file *ndz;
    ndz_chunkno_t chunkno;
    off_t foff;
    z_stream z;
    int cbufsize;
    int cbufoff;
    char *cdatabuf;
};

ndz_chunk_t
ndz_chunk_open(struct ndz_file *ndz, ndz_chunkno_t chunkno)
{
    struct ndz_chunk *chunk = malloc(sizeof *chunk);
    if (chunk == NULL)
	return NULL;
    chunk->cdatabuf = malloc(CINPUTSIZE);
    if (chunk->cdatabuf == NULL) {
	free(chunk);
	return NULL;
    }
    chunk->cbufsize = CINPUTSIZE;
    chunk->cbufoff = 0;

    chunk->ndz = ndz;
    chunk->chunkno = chunkno;
    chunk->z.zalloc = Z_NULL;
    chunk->z.zfree = Z_NULL;
    chunk->z.opaque = Z_NULL;
    chunk->z.next_in = Z_NULL;
    chunk->z.avail_in = 0;
    chunk->z.next_out = Z_NULL;
    if (inflateInit(&chunk->z) != Z_OK) {
	free(chunk);
	return NULL;
    }
    chunk->foff = (off_t)chunkno * ndz->chunksize + DEFAULTREGIONSIZE;

    return (ndz_chunk_t)chunk;
}

void
ndz_chunk_close(ndz_chunk_t chobj)
{
    struct ndz_chunk *chunk = (struct ndz_chunk *)chobj;
    if (chunk == NULL)
	return;

    /* release any cache resources */

    inflateEnd(&chunk->z);
    free(chunk);
}

ndz_chunkno_t
ndz_chunk_chunkno(ndz_chunk_t chobj)
{
    struct ndz_chunk *chunk = (struct ndz_chunk *)chobj;
    if (chunk == NULL)
	return ~0;

    return chunk->chunkno;
}

/*
 * Sequentially read data from a chunk til there is no more to be read
 */
ssize_t
ndz_chunk_read(ndz_chunk_t chobj, void *buf, size_t bytes)
{
    int rv;
    ssize_t cc;

    struct ndz_chunk *chunk = (struct ndz_chunk *)chobj;
    if (chunk == NULL)
	return -1;

    chunk->z.next_out = (Bytef *)buf;
    chunk->z.avail_out = bytes;
    while (chunk->z.avail_out > 0) {
	/* read more compressed data from file if necessary */
	if (chunk->z.avail_in == 0) {
	    cc = ndz_read(chunk->ndz, chunk->cdatabuf, chunk->cbufsize,
			  chunk->foff);
	    if (cc <= 0)
		return cc;
	    chunk->z.next_in = (Bytef *)chunk->cdatabuf;
	    chunk->z.avail_in = cc;
	    chunk->foff += cc;
	}
	assert(chunk->z.next_in != Z_NULL);
	assert(chunk->z.avail_in > 0);

	rv = inflate(&chunk->z, Z_SYNC_FLUSH);

	if (rv == Z_STREAM_END) {
#ifdef DEBUG
	    fprintf(stderr, "chunk_read hit STREAM_END at foff=%ld, avail_out=%d\n",
		    (unsigned long)chunk->foff, chunk->z.avail_out);
#endif
	    break;
	}

	if (rv != Z_OK) {
	    fprintf(stderr, "%s: inflate failed, rv=%d\n",
		    chunk->ndz->fname, rv);
	    return -1;
	}
    }

    return (bytes - chunk->z.avail_out);
}

/*
 * XXX for now the write functions are distinct from the read functions
 * til I decide if it is worthwhile to combine.
 */
ndz_chunk_t
ndz_chunk_create(struct ndz_file *ndz, ndz_chunkno_t chunkno)
{
    struct ndz_chunk *chunk = malloc(sizeof *chunk);
    if (chunk == NULL)
	return NULL;

    /*
     * We allocate a buffer large enough to hold the entire chunk
     * since we need to manipulate it a bit while we are constructing it.
     * Once finished, we flush the whole thing to the file.
     */
    chunk->cdatabuf = malloc(CHUNKSIZE);
    if (chunk->cdatabuf == NULL) {
	free(chunk);
	return NULL;
    }
    chunk->cbufsize = CHUNKSIZE;
    chunk->cbufoff = 0;

    chunk->ndz = ndz;
    chunk->chunkno = chunkno;
    chunk->z.zalloc = Z_NULL;
    chunk->z.zfree = Z_NULL;
    chunk->z.opaque = Z_NULL;
    chunk->z.next_in = Z_NULL;
    chunk->z.avail_in = 0;
    chunk->z.next_out = Z_NULL;
    if (inflateInit(&chunk->z) != Z_OK) {
	free(chunk);
	return NULL;
    }
    chunk->foff = (off_t)chunkno * ndz->chunksize;

    return (ndz_chunk_t)chunk;
}

void
ndz_chunk_flush(ndz_chunk_t chobj)
{
    struct ndz_chunk *chunk = (struct ndz_chunk *)chobj;
    ssize_t cc;

    if (chunk == NULL)
	return;

    cc = ndz_write(chunk->ndz, chunk->cdatabuf, chunk->cbufsize, chunk->foff);

    deflateEnd(&chunk->z);
    free(chunk);
}

/*
 * Returns the amount of uncompressed data that the chunk object will
 * accept in an ndz_chunk_append operation. Zero indicates that it is
 * time to flush.
 */
ssize_t
ndz_chunk_left(ndz_chunk_t chobj)
{
    struct ndz_chunk *chunk = (struct ndz_chunk *)chobj;
    if (chunk == NULL)
	return -1;

    return chunk->cbufsize - chunk->cbufoff;
}

/*
 * Compress and append data to a chunk.
 * Writes are all-or-nothing, returns zero if the full write cannot be done.
 * It is up to the caller to use ndz_chunk_left and resize accordingly.
 */
ssize_t
ndz_chunk_append(ndz_chunk_t chobj, ndz_addr_t ssect, void *buf, size_t bytes)
{
    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * End:
 */
