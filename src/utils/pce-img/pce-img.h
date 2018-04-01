/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/utils/pce-img/pce-img.h                                  *
 * Created:     2005-11-29 by Hampa Hug <hampa@hampa.ch>                     *
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


#ifndef PCE_IMG_H
#define PCE_IMG_H 1


#include <config.h>

#include <drivers/block/block.h>


#define DSK_NONE   0
#define DSK_RAW    1
#define DSK_PCE    2
#define DSK_DOSEMU 3
#define DSK_PSI    4
#define DSK_QED    5
#define DSK_PBI    6


extern const char *arg0;
extern char       par_quiet;


void print_version (void);

void pce_set_quiet (int val);
int pce_set_n (const char *str);
int pce_set_c (const char *str);
int pce_set_h (const char *str);
int pce_set_s (const char *str);
int pce_set_geo (const char *c, const char *h, const char *s);
int pce_set_ofs (const char *str);
int pce_set_min_cluster_size (const char *str);

int pce_set_type_inp (const char *str);
int pce_set_type_out (const char *str);

unsigned char *pce_get_buf (unsigned long size);

int pce_block_is_null (const void *buf, unsigned cnt);
int pce_block_is_uniform_32 (const void *buf, unsigned cnt, unsigned long *val);

void pce_set_disk_parameters (disk_t *dsk);

int dsk_create (const char *name, unsigned type);
disk_t *dsk_open_inp (const char *name, disk_t *dsk, int ro);
disk_t *dsk_open_out (const char *name, disk_t *dsk, int create);
disk_t *dsk_cow_create (const char *name, disk_t *dsk);
disk_t *dsk_cow (const char *name, disk_t *dsk);

int main_commit (int argc, char **argv);
int main_convert (int argc, char **argv);
int main_cow (int argc, char **argv);
int main_create (int argc, char **argv);
int main_rebase (int argc, char **argv);


#endif
