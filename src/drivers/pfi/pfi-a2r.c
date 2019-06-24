/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/drivers/pfi/pfi-a2r.c                                    *
 * Created:     2019-06-18 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2019 Hampa Hug <hampa@hampa.ch>                          *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pfi.h"
#include "pfi-io.h"
#include "pfi-a2r.h"


#ifndef DEBUG_A2R
#define DEBUG_A2R 0
#endif


#define A2R_CLOCK      8000000

#define A2R_MAGIC_A2R2 0x32523241
#define A2R_MAGIC_2    0x0a0d0aff
#define A2R_MAGIC_INFO 0x4f464e49
#define A2R_MAGIC_STRM 0x4d525453
#define A2R_MAGIC_META 0x4154454d


typedef struct {
	FILE          *fp;
	pfi_img_t     *img;
	pfi_trk_t     *trk;

	unsigned long c;
	unsigned long h;
	unsigned long loop;

	char          have_info;
	unsigned char disk_type;
	unsigned char write_protected;
	unsigned char synchronized;
	char          creator[33];
} a2r_load_t;


static
void a2r_get_string (char *dst, const unsigned char *src, unsigned max)
{
	unsigned i;

	for (i = 0; i < max; i++) {
		if ((dst[i] = src[i]) == 0) {
			break;
		}
	}

	while ((i > 0) && (dst[i - 1] == 0x20)) {
		i -= 1;
	}

	dst[i] = 0;
}

static
int a2r_load_header (a2r_load_t *a2r)
{
	unsigned char buf[8];

	if (pfi_read (a2r->fp, buf, 8)) {
		return (1);
	}

	if (pfi_get_uint32_le (buf, 0) != A2R_MAGIC_A2R2) {
		fprintf (stderr, "a2r: bad magic 1\n");
		return (1);
	}

	if (pfi_get_uint32_le (buf, 4) != A2R_MAGIC_2) {
		fprintf (stderr, "a2r: bad magic 2\n");
		return (1);
	}

	return (0);
}

static
int a2r_load_meta (a2r_load_t *a2r, unsigned long size)
{
	unsigned      n;
	unsigned char buf[256];

	while (size > 0) {
		n = (size < 256) ? size : 256;

		if (pfi_read (a2r->fp, buf, n)) {
			return (1);
		}

		if (pfi_img_add_comment (a2r->img, buf, n)) {
			return (1);
		}

		size -= n;
	}

	return (0);
}

static
int a2r_load_info (a2r_load_t *a2r, unsigned long size)
{
	unsigned char buf[36];

	if (size < 36) {
		return (1);
	}

	if (pfi_read (a2r->fp, buf, 36)) {
		return (1);
	}

	if (buf[0] < 1) {
		fprintf (stderr, "a2r: bad info version (%u)\n", buf[0]);
		return (1);
	}

	a2r_get_string (a2r->creator, buf + 1, 32);

	a2r->disk_type = buf[33];
	a2r->write_protected = buf[34];
	a2r->synchronized = buf[34];

#if DEBUG_A2R >= 1
	fprintf (stderr, "a2r: creator         = '%s'\n", a2r->creator);
	fprintf (stderr, "a2r: disk type       = %u\n", a2r->disk_type);
	fprintf (stderr, "a2r: write protected = %u\n", a2r->write_protected);
	fprintf (stderr, "a2r: synchronized    = %u\n", a2r->synchronized);
#endif

	if (pfi_skip (a2r->fp, size - 36)) {
		return (1);
	}

	return (0);
}

static
int a2r_load_capture (a2r_load_t *a2r, unsigned long size)
{
	unsigned      i, n;
	unsigned long pulse, index, total;
	unsigned char buf[256];

	pulse = 0;
	total = 0;
	index = 0;

	i = 0;
	n = 0;

	while (size > 0) {
		if (total >= index) {
			if (pfi_trk_add_index (a2r->trk, total)) {
				return (1);
			}

			index += a2r->loop;
		}

		if (i >= n) {
			i = 0;
			n = (size < 256) ? size : 256;

			if (pfi_read (a2r->fp, buf, n)) {
				return (1);
			}
		}

		if (buf[i] < 255) {
			pulse += buf[i];

			if (pulse > 0) {
				if (pfi_trk_add_pulse (a2r->trk, pulse)) {
					return (1);
				}
			}

			total += pulse;
			pulse = 0;
		}
		else {
			pulse += 255;
		}

		i += 1;
		size -= 1;
	}

	pfi_trk_clean (a2r->trk);

	return (0);
}

static
int a2r_load_strm (a2r_load_t *a2r, unsigned long size)
{
	unsigned long cnt;
	unsigned char buf[10];

	while (size > 0) {
		if (pfi_read (a2r->fp, buf, 1)) {
			return (1);
		}

		size -= 1;

		if (buf[0] == 0xff) {
			break;
		}

		if (size < 9) {
			return (1);
		}

		if (pfi_read (a2r->fp, buf + 1, 9)) {
			return (1);
		}

		size -= 9;

		if (a2r->disk_type == 1) {
			a2r->c = buf[0];
			a2r->h = 0;
		}
		else {
			a2r->c = buf[0] >> 1;
			a2r->h = buf[0] & 1;
		}

		cnt = pfi_get_uint32_le (buf, 2);

		if (cnt > size) {
			return (1);
		}

		a2r->loop = pfi_get_uint32_le (buf, 6);

#if DEBUG_A2R >= 2
		fprintf (stderr, "a2r: %lu/%lu TYPE=%u SIZE=%lu LOOP=%lu\n",
			a2r->c, a2r->h, buf[1], cnt, a2r->loop
		);
#endif

		if ((buf[1] == 1) || (buf[1] == 3)) {
			a2r->trk = pfi_img_get_track (a2r->img, a2r->c, a2r->h, 1);

			if (a2r->trk == NULL) {
				return (1);
			}

			pfi_trk_reset (a2r->trk);
			pfi_trk_set_clock (a2r->trk, A2R_CLOCK);

			if (a2r_load_capture (a2r, cnt)) {
				return (1);
			}
		}
		else {
			fprintf (stderr,
				"a2r: warning: unsupported capture type (%u)\n",
				buf[1]
			);

			if (pfi_skip (a2r->fp, cnt)) {
				return (1);
			}
		}

		size -= cnt;
	}

	if (size > 0) {
		fprintf (stderr,
			"a2r: warning: %lu extra bytes in STRM chunk\n",
			size
		);

		if (pfi_skip (a2r->fp, size)) {
			return (1);
		}
	}

	return (0);
}

static
int a2r_load_image (a2r_load_t *a2r)
{
	unsigned long type, size;
	unsigned char buf[8];

	if (a2r_load_header (a2r)) {
		return (1);
	}

	a2r->have_info = 0;

	while (pfi_read (a2r->fp, buf, 8) == 0) {
		type = pfi_get_uint32_le (buf, 0);
		size = pfi_get_uint32_le (buf, 4);

#if DEBUG_A2R >= 2
		fprintf (stderr, "chunk %08lX + %08lX\n", type, size);
#endif

		if (type == A2R_MAGIC_INFO) {
			if (a2r_load_info (a2r, size)) {
				return (1);
			}

			a2r->have_info = 1;
		}
		else if (type == A2R_MAGIC_STRM) {
			if (a2r->have_info == 0) {
				return (1);
			}

			if (a2r_load_strm (a2r, size)) {
				return (1);
			}
		}
		else if (type == A2R_MAGIC_META) {
			if (a2r_load_meta (a2r, size)) {
				return (1);
			}
		}
		else {
			fprintf (stderr, "a2r: unknown chunck type (%08lX)\n",
				type
			);

			if (pfi_skip (a2r->fp, size)) {
				return (1);
			}
		}
	}

	return (0);
}

pfi_img_t *pfi_load_a2r (FILE *fp)
{
	a2r_load_t a2r;

	a2r.fp = fp;
	a2r.img = NULL;
	a2r.trk = NULL;
	a2r.have_info = 0;

	if ((a2r.img = pfi_img_new()) == NULL) {
		return (NULL);
	}

	if (a2r_load_image (&a2r)) {
		pfi_img_del (a2r.img);
		return (NULL);
	}

	return (a2r.img);
}

int pfi_probe_a2r_fp (FILE *fp)
{
	unsigned char buf[8];

	if (pfi_read (fp, buf, 8)) {
		return (0);
	}

	if (pfi_get_uint32_le (buf, 0) != A2R_MAGIC_A2R2) {
		return (0);
	}

	if (pfi_get_uint32_le (buf, 4) != A2R_MAGIC_2) {
		return (0);
	}

	return (1);
}

int pfi_probe_a2r (const char *fname)
{
	int  r;
	FILE *fp;

	if ((fp = fopen (fname, "rb")) == NULL) {
		return (0);
	}

	r = pfi_probe_a2r_fp (fp);

	fclose (fp);

	return (r);
}
