/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/drivers/pri/pri-enc-mfm.c                                *
 * Created:     2012-02-01 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2012-2018 Hampa Hug <hampa@hampa.ch>                     *
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

#include <drivers/psi/psi.h>

#include "pri.h"
#include "pri-img.h"
#include "pri-enc-mfm.h"


typedef struct {
	pri_trk_t     *trk;

	char          last;
	char          clock;

	unsigned      min_sct_size;

	unsigned      crc;

	unsigned long last_gap3_start;
} mfm_code_t;


/*
 * Calculate the CRC for MFM
 */
static
unsigned mfm_crc (unsigned crc, const void *buf, unsigned cnt)
{
	unsigned            i;
	const unsigned char *src;

	src = buf;

	while (cnt > 0) {
		crc ^= (unsigned) *src << 8;

		for (i = 0; i < 8; i++) {
			if (crc & 0x8000) {
				crc = (crc << 1) ^ 0x1021;
			}
			else {
				crc = crc << 1;
			}
		}

		src += 1;
		cnt -= 1;
	}

	return (crc & 0xffff);
}


/*
 * Get the next MFM bit
 */
static
int mfm_get_bit (mfm_code_t *mfm)
{
	unsigned long bit;

	pri_trk_get_bits (mfm->trk, &bit, 1);

	mfm->clock = !mfm->clock;

	return (bit & 1);
}

/*
 * Read an MFM byte
 */
static
unsigned char mfm_decode_byte (mfm_code_t *mfm)
{
	unsigned i;
	unsigned val;

	if (mfm->clock) {
		mfm_get_bit (mfm);
	}

	val = 0;

	for (i = 0; i < 8; i++) {
		val = (val << 1) | (mfm_get_bit (mfm) != 0);

		mfm_get_bit (mfm);
	}

	return (val);
}

/*
 * Read MFM data
 */
static
void mfm_read (mfm_code_t *mfm, unsigned char *buf, unsigned cnt)
{
	unsigned long i;

	for (i = 0; i < cnt; i++) {
		buf[i] = mfm_decode_byte (mfm);
	}
}

/*
 * Sync with an MFM bit stream.
 *
 * MFM mark:
 *   A1 = 10100001 / 0A = 00001010
 *   4489 = [0] 1 [0] 0 [0] 1 [0] 0 [1] 0 [0] 0 [1] 0 [0] 1
 *
 * If mfm_sync() returns successfully the stream position is the clock
 * bit between the last mark byte and the next byte.
 */
static
int mfm_sync (mfm_code_t *mfm)
{
	unsigned long pos;
	unsigned      v1, v2, v3;

	v1 = 0;
	v2 = 0;
	v3 = 0;

	pos = mfm->trk->idx;

	while (1) {
		v1 = ((v1 << 1) | (v2 >> 15)) & 0xffff;
		v2 = ((v2 << 1) | (v3 >> 15)) & 0xffff;
		v3 = (v3 << 1) & 0xffff;

		if (mfm_get_bit (mfm)) {
			v3 |= 1;
		}

		if ((v1 == 0x4489) && (v2 == 0x4489) && (v3 == 0x4489)) {
			break;
		}

		if (mfm->trk->idx == pos) {
			return (1);
		}
	}

	mfm->clock = 1;

	return (0);
}

/*
 * Sync with the next MFM mark and initialize the CRC.
 */
static
int mfm_sync_mark (mfm_code_t *mfm, unsigned char *val)
{
	if (mfm_sync (mfm)) {
		return (1);
	}

	*val = mfm_decode_byte (mfm);

	mfm->crc = mfm_crc (0xffff, "\xa1\xa1\xa1", 3);
	mfm->crc = mfm_crc (mfm->crc, val, 1);

	return (0);
}

static
psi_sct_t *mfm_decode_idam (mfm_code_t *mfm)
{
	unsigned      c, h, s, n;
	unsigned      crc;
	unsigned long pos;
	unsigned char buf[8];
	psi_sct_t     *sct;

	pos = mfm->trk->idx;

	mfm_read (mfm, buf, 6);

	crc = pri_get_uint16_be (buf, 4);

	mfm->crc = mfm_crc (mfm->crc, buf, 4);

	c = buf[0];
	h = buf[1];
	s = buf[2];
	n = buf[3];

	n = 128 << ((n < 8) ? n : 8);

	sct = psi_sct_new (c, h, s, n);

	if (sct == NULL) {
		return (NULL);
	}

	psi_sct_set_mfm_size (sct, buf[3]);
	psi_sct_set_position (sct, pos / 2);
	psi_sct_set_flags (sct, PSI_FLAG_NO_DAM, 1);

	if (mfm->crc != crc) {
		psi_sct_set_flags (sct, PSI_FLAG_CRC_ID, 1);
	}

	psi_sct_fill (sct, 0);

	return (sct);
}

/*
 * Look for weak bits in the data area of sct, assuming that the
 * data area starts at the current position in trk. If weak bits are
 * found, set the weak bit mask of sct.
 */
static
int mfm_decode_weak (psi_sct_t *sct, pri_trk_t *trk)
{
	unsigned long ofs, val, idx;
	pri_evt_t     *evt, *e;

	evt = pri_trk_evt_get_after (trk, PRI_EVENT_WEAK, trk->idx);

	while (evt != NULL) {
		e = evt;
		evt = evt->next;

		if (e->type != PRI_EVENT_WEAK) {
			continue;
		}

		ofs = e->pos - trk->idx;
		val = e->val;

		if (ofs >= (16UL * sct->n)) {
			break;
		}

		if (psi_weak_alloc (sct)) {
			return (1);
		}

		while (val != 0) {
			if ((ofs & 1) == 0) {
				idx = ofs >> 1;
				if (val & 0x80000000) {
					sct->weak[idx >> 3] |= 0x80 >> (idx & 7);
				}
			}

			ofs += 1;
			val = (val << 1) & 0xffffffff;
		}
	}

	psi_weak_clean (sct);

	return (0);
}

static
int mfm_decode_time (psi_sct_t *sct, pri_trk_t *trk)
{
	unsigned long      cnt, val, pos, clk, clk0;
	unsigned long long sum, add;
	pri_evt_t          *evt;

	pos = pri_trk_get_pos (trk);
	clk0 = pri_trk_get_clock (trk);

	evt = pri_trk_evt_get_before (trk, PRI_EVENT_CLOCK, pos);

	if (evt != NULL) {
		clk = pri_evt_get_clock (evt, clk0);
		evt = pri_evt_next (evt, PRI_EVENT_CLOCK);
	}
	else {
		clk = clk0;
		evt = pri_trk_evt_get_after (trk, PRI_EVENT_CLOCK, pos);

		if (evt == NULL) {
			return (0);
		}
	}

	cnt = 16UL * sct->n;
	add = (32768ULL * clk0) / clk;
	sum = 0;

	while (cnt > 0) {
		while ((evt != NULL) && (evt->pos == pos)) {
			clk = pri_evt_get_clock (evt, clk0);
			add = (32768ULL * clk0) / clk;

			if ((evt = pri_evt_next (evt, PRI_EVENT_CLOCK)) == NULL) {
				evt = pri_trk_evt_get_idx (trk, PRI_EVENT_CLOCK, 0);
			}
		}

		sum += add;
		pos += 1;
		cnt -= 1;

		if (pos >= trk->size) {
			pos = 0;
		}
	}

	val = (sum + 32767) / 65536;

	if (val != (8UL * sct->n)) {
		psi_sct_set_read_time (sct, val);
	}

	return (0);
}

static
int mfm_decode_dam (mfm_code_t *mfm, psi_sct_t *sct, unsigned mark)
{
	unsigned      crc;
	unsigned char buf[4];

	if (mfm_decode_weak (sct, mfm->trk)) {
		return (1);
	}

	if (mfm_decode_time (sct, mfm->trk)) {
		return (1);
	}

	mfm_read (mfm, sct->data, sct->n);
	mfm_read (mfm, buf, 2);

	psi_sct_set_flags (sct, PSI_FLAG_NO_DAM, 0);
	psi_sct_set_flags (sct, PSI_FLAG_DEL_DAM, mark == 0xf8);

	crc = pri_get_uint16_be (buf, 0);

	mfm->crc = mfm_crc (mfm->crc, sct->data, sct->n);

	if (mfm->crc != crc) {
		psi_sct_set_flags (sct, PSI_FLAG_CRC_DATA, 1);
	}

	return (0);
}

static
int mfm_decode_mark (mfm_code_t *mfm, psi_trk_t *trk, unsigned mark)
{
	int           r;
	unsigned char mark2;
	char          wrap;
	unsigned long pos;
	psi_sct_t     *sct;

	switch (mark) {
	case 0xfe: /* ID address mark */
		sct = mfm_decode_idam (mfm);

		if (sct == NULL) {
			return (1);
		}

		psi_sct_set_encoding (sct, PSI_ENC_MFM);

		if (sct->n < mfm->min_sct_size) {
			psi_sct_set_size (sct, mfm->min_sct_size, 0);
		}

		pos = mfm->trk->idx;
		wrap = mfm->trk->wrap;

		r = mfm_sync_mark (mfm, &mark2);

		if (r == 0) {
			if ((mark2 == 0xf8) || (mark2 == 0xfb)) {
				pos = mfm->trk->idx;
				wrap = mfm->trk->wrap;

				if (mfm_decode_dam (mfm, sct, mark2)) {
					psi_sct_del (sct);
					return (1);
				}
			}
		}

		if (sct->flags & PSI_FLAG_NO_DAM) {
			psi_sct_set_size (sct, 0, 0);
		}

		mfm->trk->idx = pos;
		mfm->trk->wrap = wrap;

		psi_trk_add_sector (trk, sct);
		break;

	case 0xfb: /* data address mark */
	case 0xf8: /* deleted data address mark */
		fprintf (stderr, "mfm: dam without idam\n");
		break;

	default:
		fprintf (stderr,
			"mfm: unknown mark (0x%02x)\n", mark
		);
	}

	return (0);
}

psi_trk_t *pri_decode_mfm_trk (pri_trk_t *trk, unsigned h, pri_dec_mfm_t *par)
{
	unsigned char mark;
	psi_trk_t     *dtrk;
	pri_dec_mfm_t def;
	mfm_code_t    mfm;

	if ((dtrk = psi_trk_new (h)) == NULL) {
		return (NULL);
	}

	if (par == NULL) {
		pri_decode_mfm_init (&def);

		par = &def;
	}

	mfm.trk = trk;
	mfm.clock = 0;
	mfm.min_sct_size = par->min_sct_size;

	pri_trk_set_pos (trk, 0);

	while (trk->wrap == 0) {
		if (mfm_sync_mark (&mfm, &mark)) {
			break;
		}

		if ((trk->wrap) && (trk->idx >= (4 * 16))) {
			break;
		}

		if (mfm_decode_mark (&mfm, dtrk, mark)) {
			psi_trk_del (dtrk);
			return (NULL);
		}
	}

	return (dtrk);
}

psi_img_t *pri_decode_mfm (pri_img_t *img, pri_dec_mfm_t *par)
{
	unsigned long c, h;
	pri_cyl_t     *cyl;
	pri_trk_t     *trk;
	psi_img_t     *dimg;
	psi_trk_t     *dtrk;

	dimg = psi_img_new();

	if (dimg == NULL) {
		return (NULL);
	}

	for (c = 0; c < img->cyl_cnt; c++) {
		cyl = img->cyl[c];

		if (cyl == NULL) {
			continue;
		}

		for (h = 0; h < cyl->trk_cnt; h++) {
			trk = cyl->trk[h];

			if (trk == NULL) {
				dtrk = psi_trk_new (h);
			}
			else {
				dtrk = pri_decode_mfm_trk (trk, h, par);
			}

			if (dtrk == NULL) {
				psi_img_del (dimg);
				return (NULL);
			}

			psi_img_add_track (dimg, dtrk, c);
		}
	}

	return (dimg);
}

void pri_decode_mfm_init (pri_dec_mfm_t *par)
{
	par->min_sct_size = 0;
}


static
void mfm_encode_byte (mfm_code_t *mfm, unsigned val, unsigned msk)
{
	unsigned      i;
	unsigned      buf;
	unsigned long max;

	if (mfm->trk->wrap) {
		return;
	}

	buf = (mfm->last != 0);

	for (i = 0; i < 8; i++) {
		buf = (buf << 2) | ((val >> 7) & 1);
		val = val << 1;

		if ((buf & 0x05) == 0) {
			buf |= 0x02;
		}
	}

	buf &= msk;

	max = mfm->trk->size - mfm->trk->idx;

	if (max > 16) {
		max = 16;
	}

	pri_trk_set_bits (mfm->trk, buf >> (16 - max), max);

	mfm->last = buf & 1;
}

static
void mfm_encode_bytes (mfm_code_t *mfm, const unsigned char *buf, unsigned cnt)
{
	while (cnt > 0) {
		mfm_encode_byte (mfm, *buf, 0xffff);
		buf += 1;
		cnt -= 1;
	}
}

static
void mfm_encode_mark (mfm_code_t *mfm, unsigned char mark)
{
	mfm_encode_byte (mfm, 0xa1, 0xffdf);
	mfm_encode_byte (mfm, 0xa1, 0xffdf);
	mfm_encode_byte (mfm, 0xa1, 0xffdf);
	mfm_encode_byte (mfm, mark, 0xffff);

	mfm->crc = mfm_crc (0xffff, "\xa1\xa1\xa1", 3);
	mfm->crc = mfm_crc (mfm->crc, &mark, 1);
}

static
void mfm_encode_data (mfm_code_t *mfm, psi_sct_t *sct)
{
	unsigned      i, n;
	unsigned long pos, msk, val;
	psi_sct_t     *fuz, *alt;

	pos = mfm->trk->idx;

	mfm_encode_bytes (mfm, sct->data, sct->n);

	if ((sct->next == NULL) && (sct->weak == NULL)) {
		return;
	}

	if ((fuz = psi_sct_clone (sct, 0)) == NULL) {
		return;
	}

	psi_sct_fill (fuz, 0);

	if (sct->weak != NULL) {
		n = (fuz->n < sct->n) ? fuz->n : sct->n;

		for (i = 0; i < n; i++) {
			fuz->data[i] |= sct->weak[i];
		}
	}

	alt = sct->next;

	while (alt != NULL) {
		n = (fuz->n < alt->n) ? fuz->n : alt->n;

		for (i = 0; i < n; i++) {
			fuz->data[i] |= sct->data[i] ^ alt->data[i];
		}

		if (alt->weak != NULL) {
			for (i = 0; i < n; i++) {
				fuz->data[i] |= alt->weak[i];
			}
		}

		alt = alt->next;
	}

	n = 8 * fuz->n;

	msk = 0;
	val = 0;

	for (i = 0; i < n; i++) {
		val = (val << 1) | ((fuz->data[i >> 3] >> (~i & 7)) & 1);
		msk = (msk << 2) | (val & 1);
		pos += 2;

		if ((val & 3) == 3) {
			msk |= 2;
		}

		if (msk & 0xc0000000) {
			pri_trk_evt_add (mfm->trk, PRI_EVENT_WEAK, pos - 32, msk);

			msk = 0;
		}
	}

	psi_sct_del (fuz);
}

static
void mfm_encode_sector (mfm_code_t *mfm, psi_sct_t *sct, unsigned gap3, int nopos)
{
	unsigned      i;
	unsigned      flags;
	unsigned long pos;
	unsigned char buf[8];
	int           clock;

	if ((sct->position != 0xffffffff) && (sct->position >= (16 * 8)) && (nopos == 0)) {
		pos = 2 * (sct->position - 16 * 8);

		if (pos > mfm->last_gap3_start) {
			pri_trk_set_pos (mfm->trk, pos);
		}
		else {
			pri_trk_set_pos (mfm->trk, mfm->last_gap3_start);
		}
	}

	flags = sct->flags;

	for (i = 0; i < 12; i++) {
		mfm_encode_byte (mfm, 0, 0xffff);
	}

	mfm_encode_mark (mfm, 0xfe);

	buf[0] = sct->c;
	buf[1] = sct->h;
	buf[2] = sct->s;
	buf[3] = psi_sct_get_mfm_size (sct);

	mfm->crc = mfm_crc (mfm->crc, buf, 4);

	if (flags & PSI_FLAG_CRC_ID) {
		mfm->crc = ~mfm->crc;
	}

	buf[4] = (mfm->crc >> 8) & 0xff;
	buf[5] = mfm->crc & 0xff;

	mfm_encode_bytes (mfm, buf, 6);

	for (i = 0; i < 22; i++) {
		mfm_encode_byte (mfm, 0x4e, 0xffff);
	}

	if (flags & PSI_FLAG_NO_DAM) {
		mfm->last_gap3_start = mfm->trk->idx;
		return;
	}

	for (i = 0; i < 12; i++) {
		mfm_encode_byte (mfm, 0x00, 0xffff);
	}

	mfm_encode_mark (mfm, (flags & PSI_FLAG_DEL_DAM) ? 0xf8 : 0xfb);

	clock = 0;

	if (sct->read_time > 0) {
		unsigned long val;

		val = (65536UL * 8 * sct->n) / sct->read_time;
		pri_trk_evt_add (mfm->trk, PRI_EVENT_CLOCK, mfm->trk->idx, val);
		clock = 1;
	}

	mfm_encode_data (mfm, sct);

	if (clock) {
		pri_trk_evt_add (mfm->trk, PRI_EVENT_CLOCK, mfm->trk->idx, 0);
	}

	mfm->crc = mfm_crc (mfm->crc, sct->data, sct->n);

	if (flags & PSI_FLAG_CRC_DATA) {
		mfm->crc = ~mfm->crc;
	}

	buf[0] = (mfm->crc >> 8) & 0xff;
	buf[1] = mfm->crc & 0xff;

	mfm_encode_bytes (mfm, buf, 2);

	mfm->last_gap3_start = mfm->trk->idx;

	for (i = 0; i < gap3; i++) {
		mfm_encode_byte (mfm, 0x4e, 0xffff);
	}
}

int pri_encode_mfm_trk (pri_trk_t *dtrk, psi_trk_t *strk, pri_enc_mfm_t *par)
{
	unsigned      i;
	unsigned long bits;
	unsigned      gap3, scnt;
	int           nopos;
	psi_sct_t     *sct;
	mfm_code_t    mfm;

	mfm.trk = dtrk;
	mfm.last = 0;
	mfm.last_gap3_start = 0;

	pri_trk_set_pos (dtrk, 0);

	nopos = par->nopos;

	gap3 = par->gap3;

	if (par->auto_gap3) {
		bits = par->gap4a + par->gap1;

		if (par->enable_iam) {
			bits += 16;
		}

		scnt = 0;

		for (i = 0; i < strk->sct_cnt; i++) {
			sct = strk->sct[i];

			if ((sct->flags & PSI_FLAG_NO_DAM) == 0) {
				scnt += 1;
				bits += strk->sct[i]->n + 12 + 10 + 22 + 12 + 4 + 2;
			}
			else {
				bits += strk->sct[i]->n + 12 + 10 + 22;
			}
		}

		bits *= 16;

		if (scnt > 0) {
			if (bits < dtrk->size) {
				gap3 = (dtrk->size - bits) / (16 * scnt);

				if (gap3 > par->gap3) {
					gap3 = par->gap3;
				}
			}
			else {
				pri_trk_set_size (dtrk, bits + 4 * 16 * scnt);
				gap3 = 4;
				nopos = 1;
			}
		}
	}

	for (i = 0; i < par->gap4a; i++) {
		mfm_encode_byte (&mfm, 0x4e, 0xffff);
	}

	if (par->enable_iam) {
		for (i = 0; i < 12; i++) {
			mfm_encode_byte (&mfm, 0x00, 0xffff);
		}

		mfm_encode_byte (&mfm, 0xc2, 0xff7f);
		mfm_encode_byte (&mfm, 0xc2, 0xff7f);
		mfm_encode_byte (&mfm, 0xc2, 0xff7f);
		mfm_encode_byte (&mfm, 0xfc, 0xffff);
	}

	for (i = 0; i < par->gap1; i++) {
		mfm_encode_byte (&mfm, 0x4e, 0xffff);
	}

	for (i = 0; i < strk->sct_cnt; i++) {
		sct = strk->sct[i];

		mfm_encode_sector (&mfm, sct, gap3, nopos);
	}

	while (dtrk->wrap == 0) {
		mfm_encode_byte (&mfm, 0x4e, 0xffff);
	}

	return (0);
}

int pri_encode_mfm_img (pri_img_t *dimg, psi_img_t *simg, pri_enc_mfm_t *par)
{
	unsigned long c, h;
	psi_cyl_t     *cyl;
	psi_trk_t     *trk;
	pri_trk_t     *dtrk;

	for (c = 0; c < simg->cyl_cnt; c++) {
		cyl = simg->cyl[c];

		for (h = 0; h < cyl->trk_cnt; h++) {
			trk = cyl->trk[h];

			dtrk = pri_img_get_track (dimg, c, h, 1);

			if (dtrk == NULL) {
				return (1);
			}

			if (pri_trk_set_size (dtrk, par->track_size)) {
				return (1);
			}

			pri_trk_set_clock (dtrk, par->clock);
			pri_trk_clear_16 (dtrk, 0x9254);

			if (pri_encode_mfm_trk (dtrk, trk, par)) {
				return (1);
			}
		}
	}

	return (0);
}

pri_img_t *pri_encode_mfm (psi_img_t *img, pri_enc_mfm_t *par)
{
	pri_img_t *dimg;

	dimg = pri_img_new();

	if (dimg == NULL) {
		return (NULL);
	}

	if (pri_encode_mfm_img (dimg, img, par)) {
		pri_img_del (dimg);
		return (NULL);
	}

	return (dimg);
}

void pri_encode_mfm_init (pri_enc_mfm_t *par, unsigned long clock, unsigned rpm)
{
	par->clock = clock;
	par->track_size = 60 * clock / rpm;

	par->enable_iam = 0;
	par->auto_gap3 = 1;

	par->nopos = 0;

	par->gap4a = 96;
	par->gap1 = 0;
	par->gap3 = 80;
}

pri_img_t *pri_encode_mfm_dd_300 (psi_img_t *img)
{
	pri_enc_mfm_t par;

	pri_encode_mfm_init (&par, 500000, 300);

	return (pri_encode_mfm (img, &par));
}

pri_img_t *pri_encode_mfm_hd_300 (psi_img_t *img)
{
	pri_enc_mfm_t par;

	pri_encode_mfm_init (&par, 1000000, 300);

	return (pri_encode_mfm (img, &par));
}

pri_img_t *pri_encode_mfm_hd_360 (psi_img_t *img)
{
	pri_enc_mfm_t par;

	pri_encode_mfm_init (&par, 1000000, 360);

	return (pri_encode_mfm (img, &par));
}
