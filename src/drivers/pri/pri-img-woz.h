/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/drivers/pri/pri-img-woz.h                                *
 * Created:     2019-06-19 by Hampa Hug <hampa@hampa.ch>                     *
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


#ifndef PCE_PRI_IMG_WOZ_H
#define PCE_PRI_IMG_WOZ_H 1


#include <drivers/pri/pri.h>


pri_img_t *pri_load_woz (FILE *fp);

int pri_save_woz (FILE *fp, const pri_img_t *img);

int pri_probe_woz_fp (FILE *fp);
int pri_probe_woz (const char *fname);


#endif
