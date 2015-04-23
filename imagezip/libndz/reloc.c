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
 * Relocation handling routines.
 *
 * XXX using a range map is overkill, since there are almost never more
 * than a few (one) relocations and they are single sector things.
 * But images with relocations are rare, so don't worry about it.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "libndz.h"

#define RELOC_DEBUG

int
ndz_reloc_get(struct ndz_file *ndz, blockhdr_t *hdr, void *buf)
{
    struct blockreloc *relocdata, *reloc = buf;
    int i;

    if (ndz == NULL || ndz->relocmap == NULL || hdr == NULL || reloc == NULL)
	return -1;

    if (hdr->magic < COMPRESSED_V2 || hdr->reloccount == 0)
	return 0;

    /* resize the relocation buffer */
    i = ndz->relocentries + hdr->reloccount;
    if (ndz->relocdata == NULL)
	relocdata = malloc(i * sizeof(*reloc));
    else
	relocdata = realloc(ndz->relocdata, i * sizeof(*reloc));
    if (relocdata == NULL) {
	ndz_reloc_free(ndz);
	return -1;
    }
    ndz->relocdata = relocdata;

    relocdata = ndz->relocdata + ndz->relocentries;
    memcpy(relocdata, reloc, hdr->reloccount * sizeof(*reloc));
    for (i = 0; i < hdr->reloccount; i++) {
	ndz_size_t size = (reloc->size + ndz->sectsize - 1) / ndz->sectsize;
	if (ndz_rangemap_alloc(ndz->relocmap, (ndz_addr_t)reloc->sector, size,
			       &relocdata[i])) {
	    ndz_reloc_free(ndz);
	    return -1;
	}
    }
    ndz->relocentries += hdr->reloccount;

#ifdef RELOC_DEBUG
    if (hdr->reloccount > 0) {
	fprintf(stderr, "got %d relocs, %d total\n",
		hdr->reloccount, ndz->relocentries);
    }
#endif

    return 0;
}

int
ndz_reloc_put(struct ndz_file *ndz, blockhdr_t *hdr, void *buf)
{
    struct blockreloc *reloc;
    struct ndz_range *rrange;
    ndz_addr_t addr, eaddr;

    if (ndz == NULL || ndz->relocmap == NULL || hdr == NULL || buf == NULL)
	return -1;

    reloc = buf;
    addr = hdr->firstsect;
    eaddr = hdr->lastsect;
    rrange = ndz_rangemap_overlap(ndz->relocmap, addr, eaddr - addr + 1);
    while (rrange && rrange->start <= eaddr) {
#ifdef RELOC_DEBUG
	fprintf(stderr, "found reloc [%lu-%lu] in chunk range [%lu-%lu]\n",
		rrange->start, rrange->end, addr, eaddr);
#endif
	assert(rrange->start >= addr && rrange->end <= eaddr);
	assert(rrange->data != NULL);

	*reloc = *(struct blockreloc *)rrange->data;
	reloc++;
	hdr->reloccount++;
	rrange = rrange->next;
    }

    return 0;
}

void
ndz_reloc_free(struct ndz_file *ndz)
{
    if (ndz) {
	if (ndz->relocdata) {
	    free(ndz->relocdata);
	    ndz->relocdata = NULL;
	}
	if (ndz->relocmap) {
	    ndz_rangemap_deinit(ndz->relocmap);
	    ndz->relocmap = NULL;
	}
	ndz->relocentries = 0;
    }
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * End:
 */
