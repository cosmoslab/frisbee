/*
 * Copyright (c) 2000-2014 University of Utah and the Flux Group.
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
 * Glue to connect imagezip with FreeBSD's sys/boot/common/gpt.c code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <err.h>

#include "imagehdr.h"
#include "sliceinfo.h"
#include "global.h"

#include "gpt_glue.h"
#undef printf

/*
 * XXX hack. gpt.c is very chatty, and we don't want to be putting out
 * error messages if we are just probing for a GPT. Fortunately, it always
 * puts BOOTPROG at the beginning of all its messages, so we only filter
 * those. We keep around the last string in case we really do want to
 * print it.
 */
#include <stdarg.h>

static char lastmsg[512];
static int silent = 0;

int
gpt_printf(const char * __restrict fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(lastmsg, sizeof(lastmsg), fmt, ap);
	if (strncmp(lastmsg, BOOTPROG, strlen(BOOTPROG)) != 0) {
		warnx("GPT: %s", lastmsg);
		lastmsg[0] = '\0';
	} else if (!silent)
		warnx("GPT%s", lastmsg + strlen(BOOTPROG));

	return 1;
}

/*
 * Read the indicated number of sectors.
 * Return 0 on success, error otherwise.
 */
int
drvread(struct dsk *dskp, void *buf, daddr_t lba, unsigned nblk)
{
	int fd = dskp->fd;
	off_t loc = sectobytes(lba);
	size_t size = sectobytes(nblk);

	if (lseek(fd, loc, SEEK_SET) < 0)
		return 1;

	if (read(fd, buf, size) != size)
		return 1;

	return 0;
}

int
drvwrite(struct dsk *dskp, void *buf, daddr_t lba, unsigned nblk)
{
	int fd = dskp->fd;
	off_t loc = sectobytes(lba);
	size_t size = sectobytes(nblk);

	if (lseek(fd, loc, SEEK_SET) < 0)
		return 1;

	if (write(fd, buf, size) != size)
		return 1;

	return 0;
}

/*
 * Return disk size in sectors.
 */
uint64_t
drvsize(struct dsk *dskp)
{
	return (uint64_t)getdisksize(dskp->fd);
}

/* keep things aligned for O_DIRECT IO */
#define SECALIGN(p)	(void *)(((uintptr_t)(p) + (SECSIZE-1)) & ~(SECSIZE-1))

/*
 * Map of GPT type UUIDs to imagezip types
 */
struct gptmap {
	struct uuid gpttype;
	char *desc;
	iz_type iztype;
};
struct gptmap gptmap[] = {
	{GPT_ENT_TYPE_UNUSED, "UNUSED", IZTYPE_UNUSED},
	{GPT_ENT_TYPE_FREEBSD, "FREEBSD", IZTYPE_386BSD},
	{GPT_ENT_TYPE_FREEBSD_UFS, "FREEBSD_UFS", IZTYPE_FBSDNOLABEL},
	{GPT_ENT_TYPE_LINUX_DATA, "LINUX_DATA", IZTYPE_LINUX},
	{GPT_ENT_TYPE_LINUX_SWAP, "LINUX_SWAP", IZTYPE_LINSWP},
	{GPT_ENT_TYPE_BIOS_BOOT, "BIOS_BOOT", IZTYPE_BIOSBOOT},
	{GPT_ENT_TYPE_FREEBSD_BOOT, "FREEBSD_BOOT", IZTYPE_FBSDBOOT},
	{GPT_ENT_TYPE_FREEBSD_SWAP, "FREEBSD_SWAP", IZTYPE_FBSDSWAP},
	{GPT_ENT_TYPE_EFI, "EFI", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_MBR, "MBR", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_FREEBSD_NANDFS, "FREEBSD_NANDFS", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_FREEBSD_VINUM, "FREEBSD_VINUM", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_FREEBSD_ZFS, "FREEBSD_ZFS", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_PREP_BOOT, "PREP_BOOT", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_MS_RESERVED, "MS_RESERVED", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_MS_BASIC_DATA, "MS_BASIC_DATA", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_MS_LDM_METADATA, "MS_LDM_METADATA", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_MS_LDM_DATA, "MS_LDM_DATA", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_LINUX_RAID, "LINUX_RAID", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_LINUX_LVM, "LINUX_LVM", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_VMFS, "VMFS", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_VMKDIAG, "VMKDIAG", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_VMRESERVED, "VMRESERVED", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_VMVSANHDR, "VMVSANHDR", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_APPLE_BOOT, "APPLE_BOOT", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_APPLE_HFS, "APPLE_HFS", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_APPLE_UFS, "APPLE_UFS", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_APPLE_ZFS, "APPLE_ZFS", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_APPLE_RAID, "APPLE_RAID", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_APPLE_RAID_OFFLINE, "APPLE_RAID_OFFLINE", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_APPLE_LABEL, "APPLE_LABEL", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_APPLE_TV_RECOVERY, "APPLE_TV_RECOVERY", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_NETBSD_FFS, "NETBSD_FFS", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_NETBSD_LFS, "NETBSD_LFS", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_NETBSD_SWAP, "NETBSD_SWAP", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_NETBSD_RAID, "NETBSD_RAID", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_NETBSD_CCD, "NETBSD_CCD", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_NETBSD_CGD, "NETBSD_CGD", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_LABEL32, "DRAGONFLY_LABEL32", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_SWAP, "DRAGONFLY_SWAP", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_UFS1, "DRAGONFLY_UFS1", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_VINUM, "DRAGONFLY_VINUM", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_CCD, "DRAGONFLY_CCD", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_LABEL64, "DRAGONFLY_LABEL64", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_LEGACY, "DRAGONFLY_LEGACY", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_HAMMER, "DRAGONFLY_HAMMER", IZTYPE_UNKNOWN},
	{GPT_ENT_TYPE_DRAGONFLY_HAMMER2, "DRAGONFLY_HAMMER2", IZTYPE_UNKNOWN}
};
int ngptmap = sizeof(gptmap) / sizeof(gptmap[0]);

struct gptmap *
getgpttype(struct uuid *gtype)
{
	int i;
	for (i = 0; i < ngptmap; i++)
		if (memcmp(&gptmap[i].gpttype, gtype, sizeof(*gtype)) == 0)
			return &gptmap[i];
	return NULL;
}

/*
 * Our interface to GPT code.
 * Read a GPT partition table. Returns 0 on success, an error otherwise.
 */
int
parse_gpt(int fd, struct iz_slice *parttab, iz_lba *startp, iz_size *sizep,
	  int dowarn)
{
	struct dsk dsk;
	uuid_t uuid;
	struct gpt_hdr *hdr;
	struct gpt_ent *ent;
	char secbuf[SECSIZE+SECSIZE-1], *buf = SECALIGN(secbuf);
	uint64_t dsize, losect, hisect;
	int i;

	dsk.fd = fd;

	/* mark everything invalid to start */
	for (i = 0; i < MAXSLICES; i++)
		parttab[i].type = IZTYPE_INVALID;

	silent = (dowarn == 0);

	/* attempt to read a GPT */
	if (gptread(&uuid, &dsk, buf))
		return 1;

	/* XXX get pointers to the static structs in gpt.c */
	gptgettables(&hdr, &ent);
	if (!hdr || !ent) {
		warnx("GPT: no header or table!?");
		return 1;
	}

	if (hdr->hdr_entries > MAXSLICES) {
		warnx("GPT: too many entries in table!");
		return 1;
	}

	losect = ~0;
	hisect = 0;
	for (i = 0; i < hdr->hdr_entries; i++) {
		struct gptmap *gmap = getgpttype(&ent[i].ent_type);
		uint64_t start = ent[i].ent_lba_start;
		uint64_t size = ent[i].ent_lba_end - ent[i].ent_lba_start + 1;
		iz_type type = IZTYPE_UNKNOWN;

		if (gmap)
			type = gmap->iztype;

		parttab[i].type = type;
		if (type == IZTYPE_UNKNOWN) {
			warnx("P%d: Unsupported GPT partition type %s",
			      i+1, gmap ? gmap->desc : "???");
			parttab[i].flags |= IZFLAG_NOTSUP;
		} else
			parttab[i].flags = 0;

		/* consider a zero-length unused partition as invalid */
		if (type == IZTYPE_UNUSED && start == 0 && size == 1) {
			parttab[i].type = type = IZTYPE_INVALID;
			size = 0;
		}

		parttab[i].offset = start;
		parttab[i].size = size;

		/* XXX right now imagezip only handles 32-bit off/size */
		if ((uint64_t)(parttab[i].offset) != start ||
		    (uint64_t)(parttab[i].size) != size) {
			warnx("P%d: Offset/size too large, ignoring", i+1);
			parttab[i].flags |= IZFLAG_IGNORE;
		}

		if (type != IZTYPE_INVALID) {
			if (start < losect)
				losect = start;
			if (start + size > hisect)
				hisect = start + size;
		}
	}

	/*
	 * At this point we should sanity check the partitions, looking
	 * for overlaps and gaps. The gaps should be added to the skip
	 * list (except for the init pre-partition 1 space which is
	 * typically a boot block and other magic).
	 *
	 * We could do this is a partitioner-indepenedent way, except
	 * that only the partitioner knows which gaps are special and
	 * need to be saved.
	 *
	 * For GPT right now, we just do the easy stuff. The GPT header
	 * tells us the first and last sectors allocated to partitions.
	 * Everything before and after, except the primary and secondary
	 * GPTs, can be skipped.
	 */
	dsize = gpt_drvsize(&dsk);
	{
		uint64_t prilba, seclba, size;

		if (hdr->hdr_lba_self > hdr->hdr_lba_alt) {
			if (dowarn)
				warnx("GPT: using secondary GPT");
			prilba = hdr->hdr_lba_alt;
			seclba = hdr->hdr_lba_self;
		} else {
			prilba = hdr->hdr_lba_self;
			seclba = hdr->hdr_lba_alt;
		}
//fprintf(stderr, "dsize=%lu, pri=%lu, alt=%lu, lba_low=%lu, lba_high=%lu\n", dsize, prilba, seclba, hdr->hdr_lba_start, hdr->hdr_lba_end);
		if (dowarn && (prilba == 0 || seclba == 0))
			warnx("GPT: primary (%lu) or secondary (%lu) is zero",
			      prilba, seclba);
		if (dowarn && losect < hdr->hdr_lba_start)
			warnx("GPT: partition starts (%lu) below lba_start (%lu)",
			      losect, hdr->hdr_lba_start);
		if (dowarn && hisect - 1 > hdr->hdr_lba_end)
			warnx("GPT: partition ends (%lu) after lba_end (%lu)",
			      hisect-1, hdr->hdr_lba_end);
		if (dowarn && prilba != 1)
			warnx("GPT: primary (%lu) not at sector 1", prilba);
		if (dsize && seclba + 1 != dsize) {
			if (dowarn)
				warnx("GPT: secondary (%lu) not at end of disk (%lu)",
				      seclba, dsize);
			seclba = dsize - 1;
		}

		if (losect > hdr->hdr_lba_start) {
			size = losect - hdr->hdr_lba_start;
			addskip(hdr->hdr_lba_start, size);
			if (dowarn)
				warnx("GPT: Skipping %lu sectors at %lu",
				      size, hdr->hdr_lba_start);
		}
		if (hisect < hdr->hdr_lba_end + 1) {
			size = hdr->hdr_lba_end + 1 - hisect;
			addskip(hisect, size);
			if (dowarn)
				warnx("GPT: Skipping %lu sectors at %lu",
				      size, hisect);
		}
		if (startp)
			*startp = 0;
		if (sizep)
			*sizep = (iz_size)dsize;
	}

	return 0;
}
