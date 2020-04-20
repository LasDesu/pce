/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/utils/pri/rotate.c                                       *
 * Created:     2013-12-19 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2013-2020 Hampa Hug <hampa@hampa.ch>                     *
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


#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/pri/pri.h>


static
int pri_half_rate_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	unsigned long i, j, n;
	unsigned char *buf;
	pri_evt_t     *evt;

	buf = trk->data;

	j = 0;
	n = pri_trk_get_size (trk) / 2;

	for (i = 0; i < n; i++) {
		if (buf[j >> 3] & 0x80 >> (j & 7)) {
			buf[i >> 3] |= 0x80 >> (i & 7);
		}
		else {
			buf[i >> 3] &= ~(0x80 >> (i & 7));
		}

		j += 2;
	}

	evt = trk->evt;

	while (evt != NULL) {
		evt->pos >>= 1;
		evt = evt->next;
	}

	pri_trk_set_size (trk, n);
	pri_trk_set_clock (trk, pri_trk_get_clock (trk) / 2);

	return (0);
}

int pri_half_rate (pri_img_t *img)
{
	return (pri_for_all_tracks (img, pri_half_rate_cb, NULL));
}


static
int pri_mac_gcr_align_sector_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	unsigned      rev;
	unsigned      min_sct, sct;
	unsigned long min_pos;
	unsigned long bit, buf1, buf2;

	pri_trk_set_pos (trk, 0);

	buf1 = 0;
	buf2 = 0;

	min_pos = 0;
	min_sct = -1;

	rev = 0;

	while (rev < 2) {
		pri_trk_get_bits (trk, &bit, 1);

		buf1 = ((buf1 << 1) & 0xffffffff) | ((buf2 >> 31) & 1);
		buf2 = ((buf2 << 1) & 0xffffffff) | (bit & 1);

		if ((buf1 & 0xffffff00) == 0xd5aa9600) {
			sct = (buf2 >> 24) & 0xff;

			if (sct < min_sct) {
				min_sct = sct;
				min_pos = trk->idx - 64;
			}
		}

		if (trk->wrap) {
			rev += 1;
			trk->wrap = 0;
		}
	}

	if (par_verbose) {
		fprintf (stderr, "track %lu/%lu rotate %6lu / %6lu\n",
			c, h, min_pos, trk->size
		);
	}

	if (min_pos != 0) {
		pri_trk_rotate (trk, min_pos);
	}

	return (0);
}

int pri_mac_gcr_align_sector (pri_img_t *img)
{
	return (pri_for_all_tracks (img, pri_mac_gcr_align_sector_cb, NULL));
}


static
int pri_mac_gcr_align_sync_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	unsigned      val, run;
	unsigned long bit;
	unsigned long syn1, syn2, syn_cnt;
	unsigned long max1, max2, max_cnt;

	pri_trk_set_pos (trk, 0);

	syn1 = 0;
	syn2 = 0;
	syn_cnt = 0;

	max1 = 0;
	max2 = 0;
	max_cnt = 0;

	val = 0;
	run = 0;

	while (1) {
		pri_trk_get_bits (trk, &bit, 1);

		val = (val << 1) | (bit & 1);
		run += 1;

		if ((val & 0x3ff) == 0xff) {
			syn2 = trk->idx;
			syn_cnt += 1;

			if (syn_cnt > max_cnt) {
				max1 = syn1;
				max2 = syn2;
				max_cnt = syn_cnt;
			}

			run = 0;
		}
		else if (run >= 18) {
			if (trk->wrap) {
				break;
			}

			syn1 = trk->idx;
			syn_cnt = 0;
		}
	}

	max1 = (max1 + (max2 - max1) / 2) % trk->size;

	if (par_verbose) {
		fprintf (stderr, "track %lu/%lu rotate %6lu / %6lu\n",
			c, h, max1, trk->size
		);
	}

	pri_trk_rotate (trk, max1);

	return (0);
}

int pri_mac_gcr_align_sync (pri_img_t *img)
{
	return (pri_for_all_tracks (img, pri_mac_gcr_align_sync_cb, NULL));
}


struct pri_mfm_align_am_s {
	unsigned long pos;
	unsigned long index;
	char          iam;
	char          idam;
	char          dam;
};

static
int pri_mfm_align_am_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	int                       mark;
	unsigned long             bit, val1, val2;
	unsigned long             ofs, pos, size;
	unsigned long             amcnt;
	struct pri_mfm_align_am_s *par;

	par = opaque;

	size = pri_trk_get_size (trk);

	if (size < 64) {
		return (0);
	}

	if ((size + par->pos) < size) {
		pos = size + par->pos;
	}
	else {
		pos = par->pos;
	}

	if (pos >= size) {
		return (1);
	}

	amcnt = 0;
	ofs = 0;

	pri_trk_set_pos (trk, size - 64);

	pri_trk_get_bits (trk, &val1, 32);
	pri_trk_get_bits (trk, &val2, 32);

	pri_trk_set_pos (trk, 0);

	while (trk->wrap == 0) {
		pri_trk_get_bits (trk, &bit, 1);

		val1 = ((val1 << 1) | (val2 >> 31)) & 0xffffffff;
		val2 = ((val2 << 1) | (bit & 1)) & 0xffffffff;
		ofs += 1;

		mark = 0;

		if (par->iam) {
			if ((val1 == 0x52245224) && (val2 == 0x52245552)) {
				/* IAM: C2 C2 C2 FC */
				mark = 1;
			}
		}

		if (par->idam) {
			if ((val1 == 0x44894489) && (val2 == 0x44895554)) {
				/* IDAM: A1 A1 A1 FE */
				mark = 1;
			}
		}

		if (par->dam) {
			if ((val1 == 0x44894489) && (val2 == 0x44895545)) {
				/* DAM: A1 A1 A1 FB */
				mark = 1;
			}
		}

		if (mark) {
			amcnt += 1;

			if (amcnt < par->index) {
				continue;
			}

			ofs = (ofs + 2 * size - pos - 64) % size;

			if (ofs != 0) {
				if (pri_trk_rotate (trk, ofs)) {
					return (1);
				}
			}

			return (0);
		}
	}

	return (0);
}

static
int is_prefix (const char **str, const char *pre)
{
	const char *tmp;

	tmp = *str;

	while (*pre != 0) {
		if (*(tmp++) != *(pre++)) {
			return (0);
		}
	}

	if ((*tmp == 0) || (*tmp == '+') || (*tmp == '-') || (*tmp == ' ')) {
		*str = tmp;
		return (1);
	}

	return (0);
}

int pri_mfm_align_am (pri_img_t *img, const char *what, const char *idx, const char *pos)
{
	int                       sign;
	struct pri_mfm_align_am_s par;

	par.pos = strtoul (pos, NULL, 0);
	par.index = strtoul (idx, NULL, 0);

	par.iam = 0;
	par.idam = 0;
	par.dam = 0;

	sign = 1;

	while (*what != 0) {
		if (*what == '+') {
			sign = 1;
			what += 1;
		}
		else if (*what == '-') {
			sign = 0;
			what += 1;
		}
		else if (*what == ' ') {
			what += 1;
		}
		else if (is_prefix (&what, "all")) {
			par.iam = sign;
			par.idam = sign;
			par.dam = sign;
		}
		else if (is_prefix (&what, "iam")) {
			par.iam = sign;
		}
		else if (is_prefix (&what, "idam")) {
			par.idam = sign;
		}
		else if (is_prefix (&what, "dam")) {
			par.dam = sign;
		}
		else {
			return (1);
		}
	}

	return (pri_for_all_tracks (img, pri_mfm_align_am_cb, &par));
}


static
int pri_rotate_tracks_angle_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	double        val;
	unsigned long cnt, max;

	max = pri_trk_get_size (trk);

	if (max == 0) {
		return (0);
	}

	val = *(double *) opaque;
	cnt = (val * max) / 360.0;

	if (pri_trk_rotate (trk, cnt)) {
		return (1);
	}

	return (0);
}

int pri_rotate_tracks_angle (pri_img_t *img, double val)
{
	while (val < 0.0) {
		val += 360.0;
	}

	while (val >= 360.0) {
		val -= 360.0;
	}

	return (pri_for_all_tracks (img, pri_rotate_tracks_angle_cb, &val));
}


struct pri_rotate_s {
	char          left;
	unsigned long cnt;
};

static
int pri_rotate_track_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	unsigned long       cnt, max;
	struct pri_rotate_s *par;

	par = opaque;

	max = pri_trk_get_size (trk);

	if (max == 0) {
		return (0);
	}

	if (par->left) {
		cnt = par->cnt % max;
	}
	else {
		cnt = max - (par->cnt % max);
	}

	if (pri_trk_rotate (trk, cnt)) {
		return (1);
	}

	return (0);
}

int pri_rotate_tracks (pri_img_t *img, long ofs)
{
	struct pri_rotate_s par;

	if (ofs < 0) {
		par.left = 0;
		par.cnt = -ofs;
	}
	else {
		par.left = 1;
		par.cnt = ofs;
	}

	return (pri_for_all_tracks (img, pri_rotate_track_cb, &par));
}

int pri_rotate_tracks_left (pri_img_t *img, unsigned long ofs)
{
	struct pri_rotate_s par;

	par.left = 1;
	par.cnt = ofs;

	return (pri_for_all_tracks (img, pri_rotate_track_cb, &par));
}

int pri_rotate_tracks_right (pri_img_t *img, unsigned long ofs)
{
	struct pri_rotate_s par;

	par.left = 0;
	par.cnt = ofs;

	return (pri_for_all_tracks (img, pri_rotate_track_cb, &par));
}
