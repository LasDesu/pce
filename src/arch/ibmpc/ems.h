/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:     src/ibmpc/ems.h                                            *
 * Created:       2003-10-18 by Hampa Hug <hampa@hampa.ch>                   *
 * Last modified: 2003-10-18 by Hampa Hug <hampa@hampa.ch>                   *
 * Copyright:     (C) 2003 by Hampa Hug <hampa@hampa.ch>                     *
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

/* $Id: ems.h,v 1.1 2003/12/20 01:01:33 hampa Exp $ */


#ifndef PCE_EMS_H
#define PCE_EMS_H 1


typedef struct ems_block_t {
  unsigned short     handle;
  unsigned           pages;
  unsigned char      *data;

  int                map_saved;
  struct ems_block_t *map_blk[4];
  unsigned           map_page[4];
} ems_block_t;


typedef struct {
  ems_block_t   *blk[256];

  unsigned      pages_max;
  unsigned      pages_used;

  ems_block_t   *map_blk[4];
  unsigned      map_page[4];

  mem_blk_t     *mem;
} ems_t;


ems_t *ems_new (ini_sct_t *sct);

void ems_del (ems_t *ems);

mem_blk_t *ems_get_mem (ems_t *ems);

void ems_prt_state (ems_t *xms, FILE *fp);

void ems_info (ems_t *ems, e8086_t *cpu);

unsigned char ems_get_uint8 (ems_t *ems, unsigned long addr);
void ems_set_uint8 (ems_t *ems, unsigned long addr, unsigned char val);
unsigned short ems_get_uint16 (ems_t *ems, unsigned long addr);
void ems_set_uint16 (ems_t *ems, unsigned long addr, unsigned short val);

void ems_handler (ems_t *ems, e8086_t *cpu);


#endif