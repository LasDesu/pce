/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/drivers/pfi/pfi-pfi.c                                    *
 * Created:     2013-12-26 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2013-2019 Hampa Hug <hampa@hampa.ch>                     *
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
#include "pfi-pfi.h"


#define PFI_MAGIC_PFI  0x50464920
#define PFI_MAGIC_TEXT 0x54455854
#define PFI_MAGIC_TRAK 0x5452414b
#define PFI_MAGIC_INDX 0x494e4458
#define PFI_MAGIC_DATA 0x44415441
#define PFI_MAGIC_END  0x454e4420

#define PFI_CRC_POLY   0x1edc6f41


typedef struct {
	FILE          *fp;
	pfi_img_t     *img;
	pfi_trk_t     *trk;

	uint32_t      crc;

	char          have_header;

	unsigned long bufmax;
	unsigned char *buf;
} pfi_load_t;


static
uint32_t pfi_crc (uint32_t crc, const void *buf, unsigned cnt)
{
	unsigned            i, j;
	unsigned            val;
	uint32_t            reg;
	const unsigned char *src;
	static int          tab_ok = 0;
	static uint32_t     tab[256];

	if (tab_ok == 0) {
		for (i = 0; i < 256; i++) {
			reg = (uint32_t) i << 24;

			for (j = 0; j < 8; j++) {
				if (reg & 0x80000000) {
					reg = (reg << 1) ^ PFI_CRC_POLY;
				}
				else {
					reg = reg << 1;
				}
			}

			tab[i] = reg;
		}

		tab_ok = 1;
	}

	src = buf;

	while (cnt > 0) {
		val = (crc >> 24) ^ *(src++);
		crc = (crc << 8) ^ tab[val & 0xff];
		cnt -= 1;
	}

	return (crc & 0xffffffff);
}

static
int pfi_read_crc (pfi_load_t *pfi, void *buf, unsigned long cnt)
{
	if (pfi_read (pfi->fp, buf, cnt)) {
		return (1);
	}

	pfi->crc = pfi_crc (pfi->crc, buf, cnt);

	return (0);
}

static
unsigned char *pfi_alloc (pfi_load_t *pfi, unsigned long size)
{
	unsigned char *tmp;

	if (pfi->bufmax < size) {
		if ((tmp = realloc (pfi->buf, size)) == NULL) {
			return (NULL);
		}

		pfi->buf = tmp;
		pfi->bufmax = size;
	}

	return (pfi->buf);
}

static
void pfi_free (pfi_load_t *pfi)
{
	free (pfi->buf);

	pfi->buf = NULL;
	pfi->bufmax = 0;
}

static
int pfi_skip_chunk (pfi_load_t *pfi, unsigned long size)
{
	unsigned      cnt;
	unsigned char *buf;

	if ((buf = pfi_alloc (pfi, 4096)) == NULL) {
		return (1);
	}

	while (size > 0) {
		cnt = (size < 4096) ? size : 4096;

		if (pfi_read_crc (pfi, buf, cnt)) {
			return (1);
		}

		size -= cnt;
	}

	if (pfi_read (pfi->fp, buf, 4)) {
		return (1);
	}

	if (pfi_get_uint32_be (buf, 0) != pfi->crc) {
		fprintf (stderr, "pfi: crc error\n");
		return (1);
	}

	return (0);
}

static
int pfi_load_header (pfi_load_t *pfi, unsigned long size)
{
	unsigned char buf[4];
	unsigned long vers;

	if (size < 4) {
		return (1);
	}

	if (pfi_read_crc (pfi, buf, 4)) {
		return (1);
	}

	vers = pfi_get_uint32_be (buf, 0);

	if (vers != 0) {
		fprintf (stderr, "pfi: unknown version number (%lu)\n", vers);
		return (1);
	}

	if (pfi_skip_chunk (pfi, size - 4)) {
		return (1);
	}

	return (0);
}

static
int pfi_load_comment (pfi_load_t *pfi, unsigned size)
{
	unsigned      i, j, k, d;
	unsigned char *buf;

	if (size == 0) {
		return (pfi_skip_chunk (pfi, size));
	}

	if ((buf = pfi_alloc (pfi, size)) == NULL) {
		return (1);
	}

	if (pfi_read_crc (pfi, buf, size)) {
		return (1);
	}

	i = 0;
	j = size;

	while (i < j) {
		if ((buf[i] == 0x0d) || (buf[i] == 0x0a)) {
			i += 1;
		}
		else if (buf[i] == 0x00) {
			i += 1;
		}
		else {
			break;
		}
	}

	while (j > i) {
		if ((buf[j - 1] == 0x0d) || (buf[j - 1] == 0x0a)) {
			j -= 1;
		}
		else if (buf[j - 1] == 0x00) {
			j += 1;
		}
		else {
			break;
		}
	}

	if (i == j) {
		return (pfi_skip_chunk (pfi, 0));
	}

	k = i;
	d = i;

	while (k < j) {
		if (buf[k] == 0x0d) {
			if (((k + 1) < j) && (buf[k + 1] == 0x0a)) {
				k += 1;
			}
			else {
				buf[d++] = 0x0a;
			}
		}
		else {
			buf[d++] = buf[k];
		}

		k += 1;
	}

	j = d;

	if (pfi->img->comment_size > 0) {
		unsigned char c;

		c = 0x0a;

		if (pfi_img_add_comment (pfi->img, &c, 1)) {
			return (1);
		}
	}

	if (pfi_img_add_comment (pfi->img, buf + i, j - i)) {
		return (1);
	}

	if (pfi_skip_chunk (pfi, 0)) {
		return (1);
	}

	return (0);
}

static
int pfi_load_track_header (pfi_load_t *pfi, unsigned long size)
{
	unsigned char buf[12];
	unsigned long c, h, clock;

	if (size < 12) {
		return (1);
	}

	if (pfi_read_crc (pfi, buf, 12)) {
		return (1);
	}

	size -= 12;

	c = pfi_get_uint32_be (buf, 0);
	h = pfi_get_uint32_be (buf, 4);
	clock = pfi_get_uint32_be (buf, 8);

	pfi->trk = pfi_img_get_track (pfi->img, c, h, 1);

	if (pfi->trk == NULL) {
		return (1);
	}

	pfi_trk_set_clock (pfi->trk, clock);

	if (pfi_skip_chunk (pfi, size)) {
		return (1);
	}

	return (0);
}

static
int pfi_load_indx (pfi_load_t *pfi, unsigned long size)
{
	unsigned char buf[4];

	while (size >= 4) {
		if (pfi_read_crc (pfi, buf, 4)) {
			return (1);
		}

		if (pfi_trk_add_index (pfi->trk, pfi_get_uint32_be (buf, 0))) {
			return (1);
		}

		size -= 4;
	}

	if (pfi_skip_chunk (pfi, size)) {
		return (1);
	}

	return (0);
}

static
int pfi_decode_track_data (pfi_load_t *pfi, unsigned char *buf, unsigned long size)
{
	unsigned      cnt;
	unsigned long pos;
	uint32_t      val;

	if (pfi_read_crc (pfi, buf, size)) {
		return (1);
	}

	pos = 0;

	while (pos < size) {
		cnt = buf[pos++];

		if (cnt < 4) {
			if ((pos + cnt + 1) > size) {
				return (1);
			}

			val = buf[pos++];

			while (cnt > 0) {
				val = (val << 8) | buf[pos++];
				cnt -= 1;
			}
		}
		else if (cnt < 8) {
			if ((pos + 1) > size) {
				return (1);
			}

			val = ((cnt << 8) | buf[pos++]) & 0x03ff;
		}
		else {
			val = cnt;
		}

		if (pfi_trk_add_pulse (pfi->trk, val)) {
			return (1);
		}
	}

	pfi_trk_clean (pfi->trk);

	return (0);
}

static
int pfi_load_track_data (pfi_load_t *pfi, unsigned long size)
{
	unsigned char *buf;

	if ((buf = pfi_alloc (pfi, size)) == NULL) {
		return (1);
	}

	if (pfi_decode_track_data (pfi, buf, size)) {
		return (1);
	}

	if (pfi_skip_chunk (pfi, 0)) {
		return (1);
	}

	return (0);
}

static
int pfi_load_image (pfi_load_t *pfi)
{
	unsigned long type, size;
	unsigned char buf[8];

	pfi->have_header = 0;
	pfi->trk = NULL;

	while (1) {
		pfi->crc = 0;

		if (pfi_read_crc (pfi, buf, 8)) {
			return (1);
		}

		type = pfi_get_uint32_be (buf, 0);
		size = pfi_get_uint32_be (buf, 4);

		if (type == PFI_MAGIC_END) {
			if (pfi_skip_chunk (pfi, size)) {
				return (1);
			}

			return (0);
		}
		else if (type == PFI_MAGIC_PFI) {
			if (pfi->have_header) {
				return (1);
			}

			if (pfi_load_header (pfi, size)) {
				return (1);
			}

			pfi->have_header = 1;
		}
		else if (type == PFI_MAGIC_TEXT) {
			if (pfi->have_header == 0) {
				return (1);
			}

			if (pfi_load_comment (pfi, size)) {
				return (1);
			}
		}
		else if (type == PFI_MAGIC_TRAK) {
			if (pfi->have_header == 0) {
				return (1);
			}

			if (pfi_load_track_header (pfi, size)) {
				return (1);
			}
		}
		else if (type == PFI_MAGIC_INDX) {
			if (pfi->trk == NULL) {
				return (1);
			}

			if (pfi_load_indx (pfi, size)) {
				return (1);
			}
		}
		else if (type == PFI_MAGIC_DATA) {
			if (pfi->trk == NULL) {
				return (1);
			}

			if (pfi_load_track_data (pfi, size)) {
				return (1);
			}
		}
		else {
			if (pfi->have_header == 0) {
				return (1);
			}

			if (pfi_skip_chunk (pfi, size)) {
				return (1);
			}
		}
	}

	return (1);
}

pfi_img_t *pfi_load_pfi (FILE *fp)
{
	int        r;
	pfi_load_t pfi;

	pfi.fp = fp;
	pfi.img = NULL;
	pfi.trk = NULL;
	pfi.buf = NULL;
	pfi.bufmax = 0;

	if ((pfi.img = pfi_img_new()) == NULL) {
		return (NULL);
	}

	r = pfi_load_image (&pfi);

	pfi_free (&pfi);

	if (r) {
		pfi_img_del (pfi.img);
		return (NULL);
	}

	return (pfi.img);
}


static
int pfi_save_header (FILE *fp, const pfi_img_t *img)
{
	unsigned char buf[16];

	pfi_set_uint32_be (buf, 0, PFI_MAGIC_PFI);
	pfi_set_uint32_be (buf, 4, 4);
	pfi_set_uint32_be (buf, 8, 0);
	pfi_set_uint32_be (buf, 12, pfi_crc (0, buf, 12));

	if (pfi_write (fp, buf, 16)) {
		return (1);
	}

	return (0);
}

static
int pfi_save_end (FILE *fp, const pfi_img_t *img)
{
	unsigned char buf[16];

	pfi_set_uint32_be (buf, 0, PFI_MAGIC_END);
	pfi_set_uint32_be (buf, 4, 0);
	pfi_set_uint32_be (buf, 8, pfi_crc (0, buf, 8));

	if (pfi_write (fp, buf, 12)) {
		return (1);
	}

	return (0);
}

static
int pfi_save_comment (FILE *fp, const pfi_img_t *img)
{
	unsigned long       i, j;
	unsigned long       crc;
	const unsigned char *src;
	unsigned char       *buf;
	unsigned char       hdr[8];

	if (img->comment_size == 0) {
		return (0);
	}

	buf = malloc (img->comment_size + 2);

	if (buf == NULL) {
		return (1);
	}

	src = img->comment;

	buf[0] = 0x0a;

	i = 0;
	j = 1;

	while (i < img->comment_size) {
		if ((src[i] == 0x0d) || (src[i] == 0x0a)) {
			i += 1;
		}
		else if (src[i] == 0x00) {
			i += 1;
		}
		else {
			break;
		}
	}

	while (i < img->comment_size) {
		if (src[i] == 0x0d) {
			if (((i + 1) < img->comment_size) && (src[i + 1] == 0x0a)) {
				i += 1;
			}
			else {
				buf[j++] = 0x0a;
			}
		}
		else {
			buf[j++] = src[i];
		}

		i += 1;
	}

	while (j > 1) {
		if ((buf[j - 1] == 0x0a) || (buf[j - 1] == 0x00)) {
			j -= 1;
		}
		else {
			break;
		}
	}

	if (j == 1) {
		free (buf);
		return (0);
	}

	buf[j++] = 0x0a;

	pfi_set_uint32_be (hdr, 0, PFI_MAGIC_TEXT);
	pfi_set_uint32_be (hdr, 4, j);

	crc = pfi_crc (0, hdr, 8);

	if (pfi_write (fp, hdr, 8)) {
		return (1);
	}

	crc = pfi_crc (crc, buf, j);

	if (pfi_write (fp, buf, j)) {
		return (1);
	}

	pfi_set_uint32_be (hdr, 0, crc);

	if (pfi_write (fp, hdr, 4)) {
		return (1);
	}

	return (0);
}

static
int pfi_save_track_header (FILE *fp, const pfi_trk_t *trk, unsigned long c, unsigned long h)
{
	unsigned char buf[32];

	pfi_set_uint32_be (buf, 0, PFI_MAGIC_TRAK);
	pfi_set_uint32_be (buf, 4, 12);
	pfi_set_uint32_be (buf, 8, c);
	pfi_set_uint32_be (buf, 12, h);
	pfi_set_uint32_be (buf, 16, trk->clock);
	pfi_set_uint32_be (buf, 20, pfi_crc (0, buf, 20));

	if (pfi_write (fp, buf, 24)) {
		return (1);
	}

	return (0);
}

static
int pfi_save_indx (FILE *fp, const pfi_trk_t *trk)
{
	unsigned      i;
	unsigned long crc;
	unsigned char buf[8];

	if (trk->index_cnt == 0) {
		return (0);
	}

	pfi_set_uint32_be (buf, 0, PFI_MAGIC_INDX);
	pfi_set_uint32_be (buf, 4, 4 * trk->index_cnt);

	crc = pfi_crc (0, buf, 8);

	if (pfi_write (fp, buf, 8)) {
		return (1);
	}

	for (i = 0; i < trk->index_cnt; i++) {
		pfi_set_uint32_be (buf, 0, trk->index[i]);

		crc = pfi_crc (crc, buf, 4);

		if (pfi_write (fp, buf, 4)) {
			return (1);
		}
	}

	pfi_set_uint32_be (buf, 0, crc);

	if (pfi_write (fp, buf, 4)) {
		return (1);
	}

	return (0);
}

static
int pfi_save_data (FILE *fp, const pfi_trk_t *trk)
{
	int           r;
	uint32_t      val;
	unsigned long i, n, max;
	unsigned char *buf, *tmp;

	if (trk->pulse_cnt == 0) {
		return (0);
	}

	max = 4096;

	if ((buf = malloc (max)) == NULL) {
		return (1);
	}

	pfi_set_uint32_be (buf, 0, PFI_MAGIC_DATA);

	n = 8;

	for (i = 0; i < trk->pulse_cnt; i++) {
		val = trk->pulse[i];

		if (val == 0) {
			continue;
		}

		if ((max - n) < 16) {
			max *= 2;

			if ((tmp = realloc (buf, max)) == 0) {
				free (buf);
				return (1);
			}

			buf = tmp;
		}

		if (val < 8) {
			buf[n++] = 4;
			buf[n++] = val;
		}
		else if (val < (1UL << 8)) {
			buf[n++] = val;
		}
		else if (val < (1UL << 10)) {
			buf[n++] = ((val >> 8) & 3) | 4;
			buf[n++] = val & 0xff;
		}
		else if (val < (1UL << 16)) {
			buf[n++] = 1;
			buf[n++] = (val >> 8) & 0xff;
			buf[n++] = val & 0xff;
		}
		else if (val < (1UL << 24)) {
			buf[n++] = 2;
			buf[n++] = (val >> 16) & 0xff;
			buf[n++] = (val >> 8) & 0xff;
			buf[n++] = val & 0xff;
		}
		else {
			buf[n++] = 3;
			buf[n++] = (val >> 24) & 0xff;
			buf[n++] = (val >> 16) & 0xff;
			buf[n++] = (val >> 8) & 0xff;
			buf[n++] = val & 0xff;
		}
	}

	pfi_set_uint32_be (buf, 4, n - 8);
	pfi_set_uint32_be (buf, n, pfi_crc (0, buf, n));

	r = pfi_write (fp, buf, n + 4);

	free (buf);

	return (r);
}

static
int pfi_save_track (FILE *fp, const pfi_trk_t *trk, unsigned long c, unsigned long h)
{
	if (pfi_save_track_header (fp, trk, c, h)) {
		return (1);
	}

	if (pfi_save_indx (fp, trk)) {
		return (1);
	}

	if (pfi_save_data (fp, trk)) {
		return (1);
	}

	return (0);
}

int pfi_save_pfi (FILE *fp, const pfi_img_t *img)
{
	unsigned long c, h;
	pfi_cyl_t     *cyl;
	pfi_trk_t     *trk;

	if (pfi_save_header (fp, img)) {
		return (1);
	}

	if (pfi_save_comment (fp, img)) {
		return (1);
	}

	for (c = 0; c < img->cyl_cnt; c++) {
		if ((cyl = img->cyl[c]) == NULL) {
			continue;
		}

		for (h = 0; h < cyl->trk_cnt; h++) {
			if ((trk = cyl->trk[h]) == NULL) {
				continue;
			}

			if (pfi_save_track (fp, trk, c, h)) {
				return (1);
			}
		}
	}

	if (pfi_save_end (fp, img)) {
		return (1);
	}

	return (0);
}


int pfi_probe_pfi_fp (FILE *fp)
{
	unsigned char buf[4];

	if (pfi_read (fp, buf, 4)) {
		return (0);
	}

	if (pfi_get_uint32_be (buf, 0) != PFI_MAGIC_PFI) {
		return (0);
	}

	return (1);
}

int pfi_probe_pfi (const char *fname)
{
	int  r;
	FILE *fp;

	if ((fp = fopen (fname, "rb")) == NULL) {
		return (0);
	}

	r = pfi_probe_pfi_fp (fp);

	fclose (fp);

	return (r);
}
