/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/sim405/sercons.c                                    *
 * Created:     2004-06-01 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2004-2018 Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2004-2006 Lukas Ruf <ruf@lpr.ch>                         *
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

/*****************************************************************************
 * This software was developed at the Computer Engineering and Networks      *
 * Laboratory (TIK), Swiss Federal Institute of Technology (ETH) Zurich.     *
 *****************************************************************************/


#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include "msg.h"
#include "sim405.h"

#include <lib/sysdep.h>


void scon_check (sim405_t *sim)
{
	unsigned      i, n;
	unsigned char buf[8];
	ssize_t       r;

	if (par_sig_int) {
		par_sig_int = 0;
		s405_set_keycode (sim, 0x03);
	}

	if (!pce_fd_readable (0, 0)) {
		return;
	}

	r = read (0, buf, 8);
	if (r <= 0) {
		return;
	}

	n = (unsigned) r;

	if ((n == 1) && (buf[0] == 0)) {
		s405_set_msg (sim, "emu.stop", "");
		return;
	}
#if 0
	if ((n == 1) && (buf[0] == 0x1b)) {
		s405_set_msg (sim, "emu.stop", "");
		return;
	}
#endif
	else if ((n == 1) && (buf[0] == 0xe0)) {
		s405_set_msg (sim, "emu.abort", "");
		return;
	}

	for (i = 0; i < n; i++) {
		s405_set_keycode (sim, buf[i]);
	}
}
