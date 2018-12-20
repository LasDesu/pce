/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/chipset/82xx/e8272.h                                     *
 * Created:     2005-03-06 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2005-2018 Hampa Hug <hampa@hampa.ch>                     *
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


#ifndef PCE_E8272_H
#define PCE_E8272_H 1


#define E8272_ERR_OK       0x00
#define E8272_ERR_OTHER    0x01
#define E8272_ERR_NO_ID    0x02
#define E8272_ERR_NO_DATA  0x04
#define E8272_ERR_CRC_ID   0x08
#define E8272_ERR_CRC_DATA 0x10
#define E8272_ERR_DEL_DAM  0x20
#define E8272_ERR_DATALEN  0x40
#define E8272_ERR_WPROT    0x80

#define E8272_MAX_SCT 128

#define E8272_DISKOP_READ   0
#define E8272_DISKOP_WRITE  1
#define E8272_DISKOP_FORMAT 3
#define E8272_DISKOP_READID 4


typedef struct {
	unsigned char  c;
	unsigned char  h;
	unsigned char  s;
	unsigned short n;
	unsigned short ofs;
} e8272_sct_t;


typedef struct {
	unsigned      d;
	unsigned      c;
	unsigned      h;

	char          ok;
	unsigned      sct_cnt;
	e8272_sct_t   sct[E8272_MAX_SCT];
} e8272_drive_t;


typedef struct {
	unsigned char pd;
	unsigned char pc;
	unsigned char ph;
	unsigned char ps;

	unsigned char lc;
	unsigned char lh;
	unsigned char ls;
	unsigned char ln;

	void          *buf;
	unsigned      cnt;

	unsigned char fill;
} e8272_diskop_t;


typedef struct e8272_t {
	unsigned char  dor;
	unsigned char  msr;

	unsigned char  st[4];

	unsigned char  drvmsk;

	e8272_drive_t  drv[4];
	e8272_drive_t  *curdrv;

	unsigned       cmd_i;
	unsigned       cmd_n;
	unsigned char  cmd[16];

	unsigned       res_i;
	unsigned       res_n;
	unsigned char  res[16];

	unsigned       buf_i;
	unsigned       buf_n;
	unsigned char  buf[8192];

	char           dma;

	unsigned char  ready_change;

	unsigned short step_rate;

	char           accurate;
	char           ignore_eot;

	unsigned long  delay_clock;

	unsigned long  input_clock;

	unsigned long  track_pos;
	unsigned long  track_clk;
	unsigned long  track_size;

	unsigned short index_cnt;

	char           read_error;
	char           read_deleted;
	unsigned short read_track_cnt;
	unsigned short write_id;
	unsigned short format_cnt;

	void           (*set_data) (struct e8272_t *fdc, unsigned char val);
	unsigned char  (*get_data) (struct e8272_t *fdc);
	void           (*set_tc) (struct e8272_t *fdc);
	void           (*set_clock) (struct e8272_t *fdc, unsigned long cnt);
	void           (*start_cmd) (struct e8272_t *fdc);

	void           *diskop_ext;
	unsigned       (*diskop) (void *ext, unsigned op, e8272_diskop_t *p);

	/* the interrupt function */
	void           *irq_ext;
	unsigned char  irq_val;
	void           (*irq) (void *ext, unsigned char val);

	void           *dreq_ext;
	unsigned char  dreq_val;
	void           (*dreq) (void *ext, unsigned char val);
} e8272_t;


void e8272_init (e8272_t *fdc);

e8272_t *e8272_new (void);

void e8272_free (e8272_t *fdc);

void e8272_del (e8272_t *fdc);


void e8272_set_irq_fct (e8272_t *fdc, void *ext, void *fct);

void e8272_set_dreq_fct (e8272_t *fdc, void *ext, void *fct);


void e8272_set_diskop_fct (e8272_t *fdc, void *ext, void *fct);


void e8272_set_input_clock (e8272_t *fdc, unsigned long clk);

void e8272_set_accuracy (e8272_t *fdc, int accurate);

void e8272_set_ignore_eot (e8272_t *fdc, int ignore_eot);

void e8272_set_drive_mask (e8272_t *fdc, unsigned mask);


void e8272_write_data (e8272_t *fdc, unsigned char val);

unsigned char e8272_read_data (e8272_t *fdc);


unsigned char e8272_get_uint8 (e8272_t *fdc, unsigned long addr);

void e8272_set_uint8 (e8272_t *fdc, unsigned long addr, unsigned char val);


void e8272_reset (e8272_t *fdc);

void e8272_set_tc (e8272_t *fdc, unsigned char val);

void e8272_clock (e8272_t *fdc, unsigned long n);


#endif
