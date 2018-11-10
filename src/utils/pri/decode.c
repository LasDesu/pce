/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/utils/pri/decode.c                                       *
 * Created:     2013-12-19 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2013-2018 Hampa Hug <hampa@hampa.ch>                     *
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
#include <string.h>

#include <drivers/psi/psi-img.h>
#include <drivers/psi/psi.h>

#include <drivers/pri/pri.h>
#include <drivers/pri/pri-img.h>
#include <drivers/pri/pri-enc-fm.h>
#include <drivers/pri/pri-enc-gcr.h>
#include <drivers/pri/pri-enc-mfm.h>


struct pri_decode_psi_s {
	psi_img_t     *img;
	pri_dec_mfm_t mfm;
};


extern pri_dec_mfm_t par_dec_mfm;


static
int pri_decode_fm_raw_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	FILE          *fp;
	unsigned      outbuf, outcnt;
	unsigned      val, clk;
	unsigned long bit;

	fp = opaque;

	pri_trk_set_pos (trk, 0);

	val = 0;
	clk = 0xffff;

	outbuf = 0;
	outcnt = 0;

	while (trk->wrap == 0) {
		pri_trk_get_bits (trk, &bit, 1);

		val = ((val << 1) | (bit & 1)) & 0xffff;
		clk = (clk << 1) | (~clk & val & 1);

		if ((val == 0xf57e) || (val == 0xf56f) || (val == 0xf56a)) {
			clk = 0xaaaa;

			if (outcnt > 0) {
				outbuf = outbuf << (8 - outcnt);
				outcnt = 8;
			}
		}
		else if ((clk & 0x8000) == 0) {
			outbuf = (outbuf << 1) | ((val >> 15) & 1);
			outcnt += 1;
		}

		if (outcnt >= 8) {
			fputc (outbuf & 0xff, fp);
			outbuf = 0;
			outcnt = 0;
		}
	}

	return (0);
}

static
int pri_decode_fm_raw (pri_img_t *img, const char *fname)
{
	int  r;
	FILE *fp;

	if ((fp = fopen (fname, "wb")) == NULL) {
		return (1);
	}

	r = pri_for_all_tracks (img, pri_decode_fm_raw_cb, fp);

	fclose (fp);

	return (r);
}


static
int pri_decode_gcr_raw_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	FILE          *fp;
	unsigned      val;
	unsigned long bit;

	fp = opaque;

	pri_trk_set_pos (trk, 0);

	val = 0;

	while (trk->wrap == 0) {
		pri_trk_get_bits (trk, &bit, 1);

		val = (val << 1) | (bit & 1);

		if (val & 0x80) {
			fputc (val, fp);
			val = 0;
		}
	}

	return (0);
}

static
int pri_decode_gcr_raw (pri_img_t *img, const char *fname)
{
	int  r;
	FILE *fp;

	fp = fopen (fname, "wb");

	if (fp == NULL) {
		return (1);
	}

	r = pri_for_all_tracks (img, pri_decode_gcr_raw_cb, fp);

	fclose (fp);

	return (r);
}


static
int pri_decode_mfm_raw_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	FILE          *fp;
	unsigned      outbuf, outcnt;
	unsigned      val, clk;
	unsigned long bit;

	fp = opaque;

	pri_trk_set_pos (trk, 0);

	val = 0;
	clk = 0;

	outbuf = 0;
	outcnt = 0;

	while (trk->wrap == 0) {
		pri_trk_get_bits (trk, &bit, 1);

		val = (val << 1) | (bit & 1);
		clk = (clk << 1) | (~clk & 1);

		if ((clk & 1) == 0) {
			outbuf = (outbuf << 1) | (val & 1);
			outcnt += 1;
		}

		if ((val & 0xffff) == 0x4489) {
			outbuf = 0xa1;
			outcnt = 8;
			clk = 0;
		}

		if (outcnt >= 8) {
			fputc (outbuf & 0xff, fp);
			outbuf = 0;
			outcnt = 0;
		}
	}

	return (0);
}

static
int pri_decode_mfm_raw (pri_img_t *img, const char *fname)
{
	int  r;
	FILE *fp;

	fp = fopen (fname, "wb");

	if (fp == NULL) {
		return (1);
	}

	r = pri_for_all_tracks (img, pri_decode_mfm_raw_cb, fp);

	fclose (fp);

	return (r);
}


static
int pri_decode_raw_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	FILE *fp;

	fp = opaque;

	if (fwrite (trk->data, (trk->size + 7) / 8, 1, fp) != 1) {
		return (1);
	}

	return (0);
}

int pri_decode_raw (pri_img_t *img, const char *fname)
{
	int  r;
	FILE *fp;

	fp = fopen (fname, "wb");

	if (fp == NULL) {
		return (1);
	}

	r = pri_for_all_tracks (img, pri_decode_raw_cb, fp);

	fclose (fp);

	return (r);
}


static
int pri_decode_psi_auto_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	psi_trk_t               *dtrk;
	struct pri_decode_psi_s *dec;

	dec = opaque;

	if ((dtrk = pri_decode_mfm_trk (trk, h, &dec->mfm)) == NULL) {
		return (1);
	}

	if (dtrk->sct_cnt == 0) {
		psi_trk_del (dtrk);

		if ((dtrk = pri_decode_fm_trk (trk, h)) == NULL) {
			return (1);
		}
	}

	if (dtrk->sct_cnt == 0) {
		psi_trk_del (dtrk);

		if ((dtrk = pri_decode_gcr_trk (trk, h)) == NULL) {
			return (1);
		}
	}

	if (psi_img_add_track (dec->img, dtrk, c)) {
		psi_trk_del (dtrk);
		return (1);
	}

	return (0);
}

static
int pri_decode_psi_mfm_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	psi_trk_t               *dtrk;
	struct pri_decode_psi_s *dec;

	dec = opaque;

	if ((dtrk = pri_decode_mfm_trk (trk, h, &dec->mfm)) == NULL) {
		return (1);
	}

	if (psi_img_add_track (dec->img, dtrk, c)) {
		psi_trk_del (dtrk);
		return (1);
	}

	return (0);
}

static
int pri_decode_psi_fm_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	psi_trk_t               *dtrk;
	struct pri_decode_psi_s *dec;

	dec = opaque;

	if ((dtrk = pri_decode_fm_trk (trk, h)) == NULL) {
		return (1);
	}

	if (psi_img_add_track (dec->img, dtrk, c)) {
		psi_trk_del (dtrk);
		return (1);
	}

	return (0);
}

static
int pri_decode_psi_gcr_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	psi_trk_t               *dtrk;
	struct pri_decode_psi_s *dec;

	dec = opaque;

	if ((dtrk = pri_decode_gcr_trk (trk, h)) == NULL) {
		return (1);
	}

	if (psi_img_add_track (dec->img, dtrk, c)) {
		psi_trk_del (dtrk);
		return (1);
	}

	return (0);
}

static
int pri_decode_psi (pri_img_t *img, const char *type, const char *fname)
{
	int                     r;
	pri_trk_cb              fct;
	struct pri_decode_psi_s dec;

	dec.mfm = par_dec_mfm;

	if (strcmp2 (type, "auto", "mfm-fm") == 0) {
		fct = pri_decode_psi_auto_cb;
	}
	else if (strcmp2 (type, "ibm-fm", "fm") == 0) {
		fct = pri_decode_psi_fm_cb;
	}
	else if (strcmp2 (type, "mac-gcr", "gcr") == 0) {
		fct = pri_decode_psi_gcr_cb;
	}
	else if (strcmp2 (type, "ibm-mfm", "mfm") == 0) {
		fct = pri_decode_psi_mfm_cb;
	}
	else {
		fprintf (stderr, "%s: unknown decode type (%s)\n", arg0, type);
		return (1);
	}

	if ((dec.img = psi_img_new()) == NULL) {
		return (1);
	}

	if (pri_for_all_tracks (img, fct, &dec)) {
		psi_img_del (dec.img);
		return (1);
	}

	if (img->comment_size > 0) {
		psi_img_set_comment (dec.img, img->comment, img->comment_size);
	}

	r = psi_save (fname, dec.img, PSI_FORMAT_NONE);

	psi_img_del (dec.img);

	return (r);
}


int pri_decode (pri_img_t *img, const char *type, const char *fname)
{
	if (strcmp (type, "fm-raw") == 0) {
		return (pri_decode_fm_raw (img, fname));
	}
	else if (strcmp (type, "gcr-raw") == 0) {
		return (pri_decode_gcr_raw (img, fname));
	}
	else if (strcmp (type, "mfm-raw") == 0) {
		return (pri_decode_mfm_raw (img, fname));
	}
	else if (strcmp (type, "text") == 0) {
		return (pri_decode_text (img, fname, PRI_TEXT_AUTO));
	}
	else if (strcmp (type, "text-fm") == 0) {
		return (pri_decode_text (img, fname, PRI_TEXT_FM));
	}
	else if (strcmp (type, "text-mac") == 0) {
		return (pri_decode_text (img, fname, PRI_TEXT_MAC));
	}
	else if (strcmp (type, "text-mfm") == 0) {
		return (pri_decode_text (img, fname, PRI_TEXT_MFM));
	}
	else if (strcmp (type, "text-raw") == 0) {
		return (pri_decode_text (img, fname, PRI_TEXT_RAW));
	}
	else if (strcmp (type, "raw") == 0) {
		return (pri_decode_raw (img, fname));
	}
	else {
		return (pri_decode_psi (img, type, fname));
	}
}
