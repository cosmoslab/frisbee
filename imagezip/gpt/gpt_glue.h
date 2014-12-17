/*
 * Glue to connect imagezip with FreeBSD's sys/boot/common/gpt.c code.
 */
#ifndef _GPT_GLUE_H
#define _GPT_GLUE_H
#include <sys/types.h>
#include <stdio.h>
#include <strings.h>
#include "imagehdr.h"
#include "gpt.h"

#define drvread  gpt_drvread
#define drvwrite gpt_drvwrite
#define drvsize  gpt_drvsize

/* we want to intercept these */
#define printf	 gpt_printf
int gpt_printf(const char * __restrict fmt, ...);

#ifndef DEV_BSIZE
#define DEV_BSIZE SECSIZE
#endif
#define BOOTPROG "YOWZA!"
typedef struct uuid uuid_t;

/* all se need for a "disk" is an fd */
struct dsk {
	uint64_t start;	/* we don't use this but code sets it */
	int fd;
};
int drvread(struct dsk *dskp, void *buf, daddr_t lba, unsigned nblk);
int drvwrite(struct dsk *dskp, void *buf, daddr_t lba, unsigned nblk);
uint64_t drvsize(struct dsk *dskp);

int gptread(const uuid_t *uuid, struct dsk *dskp, char *buf);
void gptgettables(struct gpt_hdr **hdr, struct gpt_ent **ent);

#endif /* _GPT_GLUE_H */
