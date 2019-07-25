/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/utils/pri/text.c                                         *
 * Created:     2014-08-18 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2014-2019 Hampa Hug <hampa@hampa.ch>                     *
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


#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <drivers/pri/pri.h>

#include "main.h"
#include "text.h"


void txt_save_pos (const pri_text_t *ctx, pri_text_pos_t *pos)
{
	pos->pos = ctx->trk->idx;
	pos->wrap = ctx->trk->wrap;
	pos->evt = ctx->trk->cur_evt;
}

void txt_restore_pos (pri_text_t *ctx, const pri_text_pos_t *pos)
{
	ctx->trk->idx = pos->pos;
	ctx->trk->wrap = pos->wrap;
	ctx->trk->cur_evt = pos->evt;
}

static
unsigned txt_guess_encoding (pri_trk_t *trk)
{
	unsigned long val;
	unsigned long bit;

	val = 0;

	pri_trk_set_pos (trk, 0);

	while (trk->wrap == 0) {
		pri_trk_get_bits (trk, &bit, 1);

		val = (val << 1) | (bit & 1);

		switch (val & 0xffff) {
		case 0x4489:
			return (PRI_TEXT_MFM);

		case 0xf57e:
		case 0xf56f:
		case 0xf56a:
			return (PRI_TEXT_FM);
		}

		if ((val & 0xffffffff) == 0xcff3fcff) {
			return (PRI_TEXT_MAC);
		}
	}

	return (PRI_TEXT_RAW);
}

int txt_dec_match (pri_text_t *ctx, const void *buf, unsigned cnt)
{
	unsigned            i;
	unsigned long       type, val;
	unsigned long       bit;
	const unsigned char *ptr;
	pri_text_pos_t      pos;

	txt_save_pos (ctx, &pos);

	ptr = buf;

	for (i = 0; i < cnt; i++) {
		if (pri_trk_get_event (ctx->trk, &type, &val) == 0) {
			break;
		}

		pri_trk_get_bits (ctx->trk, &bit, 1);

		if (((ptr[i >> 3] >> (~i & 7)) ^ bit) & 1) {
			break;
		}
	}

	if ((i < cnt) || ctx->trk->wrap) {
		txt_restore_pos (ctx, &pos);

		return (1);
	}

	return (0);
}

void txt_dec_bits (pri_text_t *ctx, unsigned cnt)
{
	unsigned      i;
	unsigned long val;

	val = (ctx->shift >> (ctx->shift_cnt - cnt)) & ((1UL << cnt) - 1);

	ctx->shift_cnt -= cnt;

	if (ctx->column > 0) {
		fputc ('\n', ctx->fp);
	}

	fprintf (ctx->fp, "RAW");

	for (i = 0; i < cnt; i++) {
		fprintf (ctx->fp, " %lu", (val >> (cnt - i - 1)) & 1);
	}

	fprintf (ctx->fp, "\n");

	ctx->column = 0;

	ctx->last_val = val & 1;
}

void txt_dec_event (pri_text_t *ctx, unsigned long type, unsigned long val)
{
	if (ctx->column > 0) {
		fputc ('\n', ctx->fp);
		ctx->column = 0;
		ctx->need_nl = 0;
	}

	if (type == PRI_EVENT_WEAK) {
		fprintf (ctx->fp, "WEAK %08lX\n", val);
	}
	else if (type == PRI_EVENT_CLOCK) {
		unsigned long long tmp;

		tmp = pri_trk_get_clock (ctx->trk);
		tmp = (tmp * val + 32768) / 65536;

		fprintf (ctx->fp, "CLOCK %lu\n", (unsigned long) tmp);
	}
	else {
		fprintf (ctx->fp, "EVENT %08lX %08lX\n", type, val);
	}
}

static
int pri_decode_text_auto_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	unsigned enc;

	pri_text_t ctx;

	ctx.fp = opaque;
	ctx.img = img;
	ctx.trk = trk;
	ctx.c = c;
	ctx.h = h;

	enc = txt_guess_encoding (trk);

	if (enc == PRI_TEXT_FM) {
		txt_fm_dec_track (&ctx);
	}
	else if (enc == PRI_TEXT_MFM) {
		txt_mfm_dec_track (&ctx);
	}
	else if (enc == PRI_TEXT_MAC) {
		txt_mac_dec_track (&ctx);
	}
	else {
		txt_raw_dec_track (&ctx);
	}

	return (0);
}

static
void txt_dec_init (pri_text_t *ctx, FILE *fp, pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h)
{
	memset (ctx, 0, sizeof (pri_text_t));

	ctx->fp = fp;
	ctx->img = img;
	ctx->trk = trk;
	ctx->c = c;
	ctx->h = h;
	ctx->first_track = 1;

	ctx->mac_no_slip = par_mac_no_slip;
}

static
int pri_decode_text_mfm_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	pri_text_t ctx;

	txt_dec_init (&ctx, opaque, img, trk, c, h);
	txt_mfm_dec_track (&ctx);

	return (0);
}

static
int pri_decode_text_fm_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	pri_text_t ctx;

	txt_dec_init (&ctx, opaque, img, trk, c, h);
	txt_fm_dec_track (&ctx);

	return (0);
}

static
int pri_decode_text_mac_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	pri_text_t ctx;

	txt_dec_init (&ctx, opaque, img, trk, c, h);
	txt_mac_dec_track (&ctx);

	return (0);
}

static
int pri_decode_text_raw_cb (pri_img_t *img, pri_trk_t *trk, unsigned long c, unsigned long h, void *opaque)
{
	pri_text_t ctx;

	txt_dec_init (&ctx, opaque, img, trk, c, h);
	txt_raw_dec_track (&ctx);

	return (0);
}

int pri_decode_text (pri_img_t *img, const char *fname, unsigned enc)
{
	int  r;
	FILE *fp;

	if ((fp = fopen (fname, "w")) == NULL) {
		return (1);
	}

	fprintf (fp, "PRI 0\n\n");

	if (enc == PRI_TEXT_AUTO) {
		r = pri_for_all_tracks (img, pri_decode_text_auto_cb, fp);
	}
	else if (enc == PRI_TEXT_MFM) {
		r = pri_for_all_tracks (img, pri_decode_text_mfm_cb, fp);
	}
	else if (enc == PRI_TEXT_FM) {
		r = pri_for_all_tracks (img, pri_decode_text_fm_cb, fp);
	}
	else if (enc == PRI_TEXT_MAC) {
		r = pri_for_all_tracks (img, pri_decode_text_mac_cb, fp);
	}
	else {
		r = pri_for_all_tracks (img, pri_decode_text_raw_cb, fp);
	}

	fclose (fp);

	return (r);
}


static
void txt_init (pri_text_t *ctx, FILE *fp)
{
	ctx->fp = fp;
	ctx->cnt = 0;
	ctx->line = 0;
}

static
int txt_getc (pri_text_t *ctx, unsigned idx)
{
	int      c;
	unsigned i;

	for (i = ctx->cnt; i <= idx; i++) {
		if ((c = fgetc (ctx->fp)) == EOF) {
			return (EOF);
		}

		ctx->buf[ctx->cnt++] = c;
	}

	return (ctx->buf[idx]);
}

int txt_error (pri_text_t *ctx, const char *str)
{
	int c;

	if (str == NULL) {
		str = "unknown";
	}

	c = txt_getc (ctx, 0);

	fprintf (stderr, "pri-text:%u: error (%s), next = %02X\n", ctx->line + 1, str, c);

	return (1);
}

static
void txt_skip (pri_text_t *ctx, unsigned cnt)
{
	unsigned i;

	if (cnt >= ctx->cnt) {
		ctx->cnt = 0;
	}
	else {
		for (i = cnt; i < ctx->cnt; i++) {
			ctx->buf[i - cnt] = ctx->buf[i];
		}

		ctx->cnt -= cnt;
	}
}

static
void txt_match_space (pri_text_t *ctx)
{
	int c, line;

	line = 0;

	while (1) {
		c = txt_getc (ctx, 0);

		if (c == EOF) {
			return;
		}

		if ((c == 0x0d) || (c == 0x0a)) {
			line = 0;
			ctx->line += 1;
		}
		else if (line) {
			;
		}
		else if ((c == '\t') || (c == ' ')) {
			;
		}
		else if (c == '#') {
			line = 1;
		}
		else {
			return;
		}

		txt_skip (ctx, 1);
	}
}

int txt_match_eol (pri_text_t *ctx)
{
	int c, line;

	line = 0;

	while (1) {
		c = txt_getc (ctx, 0);

		if (c == EOF) {
			return (1);
		}

		if ((c == 0x0d) || (c == 0x0a)) {
			ctx->line += 1;
			txt_skip (ctx, 1);

			return (1);
		}
		else if (line) {
			;
		}
		else if ((c == '\t') || (c == ' ')) {
			;
		}
		else if (c == '#') {
			line = 1;
		}
		else {
			return (0);
		}

		txt_skip (ctx, 1);
	}
}

int txt_match (pri_text_t *ctx, const char *str, int skip)
{
	int      c;
	unsigned i, n;

	txt_match_space (ctx);

	n = 0;

	while (str[n] != 0) {
		if (n >= ctx->cnt) {
			if ((c = fgetc (ctx->fp)) == EOF) {
				return (0);
			}

			ctx->buf[ctx->cnt++] = c;
		}

		if (ctx->buf[n] != str[n]) {
			return (0);
		}

		n += 1;
	}

	if (skip) {
		for (i = n; i < ctx->cnt; i++) {
			ctx->buf[i - n] = ctx->buf[i];
		}

		ctx->cnt -= n;
	}

	return (1);
}

int txt_match_uint (pri_text_t *ctx, unsigned base, unsigned long *val)
{
	unsigned i, dig;
	int      c, s, ok;

	txt_match_space (ctx);

	i = 0;
	s = 0;
	ok = 0;

	c = txt_getc (ctx, i);

	if ((c == '-') || (c == '+')) {
		s = (c == '-');

		c = txt_getc (ctx, ++i);
	}

	*val = 0;

	while (1) {
		if ((c >= '0') && (c <= '9')) {
			dig = c - '0';
		}
		else if ((c >= 'a') && (c <= 'z')) {
			dig = c - 'a' + 10;
		}
		else if ((c >= 'A') && (c <= 'Z')) {
			dig = c - 'A' + 10;
		}
		else {
			break;
		}

		if (dig >= base) {
			return (0);
		}

		*val = base * *val + dig;
		ok = 1;

		c = txt_getc (ctx, ++i);
	}

	if (ok == 0) {
		return (0);
	}

	if (s) {
		*val = ~*val + 1;
	}

	*val &= 0xffffffff;

	txt_skip (ctx, i);

	return (1);
}

int txt_enc_bits_raw (pri_text_t *ctx, unsigned long val, unsigned cnt)
{
	pri_trk_set_bits (ctx->trk, val, cnt);
	ctx->bit_cnt += cnt;

	if ((2 * ctx->bit_cnt) > ctx->bit_max) {
		ctx->bit_max *= 2;

		if (pri_trk_set_size (ctx->trk, ctx->bit_max)) {
			return (1);
		}

		pri_trk_set_pos (ctx->trk, ctx->bit_cnt);
	}

	ctx->last_val = val & 0xff;

	return (0);
}

static
int txt_enc_clock (pri_text_t *ctx)
{
	unsigned long val, old;

	if (txt_match_uint (ctx, 10, &val) == 0) {
		return (1);
	}

	if (val > 131072) {
		old = pri_trk_get_clock (ctx->trk);
		val = (65536ULL * val + (old / 2)) / old;
	}

	if (pri_trk_evt_add (ctx->trk, PRI_EVENT_CLOCK, ctx->bit_cnt, val) == NULL) {
		return (1);
	}

	return (0);
}

static
int txt_enc_index (pri_text_t *ctx)
{
	ctx->index_position = ctx->bit_cnt;

	return (0);
}

static
int txt_enc_mode (pri_text_t *ctx)
{
	if (txt_match (ctx, "RAW", 1)) {
		ctx->encoding = PRI_TEXT_RAW;
	}
	else if (txt_match (ctx, "IBM-MFM", 1)) {
		ctx->encoding = PRI_TEXT_MFM;
	}
	else if (txt_match (ctx, "IBM-FM", 1)) {
		ctx->encoding = PRI_TEXT_FM;
	}
	else if (txt_match (ctx, "MAC-GCR", 1)) {
		ctx->encoding = PRI_TEXT_MAC;
	}
	else if (txt_match (ctx, "MFM", 1)) {
		ctx->encoding = PRI_TEXT_MFM;
	}
	else if (txt_match (ctx, "FM", 1)) {
		ctx->encoding = PRI_TEXT_FM;
	}
	else {
		ctx->encoding = PRI_TEXT_RAW;
		return (1);
	}

	return (0);
}

static
int txt_enc_rate (pri_text_t *ctx)
{
	unsigned long clock;

	if (ctx->trk == NULL) {
		return (1);
	}

	if (txt_match_uint (ctx, 10, &clock) == 0) {
		return (1);
	}

	pri_trk_set_clock (ctx->trk, clock);

	return (0);
}

static
int txt_enc_raw (pri_text_t *ctx)
{
	unsigned long val;

	while (txt_match_eol (ctx) == 0) {
		if (txt_match_uint (ctx, 16, &val) == 0) {
			return (1);
		}

		if ((val != 0) && (val != 1)) {
			return (1);
		}

		if (txt_enc_bits_raw (ctx, val, 1)) {
			return (1);
		}
	}

	return (0);
}

static
int txt_enc_track_finish (pri_text_t *ctx)
{
	if (ctx->trk == NULL) {
		return (0);
	}

	if (pri_trk_set_size (ctx->trk, ctx->bit_cnt)) {
		return (1);
	}

	if (ctx->index_position != 0) {
		unsigned long cnt, max;

		max = pri_trk_get_size (ctx->trk);

		if (max > 0) {
			cnt = ctx->index_position % max;
			pri_trk_rotate (ctx->trk, cnt);
		}
	}

	ctx->trk = NULL;

	return (0);
}

static
int txt_enc_track (pri_text_t *ctx)
{
	unsigned long c, h;

	if (txt_match_uint (ctx, 10, &c) == 0) {
		return (1);
	}

	if (txt_match_uint (ctx, 10, &h) == 0) {
		return (1);
	}

	if ((c > 255) || (h > 255)) {
		txt_error (ctx, "c/h too large");
		return (1);
	}

	ctx->trk = pri_img_get_track (ctx->img, c, h, 1);

	if (ctx->trk == NULL) {
		return (1);
	}

	pri_trk_set_clock (ctx->trk, 500000);
	pri_trk_evt_del_all (ctx->trk, PRI_EVENT_ALL);

	ctx->c = c;
	ctx->h = h;

	ctx->bit_cnt = 0;
	ctx->bit_max = 65536;

	if (pri_trk_set_size (ctx->trk, ctx->bit_max)) {
		return (1);
	}

	pri_trk_set_pos (ctx->trk, 0);

	ctx->mac_check_active = 0;
	ctx->last_val = 0;
	ctx->crc = 0xffff;

	return (0);
}

static
int txt_enc_weak (pri_text_t *ctx)
{
	unsigned long val;

	if (txt_match_uint (ctx, 16, &val) == 0) {
		return (1);
	}

	if (pri_trk_evt_add (ctx->trk, PRI_EVENT_WEAK, ctx->bit_cnt, val) == NULL) {
		return (1);
	}

	return (0);
}

static
int txt_encode_pri0 (pri_text_t *ctx)
{
	int r;

	while (1) {
		switch (ctx->encoding) {
		case PRI_TEXT_MFM:
			r = txt_encode_pri0_mfm (ctx);
			break;

		case PRI_TEXT_FM:
			r = txt_encode_pri0_fm (ctx);
			break;

		case PRI_TEXT_MAC:
			r = txt_encode_pri0_mac (ctx);
			break;

		case PRI_TEXT_RAW:
			r = txt_encode_pri0_raw (ctx);
			break;

		default:
			r = -1;
			break;
		}

		if (r >= 0) {
			if (r > 0) {
				return (1);
			}

			continue;
		}

		if (txt_match (ctx, "CLOCK", 1)) {
			if (txt_enc_clock (ctx)) {
				txt_error (ctx, "clock");
				return (1);
			}
		}
		else if (txt_match (ctx, "INDEX", 1)) {
			if (txt_enc_index (ctx)) {
				return (1);
			}
		}
		else if (txt_match (ctx, "MODE", 1)) {
			if (txt_enc_mode (ctx)) {
				return (1);
			}
		}
		else if (txt_match (ctx, "PRI", 0)) {
			break;
		}
		else if (txt_match (ctx, "RATE", 1)) {
			if (txt_enc_rate (ctx)) {
				return (1);
			}
		}
		else if (txt_match (ctx, "RAW", 1)) {
			if (txt_enc_raw (ctx)) {
				return (1);
			}
		}
		else if (txt_match (ctx, "TRACK", 1)) {
			if (txt_enc_track_finish (ctx)) {
				return (1);
			}

			if (txt_enc_track (ctx)) {
				return (1);
			}
		}
		else if (txt_match (ctx, "WEAK", 1) || txt_match (ctx, "FUZZY", 1)) {
			if (txt_enc_weak (ctx)) {
				return (1);
			}
		}
		else if (feof (ctx->fp)) {
			break;
		}
		else {
			return (1);
		}
	}

	if (txt_enc_track_finish (ctx)) {
		return (1);
	}

	return (0);
}

static
int txt_encode (pri_text_t *ctx)
{
	unsigned long val;

	ctx->trk = NULL;
	ctx->bit_cnt = 0;
	ctx->bit_max = 0;
	ctx->last_val = 0;
	ctx->encoding = PRI_TEXT_RAW;
	ctx->index_position = 0;
	ctx->crc = 0xffff;

	ctx->mac_check_active = 0;
	ctx->mac_gap_size = 6;

	while (1) {
		if (txt_match (ctx, "PRI", 1)) {
			if (txt_match_uint (ctx, 10, &val) == 0) {
				return (1);
			}

			if (val == 0) {
				if (txt_encode_pri0 (ctx)) {
					return (txt_error (ctx, "PRI"));
				}
			}
			else {
				return (txt_error (ctx, "bad pri version"));
			}
		}
		else if (feof (ctx->fp) == 0) {
			return (txt_error (ctx, "no pri header"));
		}
		else {
			break;
		}
	}

	if (txt_enc_track_finish (ctx)) {
		return (1);
	}

	return (0);
}

int pri_encode_text (pri_img_t *img, const char *fname)
{
	int        r;
	pri_text_t ctx;

	if ((ctx.fp = fopen (fname, "r")) == NULL) {
		return (1);
	}

	txt_init (&ctx, ctx.fp);

	ctx.img = img;

	r = txt_encode (&ctx);

	fclose (ctx.fp);

	return (r);
}
