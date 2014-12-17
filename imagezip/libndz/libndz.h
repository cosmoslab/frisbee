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

#ifndef _LIBNDZ_H_
#define	_LIBNDZ_H_

#include "rangemap.h"

typedef uint32_t ndz_chunk_t;

struct ndz_file {
    int fd;
    char *fname;
    int sectsize;
    int chunksize;
    ndz_chunk_t nchunks;
    struct ndz_rangemap *rangemap;
    /* per-chunk info to verify */
    /* readahead cache stuff */
};

struct chunkmap {
	ndz_addr_t start;
	ndz_addr_t end;
} *chunkmap;

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
int ndz_readahead(struct ndz_file *ndz, void *buf, size_t bytes, off_t offset);

int ndz_readchunkheader(struct ndz_file *ndz, ndz_chunk_t chunkno,
			struct ndz_chunkhdr *chunkhdr);

struct ndz_rangemap *ndz_readranges(struct ndz_file *ndz);
void ndz_dumpranges(struct ndz_rangemap *map);

#endif /* _LIBNDZ_H_ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * End:
 */
