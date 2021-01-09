/*
 * Copyright (c) 2000-2020 University of Utah and the Flux Group.
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
#ifndef _IMAGEHDR_H_
#define _IMAGEHDR_H_

#include <inttypes.h>

/*
 * Magic number when image is compressed
 *
 * This magic number has been commandeered for use as a version number.
 * None of this wimpy start at version 1 stuff either, our first version
 * is 1,768,515,945!
 *
 *	V2 introduced the first and last sector fields as well
 *	as basic relocations. Also dropped maintenance of blocktotal.
 *
 *	V3 introduced LILO relocations for Linux partition images.
 *	Since an older imageunzip would still work, but potentially
 *	lay down an incorrect images, I bumped the version number.
 *	Note that there is no change to the header structure however.
 *
 *	V4 of the block descriptor adds support for integrety protection
 *	and encryption. V4 HAS BEEN DEPRECATED and we are pretending it
 *	never existed. We will re-add the security features as part of
 *	V6 or a later version.
 *
 *	V5 introduced 64-bit blocknumbers and integrity protection from V4.
 */
#define COMPRESSED_MAGIC_BASE		0x69696969
#define COMPRESSED_V1			(COMPRESSED_MAGIC_BASE+0)
#define COMPRESSED_V2			(COMPRESSED_MAGIC_BASE+1)
#define COMPRESSED_V3			(COMPRESSED_MAGIC_BASE+2)
#define COMPRESSED_V4			(COMPRESSED_MAGIC_BASE+3)
#define COMPRESSED_V5			(COMPRESSED_MAGIC_BASE+4)
#define COMPRESSED_V6			(COMPRESSED_MAGIC_BASE+5)

/* XXX V6 not ready for prime time yet. */
#define COMPRESSED_MAGIC_CURRENT	COMPRESSED_V5

/*
 * Each compressed block of the file has this little header on it.
 * Since each block is independently compressed, we need to know
 * its internal size (it will probably be shorter than 1MB) since we
 * have to know exactly how much to give the inflator.
 */
struct blockhdr_V1 {
	uint32_t	magic;		/* magic/version */
	uint32_t	size;		/* Size of compressed part */
	int32_t		blockindex;	/* which block we are */
	int32_t		blocktotal;	/* V1: total number of blocks */
	int32_t		regionsize;	/* sizeof header + regions */
	int32_t		regioncount;	/* number of regions */
};

/*
 * Version 2 of the block descriptor adds a first and last sector value.
 * These are used to describe free space which is adjacent to the allocated
 * sector data.  This is needed in order to properly zero all free space.
 * Previously free space between regions that wound up in different
 * blocks could only be handled if the blocks were presented consecutively,
 * this was not the case in frisbee.
 */
struct blockhdr_V2 {
	uint32_t	magic;		/* magic/version */
	uint32_t	size;		/* Size of compressed part */
	int32_t		blockindex;	/* which block we are */
	int32_t		blocktotal;	/* V1: total number of blocks */
	int32_t		regionsize;	/* sizeof header + regions */
	int32_t		regioncount;	/* number of regions */
	/* V2 follows */
	uint32_t	firstsect;	/* first sector described by block */
	uint32_t	lastsect;	/* first sector past block */
	int32_t		reloccount;	/* number of reloc entries */
};

/*
 * XXX Version 3 of the image format introduced no header changes.
 */

/*
 * XXX Version 4 of the block descriptor was never released. It included
 * the original "secure image" support, which has now been deferred.
 *
 * The reasoning being, we need 64-bit images more immediately and don't
 * want to impose all the crypto requirements of images that just need
 * 64-bit blocks. Expect the secure image support to reappear in V6.
 */
#if defined(WITH_CRYPTO) || defined(SIGN_CHECKSUM)
#error "Secure image creation not supported right now."
#endif

/* Standard 128 bit field */
#define UUID_LENGTH		16

/*
 * Version 5 of the block descriptor adds support for 64-bit sectors/sizes.
 * Images of this format also use 64-bit region and relocation structs.
 *
 * It also adds the per-image unique image ID from the never released V4.
 * This UUID goes in each chunk of the image to help prevent mixing of
 * image chunks when distributed via frisbee. Use of the UUID is optional.
 */
struct blockhdr_V5 {
	uint32_t	magic;		/* magic/version */
	uint32_t	size;		/* Size of compressed part */
	int32_t		blockindex;	/* which block we are */
	int32_t		blocktotal;	/* V1: total number of blocks */
	int32_t		regionsize;	/* sizeof header + regions */
	int32_t		regioncount;	/* number of regions */
	/* V2 follows */
	uint32_t	firstsect;	/* first sector described by block */
	uint32_t	lastsect;	/* first sector past block */
	int32_t		reloccount;	/* number of reloc entries */
	/* V3 introduced no header changes */
	/* V4 was never released; security changes deferred til V6 */
	/* V5 follows */
	uint64_t	firstsect64;	/* first sector described by block */
	uint64_t	lastsect64;	/* first sector past block */
	unsigned char	imageid[UUID_LENGTH];
					/* Unique ID for the whole image */
};

/*
 * Authentication/integrity/encryption constants for V6.
 */
#define ENC_MAX_KEYLEN		32	/* XXX same as EVP_MAX_KEY_LENGTH */
#define CSUM_MAX_LEN		64
#define SIG_MAX_KEYLEN		256	/* must be > CSUM_MAX_LEN+41 */

/*
 * Version 6 of the block descriptor adds support for authentication,
 * integrety protection and encryption.
 *
 * An optionally-signed checksum (hash) of each header+chunk is stored in
 * the header (checksum) along with the hash algorithm used (csum_type).
 * The pubkey used to sign the hash is transfered out-of-band.
 *
 * To ensure that all valid signed chunks are part of the same image,
 * the per-image unique identifier from V5 that is stored in the header
 * (imageid) of each chunk is now mandatory. A random UUID is created and
 * used if the user does not provide one.
 *
 * Optionally, the contents of each chunk (but not the header) is encrypted
 * using the indicated cipher (enc_cipher) and initialization vector (enc_iv).
 */
struct blockhdr_V6 {
	uint32_t	magic;		/* magic/version */
	uint32_t	size;		/* Size of compressed part */
	int32_t		blockindex;	/* which block we are */
	int32_t		blocktotal;	/* V1: total number of blocks */
	int32_t		regionsize;	/* sizeof header + regions */
	int32_t		regioncount;	/* number of regions */
	/* V2 follows */
	uint32_t	firstsect;	/* first sector described by block */
	uint32_t	lastsect;	/* first sector past block */
	int32_t		reloccount;	/* number of reloc entries */
	/* V3 introduced no header changes */
	/* V4 was never released; security changes deferred til V6 */
	/* V5 follows */
	uint64_t	firstsect64;	/* first sector described by block */
	uint64_t	lastsect64;	/* first sector past block */
	unsigned char	imageid[UUID_LENGTH];
					/* Unique ID for the whole image */
	/* V6 follows */
	uint16_t	enc_cipher;	/* cipher was used to encrypt */
	uint16_t	csum_type;	/* checksum algortihm used */
	uint8_t		enc_iv[ENC_MAX_KEYLEN];
					/* Initialization vector */
	unsigned char	checksum[SIG_MAX_KEYLEN];
					/* (Signed) checksum */
};

/*
 * Coming some day in V7:
 *
 * Flag field?
 *   For example, to indicate a delta image. Would probably take over the
 *   otherwise unused blocktotal field.
 *
 * Sectorsize field?
 *   To make explicit the units of sector fields; e.g., 512 vs 4096.
 *
 * Chunksize field?
 *   To support different chunksizes.
 *
 * Mandate little-endian on-disk data.
 *   Code changes only to use appropriate endian macros when reading/writing
 *   data. No data struct changes needed.
 */

/*
 * Checksum types supported
 */
#define CSUM_NONE		0  /* must be zero */
#define CSUM_SHA1		1  /* SHA1 */
#define CSUM_SHA1_LEN		20
#define CSUM_SHA256		1  /* SHA256 */
#define CSUM_SHA256_LEN		32
#define CSUM_SHA512		1  /* SHA512: default */
#define CSUM_SHA512_LEN		64

/* type field */
#define CSUM_TYPE		0xFF

/* flags */
#define CSUM_SIGNED		0x8000	/* checksum is signed */

/*
 * Ciphers supported
 */
#define ENC_NONE		0  /* must be zero */
#define ENC_BLOWFISH_CBC	1

/*
 * Authentication ciphers supported
 */
#define AUTH_RSA		0

/*
 * Relocation descriptor.
 * Certain data structures like BSD disklabels and LILO boot blocks require
 * absolute block numbers.  This descriptor tells the unzipper what the
 * data structure is and where it is located in the block.
 *
 * Relocation descriptors follow the region descriptors in the header area.
 */
struct blockreloc_32 {
	uint32_t	type;		/* relocation type (below) */
	uint32_t	sector;		/* sector it applies to */
	uint32_t	sectoff;	/* offset within the sector */
	uint32_t	size;		/* size of data affected */
};

/* N.B. sector and sectoff swapped to avoid alignment issues */
struct blockreloc_64 {
	uint32_t	type;		/* relocation type (below) */
	uint32_t	sectoff;	/* offset within the sector */
	uint64_t	sector;		/* sector it applies to */
	uint64_t	size;		/* size of data affected */
};

typedef union blockreloc {
	struct blockreloc_32 r32;
	struct blockreloc_64 r64;
} blockreloc_t;

#define RELOC_VALID(is32, sec, size) \
	(!is32 || (uint32_t)(sec) == (sec) || (uint32_t)(size) == size)

#define RELOC_ADDR(is32, base, ix) \
	(is32 ? \
	    (blockreloc_t *)((struct blockreloc_32 *)(base) + (ix)) : \
	    (blockreloc_t *)((struct blockreloc_64 *)(base) + (ix)))
	
#define RELOC_RSIZE(is32, num) \
	((num) * (is32 ? \
	    sizeof(struct blockreloc_32) : \
	    sizeof(struct blockreloc_64)))
	
#define RELOC_ADD(is32, ptr, _type, _sec, _secoff, _size) \
	if (is32) { \
	    (ptr)->r32.type = (_type); \
	    (ptr)->r32.sector = (uint32_t)(_sec); \
	    (ptr)->r32.sectoff = (_secoff); \
	    (ptr)->r32.size = (uint32_t)(_size); \
	} else { \
	    (ptr)->r64.type = (_type); \
	    (ptr)->r64.sector = (_sec); \
	    (ptr)->r64.sectoff = (_secoff); \
	    (ptr)->r64.size = (_size); \
	}

#define RELOC_NEXT(is32, ptr) \
	(is32 ? \
	    (blockreloc_t *)((struct blockreloc_32 *)(ptr) + 1) : \
	    (blockreloc_t *)((struct blockreloc_64 *)(ptr) + 1))

#define RELOC_TYPE(is32, ptr) \
	(is32 ? (ptr)->r32.type : (ptr)->r64.type)
#define RELOC_SECTOR(is32, ptr) \
	(is32 ? (ptr)->r32.sector : (ptr)->r64.sector)
#define RELOC_SECTOFF(is32, ptr) \
	(is32 ? (ptr)->r32.sectoff : (ptr)->r64.sectoff)
#define RELOC_SIZE(is32, ptr) \
	(is32 ? (ptr)->r32.size : (ptr)->r64.size)

#define RELOC_NONE		0
#define RELOC_FBSDDISKLABEL	1	/* FreeBSD disklabel */
#define RELOC_OBSDDISKLABEL	2	/* OpenBSD disklabel */
#define RELOC_LILOSADDR		3	/* LILO sector address */
#define RELOC_LILOMAPSECT	4	/* LILO map sector */
#define RELOC_LILOCKSUM		5	/* LILO descriptor block cksum */
#define RELOC_SHORTSECTOR	6	/* indicated sector < sectsize */

/* XXX potential future alternatives to hard-wiring BSD disklabel knowledge */
#define RELOC_ADDPARTOFFSET	100	/* add partition offset to location */
#define RELOC_XOR16CKSUM	101	/* 16-bit XOR checksum */
#define RELOC_CKSUMRANGE	102	/* range of previous checksum */

typedef struct blockhdr_V5 blockhdr_t;

/*
 * This little struct defines the pair. Each number is in sectors. An array
 * of these come after the header above, and is padded to a 1K boundry.
 * The region says where to write the next part of the input file, which is
 * how we skip over parts of the disk that do not need to be written
 * (swap, free FS blocks).
 */
struct region_32 {
	uint32_t	start;
	uint32_t	size;
};

struct region_64 {
	uint64_t	start;
	uint64_t	size;
};

typedef union region {
	struct region_32 r32;
	struct region_64 r64;
} region_t;

#define REG_VALID(is32, start, size) \
	(!is32 || (uint32_t)(start) == (start) || (uint32_t)(size) == size)

#define REG_DIFF(is32, last, first) \
	(is32 ? \
	    ((struct region_32 *)(last) - (struct region_32 *)(first)) : \
	    ((struct region_64 *)(last) - (struct region_64 *)(first)))
	
#define REG_ADD(is32, ptr, _start, _size) \
if (is32) { \
	(ptr)->r32.start = (uint32_t)(_start); \
	(ptr)->r32.size = (uint32_t)(_size); \
} else { \
	(ptr)->r64.start = (_start); \
	(ptr)->r64.size = (_size); \
}

#define REG_NEXT(is32, ptr) \
	(is32 ? \
	    (region_t *)((struct region_32 *)(ptr) + 1) : \
	    (region_t *)((struct region_64 *)(ptr) + 1))

#define REG_PREV(is32, ptr) \
	(is32 ? \
	    (region_t *)((struct region_32 *)(ptr) - 1) : \
	    (region_t *)((struct region_64 *)(ptr) - 1))

#define REG_START(is32, ptr) \
	(is32 ? (uint64_t)ptr->r32.start : ptr->r64.start)

#define REG_SIZE(is32, ptr) \
	(is32 ? (uint64_t)ptr->r32.size : ptr->r64.size)

#define REG_END(is32, ptr) \
	(is32 ? \
	    (ptr->r32.start + ptr->r32.size) : (ptr->r64.start + ptr->r64.size))
	
/*
 * Each block has its own region header info.
 *
 * Since there is no easy way to tell how many regions will fit before
 * we have compressed the region data, we just have to pick a size here.
 * If this area is too small, it is possible that a highly fragmented image
 * will fill this header before filling the data area of a block.  If the
 * region header area is too large, we will almost always fill up the data
 * area before filling the region header.  Since the latter is more likely
 * to be common, we tend to the small-ish side.
 *
 * At 4K, with a V2 image having a 36 byte header and 8 byte region
 * descriptors, we can fix 507 regions into a single chunk.
 *
 * At 4K, with a V5 image having a 68 byte header and 16 byte region
 * descriptors, we can fix 251 regions into a single chunk.
 *
 * At 4K, with a V6 image having a 362 byte header and 16 byte region
 * descriptors, we can fix 233 regions into a single chunk.
 */
#define DEFAULTREGIONSIZE	4096

/*
 * Ah, the frisbee protocol. The new world order is to break up the
 * file into fixed chunks, with the region info prepended to each
 * chunk so that it can be layed down on disk independently of all the
 * chunks in the file. 
 */
#define F_BLOCKSIZE		1024
#define F_BLOCKSPERCHUNK	1024

#define CHUNKSIZE		(F_BLOCKSIZE * F_BLOCKSPERCHUNK)
#define CHUNKMAX		(CHUNKSIZE - DEFAULTREGIONSIZE)


/*
 * Assumed sector (block) size
 */
#define SECSIZE			512

#endif /* _IMAGEHDR_H_ */
