/*
 * Copyright (c) 2014-2018 University of Utah and the Flux Group.
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
 * We just associate an array of blockreloc structs with the NDZ file
 * to keep track of these. This is good enough since there are not ever
 * very many relocs and they are almost always in the first chunk.
 *
 * Note that I originally used a rangemap, but there can be more than
 * one reloc per sector so that doesn't work.
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

void
ndz_reloc_init(struct ndz_file *ndz)
{
    assert(ndz != NULL);

    ndz->reloc32 = 1;
    ndz->relocdata = NULL;
    ndz->relocentries = 0;
    ndz->reloclo = NDZ_HIADDR;
    ndz->relochi = NDZ_LOADDR;
}

/*
 * Read relocs out of a chunk header and add them to the array of relocs
 * for the file, reallocing the buffer as necessary. Not terrible efficient,
 * but does not have to be.
 */ 
int
ndz_reloc_get(struct ndz_file *ndz, blockhdr_t *hdr, void *buf)
{
    blockreloc_t *relocdata, *chunkreloc = buf;
    int i;

    if (ndz == NULL || hdr == NULL || chunkreloc == NULL)
	return -1;

    if (hdr->magic < COMPRESSED_V2 || hdr->reloccount == 0)
	return 0;

    if (ndz->relocdata == NULL)
	ndz->reloc32 = hdr->magic >= COMPRESSED_V5 ? 0 : 1;
    else {
	int is32 = hdr->magic >= COMPRESSED_V5 ? 0 : 1;
	assert(is32 == ndz->reloc32);
    }

    /* resize the relocation buffer */
    i = ndz->relocentries + hdr->reloccount;
    if (ndz->relocdata == NULL)
	relocdata = malloc(RELOC_RSIZE(ndz->reloc32, i));
    else
	relocdata = realloc(ndz->relocdata, RELOC_RSIZE(ndz->reloc32, i));
    if (relocdata == NULL) {
	ndz_reloc_free(ndz);
	return -1;
    }
    ndz->relocdata = relocdata;

    relocdata = RELOC_ADDR(ndz->reloc32, ndz->relocdata, ndz->relocentries);
    for (i = 0; i < hdr->reloccount; i++) {
	ndz_addr_t rsector = ndz->reloc32 ?
	    chunkreloc->r32.sector : chunkreloc->r64.sector;

	if (ndz->reloclo == NDZ_HIADDR)
	    ndz->reloclo = rsector;
	/* XXX we should be adding these in order; we assume this elsewhere */
	assert(ndz->reloclo <= rsector);
	if (rsector > ndz->relochi)
	    ndz->relochi = rsector;

	if (ndz->reloc32)
	    relocdata->r32 = chunkreloc->r32;
	else
	    relocdata->r64 = chunkreloc->r64;
	relocdata = RELOC_NEXT(ndz->reloc32, relocdata);
	chunkreloc = RELOC_NEXT(ndz->reloc32, chunkreloc);
    }
    ndz->relocentries += hdr->reloccount;

#ifdef RELOC_DEBUG
    if (hdr->reloccount > 0) {
	fprintf(stderr, "got %d relocs, %d total, range [%lu-%lu]\n",
		hdr->reloccount, ndz->relocentries,
		ndz->reloclo, ndz->relochi);
    }
#endif

    return 0;
}

/*
 * Find any relocation entries that apply to the indicated chunk and add them.
 */
int
ndz_reloc_put(struct ndz_file *ndz, blockhdr_t *hdr, void *buf)
{
    blockreloc_t *chunkreloc, *relocdata;
    int i;

    if (ndz == NULL || hdr == NULL || buf == NULL)
	return -1;

    if (ndz->relocentries == 0 ||
	hdr->firstsect > ndz->relochi || hdr->lastsect <= ndz->reloclo)
	return 0;

    chunkreloc = buf;
    relocdata = ndz->relocdata;
    for (i = 0; i < ndz->relocentries; i++) {
	ndz_addr_t rsector, roffset;
	ndz_size_t rsize;

	if (ndz->reloc32) {
	    rsector = relocdata->r32.sector;
	    roffset = relocdata->r32.sectoff;
	    rsize = relocdata->r32.size;
	} else {
	    rsector = relocdata->r64.sector;
	    roffset = relocdata->r64.sectoff;
	    rsize = relocdata->r64.size;
	}

	assert(roffset + rsize <= ndz->sectsize);
	if (rsector >= hdr->firstsect && rsector < hdr->lastsect) {
#ifdef RELOC_DEBUG
	    fprintf(stderr, "found reloc for %lu in chunk range [%u-%u]\n",
		    rsector, hdr->firstsect, hdr->lastsect - 1);
#endif
	    if (ndz->reloc32)
		chunkreloc->r32 = relocdata->r32;
	    else
		chunkreloc->r64 = relocdata->r64;
	    chunkreloc = RELOC_NEXT(ndz->reloc32, chunkreloc);
	}
	relocdata = RELOC_NEXT(ndz->reloc32, relocdata);
    }

    return 0;
}

/*
 * Returns the number of relocations in the indicated range, 0 otherwise
 * If size is zero, count til the end.
 */
int
ndz_reloc_inrange(struct ndz_file *ndz, ndz_addr_t addr, ndz_size_t size)
{
    blockreloc_t *relocdata;
    ndz_addr_t eaddr;
    int i, nreloc = 0;

    assert(ndz != NULL);

    if (size == 0)
	eaddr = (ndz->relochi > addr) ? ndz->relochi : addr;
    else
	eaddr = addr + size - 1;
    if (ndz->relocentries == 0 || addr > ndz->relochi || eaddr < ndz->reloclo)
	return 0;

    relocdata = ndz->relocdata;
    for (i = 0; i < ndz->relocentries; i++) {
	ndz_addr_t rsector, roffset;
	ndz_size_t rsize;

	if (ndz->reloc32) {
	    rsector = relocdata->r32.sector;
	    roffset = relocdata->r32.sectoff;
	    rsize = relocdata->r32.size;
	} else {
	    rsector = relocdata->r64.sector;
	    roffset = relocdata->r64.sectoff;
	    rsize = relocdata->r64.size;
	}

	assert(roffset + rsize <= ndz->sectsize);
	if (rsector > eaddr)
	    break;
	if (rsector >= addr && rsector <= eaddr) {
	    nreloc++;
	}
	relocdata = RELOC_NEXT(ndz->reloc32, relocdata);
    }
#ifdef RELOC_DEBUG
    if (nreloc)
	fprintf(stderr, "found %d relocs in range [%lu-%lu]\n",
		nreloc, addr, eaddr);
#endif
    return nreloc;
}

/*
 * Reloc info is small so this is relatively painless
 */
int
ndz_reloc_copy(struct ndz_file *ndzfrom, struct ndz_file *ndzto)
{
    size_t size;

    if (ndzfrom == NULL || ndzto == NULL || ndzto->relocentries > 0)
	return -1;

    if (ndzfrom->relocentries == 0)
	return 0;

    size = RELOC_RSIZE(ndzfrom->reloc32, ndzfrom->relocentries);
    if ((ndzto->relocdata = malloc(size)) == NULL)
	return -1;

    memcpy(ndzto->relocdata, ndzfrom->relocdata, size);
    ndzto->relocentries = ndzfrom->relocentries;
    ndzto->reloclo = ndzfrom->reloclo;
    ndzto->relochi = ndzfrom->relochi;
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
