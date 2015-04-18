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

#ifndef _LIBNDZ_H_
#define	_LIBNDZ_H_

#include "imagehdr.h"
#include "rangemap.h"

typedef uint32_t ndz_chunkno_t;
/* XXX keep this opaque so we don't create dependencies on zlib */
typedef void * ndz_chunk_t;

#ifdef maybenotneeded
struct ndz_chunkmap {
    ndz_addr_t start;
    ndz_addr_t end;
};
#endif

struct ndz_file {
    int fd;
    int flags;
    off_t curoff;
    char *fname;
    int sectsize;
    int chunksize;
    ndz_chunkno_t nchunks;
    ndz_chunk_t chunkobj;
    ndz_addr_t chunksect;
    unsigned chunkuses;
    unsigned chunkhits;
    struct ndz_rangemap *rangemap;
    unsigned hashtype;
    unsigned hashblksize;
    void *hashdata;
    struct ndz_rangemap *hashmap;
    /* per-chunk info to verify */
    /* readahead cache stuff */
};

/* flags */
#define NDZ_FILE_WRITE		1
#define NDZ_FILE_SEEKABLE	2

struct ndz_chunkhdr {
    blockhdr_t *header;
    struct region *region;
    struct blockreloc *reloc;
    char data[DEFAULTREGIONSIZE];
};

struct ndz_file *ndz_open(const char *name, int flags);
int ndz_close(struct ndz_file *ndz);
char *ndz_filename(struct ndz_file *ndz);
ssize_t ndz_read(struct ndz_file *ndz, void *buf, size_t bytes, off_t offset);
ssize_t ndz_write(struct ndz_file *ndz, void *buf, size_t bytes, off_t offset);
int ndz_readahead(struct ndz_file *ndz, void *buf, size_t bytes, off_t offset);

int ndz_readchunkheader(struct ndz_file *ndz, ndz_chunkno_t chunkno,
			struct ndz_chunkhdr *chunkhdr);
ndz_size_t ndz_readdata(struct ndz_file *ndz, void *buf, ndz_size_t nsect,
			ndz_addr_t sect);
struct ndz_rangemap *ndz_readranges(struct ndz_file *ndz);
void ndz_dumpranges(struct ndz_rangemap *map);

ndz_chunk_t ndz_chunk_open(struct ndz_file *ndz, ndz_chunkno_t chunkno);
void ndz_chunk_close(ndz_chunk_t chobj);
ssize_t ndz_chunk_read(ndz_chunk_t chobj, void *buf, size_t bytes);
ndz_chunkno_t ndz_chunk_chunkno(ndz_chunk_t chobj);
ndz_chunk_t ndz_chunk_create(struct ndz_file *ndz, ndz_chunkno_t chunkno);
void ndz_chunk_flush(ndz_chunk_t chobj);
ssize_t ndz_chunk_left(ndz_chunk_t chobj);
ssize_t ndz_chunk_append(ndz_chunk_t chobj, ndz_addr_t ssect,
			 void *buf, size_t bytes);

struct ndz_rangemap *ndz_readhashinfo(struct ndz_file *ndz, char *sigfile);
char *ndz_hash_dump(unsigned char *h, int hlen);
void ndz_hashmap_dump(struct ndz_rangemap *map, int summaryonly);
struct ndz_rangemap *ndz_compute_delta(struct ndz_rangemap *omap,
				       struct ndz_rangemap *nmap);

#endif /* _LIBNDZ_H_ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * End:
 */
