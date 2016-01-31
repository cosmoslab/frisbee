/*
 * Copyright (c) 2016 University of Utah and the Flux Group.
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
 * Support for disk erase operations (e.g., TRIM).
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#ifdef __FreeBSD__
#include <sys/disk.h>
#endif

#define SECSIZE		512
#define SECALIGN(p)	(void *)(((uintptr_t)(p) + (SECSIZE-1)) & ~(SECSIZE-1))
#define ISSECALIGNED(p)	(((uintptr_t)(p) & (SECSIZE-1)) == 0)

#define ERASEMINSIZE	4096
#define ZEROSIZE	(256*1024)

static off_t ebsize = -1;
static off_t erased = 0;
static off_t zeroed = 0;

static int zeroit(int fd, off_t offset, off_t count);

off_t
erasebsize(void)
{
	if (ebsize < 0) {
#ifdef DIOCGDELETE
		/* XXX this seems to be the minimum */
		ebsize = ERASEMINSIZE;
#else
		ebsize = 0;
#endif
	}

	return ebsize;
}

#ifdef DIOCGDELETE
int
erasedata(int fd, off_t offset, off_t ecount, int zeroonfail)
{
	off_t args[2];
	off_t toff, tend, tcnt;
	off_t bsize = erasebsize();

	if (bsize == 0) {
		if (zeroonfail)
			return zeroit(fd, offset, ecount);
		return -1;
	}

	/*
	 * Check alignment/length, erase as much as we can.
	 */
	assert(ISSECALIGNED(toff));
	assert(ISSECALIGNED(ecount));
	toff = offset;
	tend = offset + ecount;
	if ((toff % bsize) != 0)
		toff = ((toff + bsize-1) / bsize) * bsize;
	if ((tend % bsize) != 0)
		tend = (tend / bsize) * bsize;
	tcnt = tend - toff;

#if 0
	fprintf(stderr, "Would erase [%ld-%ld]\n", toff, tend-1);
	if (toff > offset || tcnt != ecount) {
		fprintf(stderr, "Would zero ");
		if (toff > offset)
			fprintf(stderr, "[%ld-%ld] ", offset, toff-1);
		toff += tcnt;
		if (toff < offset+ecount)
			fprintf(stderr, "[%ld-%ld] ", toff, offset+ecount-1);
		fprintf(stderr, "\n");
	}
	return 0;
#endif

	args[0] = toff;
	args[1] = tcnt;
	if (ioctl(fd, DIOCGDELETE, args) < 0) {
		fprintf(stderr,
			"DIOCGDELETE of [%lld-%lld] failed (%d)\n",
			(long long)args[0],
			(long long)args[0]+args[1]-1, errno);
		if (zeroonfail)
			return zeroit(fd, offset, ecount);
		return -1;
	}

	/*
	 * Take care of leading and trailing blocks we could not erase.
	 */
	if (toff > offset || tcnt != ecount) {
		/* XXX zero the excess */
		if (toff > offset && zeroit(fd, offset, toff-offset))
			return -1;
		toff += tcnt;
		if (toff < offset+ecount &&
		    zeroit(fd, toff, offset+ecount-toff))
			return -1;
	}

	return 0;
}
#else
int
erasedata(int fd, off_t offset, off_t ecount, int zeroonfail)
{
	return -1;
}
#endif

static int
zeroit(int fd, off_t offset, off_t count)
{
	char *buf, *_buf;
	size_t bsize, wsize;
	int err = 0;

	if (lseek(fd, offset, SEEK_SET) < 0) {
		perror("lseek to write zeros");
		return -1;
	}

	if (count < bsize)
		bsize = count;
	else
		bsize = ZEROSIZE;

	_buf = malloc(bsize + SECSIZE);
	if (_buf == NULL) {
		fprintf(stderr, "Could not allocated zero buffer\n");
		return -1;
	}
	buf = SECALIGN(_buf);
	memset(buf, 0, bsize);
	while (count > 0) {
		if (count < bsize)
			wsize = count;
		else
			wsize = bsize;
		if (write(fd, buf, wsize) != wsize) {
			fprintf(stderr, "Could not write zeros\n");
			err = -1;
			goto done;
		}
		count -= wsize;
	}

 done:
	free(_buf);
	return err;
}
