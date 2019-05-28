/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/macplus/msg.c                                       *
 * Created:     2007-12-04 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2007-2019 Hampa Hug <hampa@hampa.ch>                     *
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
#include "macplus.h"
#include "msg.h"

#include <string.h>

#include <lib/console.h>
#include <lib/inidsk.h>
#include <lib/log.h>
#include <lib/monitor.h>
#include <lib/msg.h>
#include <lib/sysdep.h>


extern monitor_t par_mon;


typedef struct {
	const char *msg;

	int (*set_msg) (macplus_t *sim, const char *msg, const char *val);
} mac_msg_list_t;


static
int mac_set_msg_emu_cpu_model (macplus_t *sim, const char *msg, const char *val)
{
	if (mac_set_cpu_model (sim, val)) {
		pce_log (MSG_ERR, "unknown CPU model (%s)\n", val);
		return (1);
	}

	return (0);
}

static
int mac_set_msg_emu_cpu_speed (macplus_t *sim, const char *msg, const char *val)
{
	unsigned f;

	if (msg_get_uint (val, &f)) {
		return (1);
	}

	mac_set_speed (sim, PCE_MAC_SPEED_USER, f);

	return (0);
}

static
int mac_set_msg_emu_cpu_speed_step (macplus_t *sim, const char *msg, const char *val)
{
	int v;

	if (msg_get_sint (val, &v)) {
		return (1);
	}

	v += (int) sim->speed_factor;

	if (v <= 0) {
		v = 1;
	}

	mac_set_speed (sim, PCE_MAC_SPEED_USER, v);

	return (0);
}

static
int mac_set_msg_emu_disk_commit (macplus_t *sim, const char *msg, const char *val)
{
	int      r;
	unsigned drv;

	if (strcmp (val, "all") == 0) {
		pce_log (MSG_INF, "commiting all drives\n");

		if (dsks_commit (sim->dsks)) {
			pce_log (MSG_ERR,
				"*** commit failed for at least one disk\n"
			);
			return (1);
		}

		return (0);
	}

	r = 0;

	while (*val != 0) {
		if (msg_get_prefix_uint (&val, &drv, ":", " \t")) {
			pce_log (MSG_ERR, "*** commit error: bad drive (%s)\n",
				val
			);

			return (1);
		}

		pce_log (MSG_INF, "commiting drive %u\n", drv);

		if (dsks_set_msg (sim->dsks, drv, "commit", NULL)) {
			pce_log (MSG_ERR, "*** commit error for drive %u\n",
				drv
			);

			r = 1;
		}
	}

	return (r);
}

static
int mac_set_msg_emu_disk_eject (macplus_t *sim, const char *msg, const char *val)
{
	unsigned drv;
	disk_t   *dsk;

	while (*val != 0) {
		if (msg_get_prefix_uint (&val, &drv, ":", " \t")) {
			pce_log (MSG_ERR,
				"*** disk eject error: bad drive (%s)\n",
				val
			);

			return (1);
		}

		dsk = dsks_get_disk (sim->dsks, drv);

		if (dsk == NULL) {
			pce_log (MSG_ERR,
				"*** disk eject error: no such disk (%lu)\n", drv
			);
		}
		else {
			pce_log (MSG_INF, "ejecting drive %lu\n", drv);

			dsks_rmv_disk (sim->dsks, dsk);

			dsk_del (dsk);
		}
	}

	return (0);
}

static
int mac_set_msg_emu_disk_insert (macplus_t *sim, const char *msg, const char *val)
{
	if (dsk_insert (sim->dsks, val, 1)) {
		return (1);
	}

	return (0);
}

static
int mac_set_msg_emu_disk_ro (macplus_t *sim, const char *msg, const char *val)
{
	unsigned drv;
	disk_t   *dsk;

	if (msg_get_uint (val, &drv)) {
		return (1);
	}

	dsk = dsks_get_disk (sim->dsks, drv);

	if (dsk != NULL) {
		pce_log (MSG_INF, "setting readonly drive %lu\n", drv);

		dsk_set_readonly (dsk, 1);
	}

	return (0);
}

static
int mac_set_msg_emu_disk_rw (macplus_t *sim, const char *msg, const char *val)
{
	unsigned drv;
	disk_t   *dsk;

	if (msg_get_uint (val, &drv)) {
		return (1);
	}

	dsk = dsks_get_disk (sim->dsks, drv);

	if (dsk != NULL) {
		pce_log (MSG_INF, "setting read/write drive %lu\n", drv);

		dsk_set_readonly (dsk, 0);
	}

	return (0);
}

static
int mac_set_msg_emu_exit (macplus_t *sim, const char *msg, const char *val)
{
	sim->brk = PCE_BRK_ABORT;

	mon_set_terminate (&par_mon, 1);

	return (0);
}

static
int mac_set_msg_emu_iwm_insert (macplus_t *sim, const char *msg, const char *val)
{
	unsigned drv;

	if (msg_get_prefix_uint (&val, &drv, ":", " \t")) {
		pce_log (MSG_ERR, "*** insert error: bad drive (%s)\n",
			val
		);

		return (1);
	}

	if (drv == 0) {
		pce_log (MSG_ERR, "*** insert error: bad drive (%u)\n",
			drv
		);

		return (1);
	}

	mac_iwm_set_fname (&sim->iwm, drv - 1, val);

	return (0);
}

static
int mac_set_msg_emu_iwm_ro (macplus_t *sim, const char *msg, const char *val)
{
	unsigned drv;

	if (msg_get_uint (val, &drv)) {
		return (1);
	}

	if (drv == 0) {
		pce_log (MSG_INF, "setting all iwm drives to read-only\n");
		mac_iwm_set_locked (&sim->iwm, 0, 1);
		mac_iwm_set_locked (&sim->iwm, 1, 1);
		mac_iwm_set_locked (&sim->iwm, 2, 1);
	}
	else {
		pce_log (MSG_INF, "setting iwm drive %lu to read-only\n", drv);
		mac_iwm_set_locked (&sim->iwm, drv - 1, 1);
	}

	return (0);
}

static
int mac_set_msg_emu_iwm_rw (macplus_t *sim, const char *msg, const char *val)
{
	unsigned drv;

	if (msg_get_uint (val, &drv)) {
		return (1);
	}

	if (drv == 0) {
		pce_log (MSG_INF, "setting all iwm drives to read/write\n");
		mac_iwm_set_locked (&sim->iwm, 0, 0);
		mac_iwm_set_locked (&sim->iwm, 1, 0);
		mac_iwm_set_locked (&sim->iwm, 2, 0);
	}
	else {
		pce_log (MSG_INF, "setting iwm drive %lu to read/write\n", drv);
		mac_iwm_set_locked (&sim->iwm, drv - 1, 0);
	}

	return (0);
}

static
int mac_set_msg_emu_iwm_status (macplus_t *sim, const char *msg, const char *val)
{
	unsigned i;

	for (i = 0; i < 3; i++) {
		pce_printf ("IWM drive %u: locked=%d\n", i + 1, mac_iwm_get_locked (&sim->iwm, i));
	}

	return (0);
}

static
int mac_set_msg_emu_mac_insert (macplus_t *sim, const char *msg, const char *val)
{
	unsigned drv;

	if (strcmp (val, "") == 0) {
		if (sim->sony.enable) {
			mac_sony_insert (&sim->sony, 1);
			mac_sony_insert (&sim->sony, 2);
			mac_sony_insert (&sim->sony, 3);
		}
	}
	else {
		if (msg_get_uint (val, &drv)) {
			return (1);
		}

		if (sim->sony.enable) {
			mac_sony_insert (&sim->sony, drv);
		}
		else {
			mac_iwm_insert (&sim->iwm, (drv > 0) ? (drv - 1) : 0);
		}
	}

	return (0);
}

static
int mac_set_msg_emu_pause (macplus_t *sim, const char *msg, const char *val)
{
	int v;

	if (msg_get_bool (val, &v)) {
		return (1);
	}

	mac_set_pause (sim, v);

	return (0);
}

static
int mac_set_msg_emu_pause_toggle (macplus_t *sim, const char *msg, const char *val)
{
	mac_set_pause (sim, !sim->pause);

	return (0);
}

static
int mac_set_msg_emu_realtime (macplus_t *sim, const char *msg, const char *val)
{
	int v;

	if (msg_get_bool (val, &v)) {
		return (1);
	}

	mac_set_speed (sim, PCE_MAC_SPEED_USER, v ? 1 : 0);

	return (0);
}

static
int mac_set_msg_emu_realtime_toggle (macplus_t *sim, const char *msg, const char *val)
{
	if (sim->speed_limit[0] > 0) {
		mac_set_speed (sim, PCE_MAC_SPEED_USER, 0);
	}
	else {
		mac_set_speed (sim, PCE_MAC_SPEED_USER, 1);
	}

	return (0);
}

static
int mac_set_msg_emu_reset (macplus_t *sim, const char *msg, const char *val)
{
	mac_reset (sim);

	return (0);
}

static
int mac_set_msg_emu_ser1_driver (macplus_t *sim, const char *msg, const char *val)
{
	if (mac_ser_set_driver (&sim->ser[0], val)) {
		return (1);
	}

	return (0);
}

static
int mac_set_msg_emu_ser1_file (macplus_t *sim, const char *msg, const char *val)
{
	if (mac_ser_set_file (&sim->ser[0], val)) {
		return (1);
	}

	return (0);
}

static
int mac_set_msg_emu_ser1_multi (macplus_t *sim, const char *msg, const char *val)
{
	unsigned v;

	if (msg_get_uint (val, &v)) {
		return (1);
	}

	e8530_set_multichar (&sim->scc, 0, v, v);

	return (0);
}

static
int mac_set_msg_emu_ser2_driver (macplus_t *sim, const char *msg, const char *val)
{
	if (mac_ser_set_driver (&sim->ser[1], val)) {
		return (1);
	}

	return (0);
}

static
int mac_set_msg_emu_ser2_file (macplus_t *sim, const char *msg, const char *val)
{
	if (mac_ser_set_file (&sim->ser[1], val)) {
		return (1);
	}

	return (0);
}

static
int mac_set_msg_emu_ser2_multi (macplus_t *sim, const char *msg, const char *val)
{
	unsigned v;

	if (msg_get_uint (val, &v)) {
		return (1);
	}

	e8530_set_multichar (&sim->scc, 1, v, v);

	return (0);
}

static
int mac_set_msg_emu_stop (macplus_t *sim, const char *msg, const char *val)
{
	mac_set_msg_trm (sim, "term.release", "1");

	sim->brk = PCE_BRK_STOP;

	return (0);
}

static
int mac_set_msg_emu_video_brightness (macplus_t *sim, const char *msg, const char *val)
{
	unsigned br;

	if (msg_get_uint (val, &br)) {
		return (1);
	}

	if (br >= 1000) {
		br = 255;
	}
	else {
		br = (256UL * br) / 1000;
	}

	mac_video_set_brightness (sim->video, br);

	return (0);
}


static mac_msg_list_t set_msg_list[] = {
	{ "emu.cpu.model", mac_set_msg_emu_cpu_model },
	{ "emu.cpu.speed", mac_set_msg_emu_cpu_speed },
	{ "emu.cpu.speed.step", mac_set_msg_emu_cpu_speed_step },
	{ "emu.disk.commit", mac_set_msg_emu_disk_commit },
	{ "emu.disk.eject", mac_set_msg_emu_disk_eject },
	{ "emu.disk.insert", mac_set_msg_emu_disk_insert },
	{ "emu.disk.ro", mac_set_msg_emu_disk_ro },
	{ "emu.disk.rw", mac_set_msg_emu_disk_rw },
	{ "emu.exit", mac_set_msg_emu_exit },
	{ "emu.iwm.insert", mac_set_msg_emu_iwm_insert },
	{ "emu.iwm.ro", mac_set_msg_emu_iwm_ro },
	{ "emu.iwm.rw", mac_set_msg_emu_iwm_rw },
	{ "emu.iwm.status", mac_set_msg_emu_iwm_status },
	{ "emu.mac.insert", mac_set_msg_emu_mac_insert },
	{ "emu.pause", mac_set_msg_emu_pause },
	{ "emu.pause.toggle", mac_set_msg_emu_pause_toggle },
	{ "emu.realtime", mac_set_msg_emu_realtime },
	{ "emu.realtime.toggle", mac_set_msg_emu_realtime_toggle },
	{ "emu.reset", mac_set_msg_emu_reset },
	{ "emu.ser1.driver", mac_set_msg_emu_ser1_driver },
	{ "emu.ser1.file", mac_set_msg_emu_ser1_file },
	{ "emu.ser1.multi", mac_set_msg_emu_ser1_multi },
	{ "emu.ser2.driver", mac_set_msg_emu_ser2_driver },
	{ "emu.ser2.file", mac_set_msg_emu_ser2_file },
	{ "emu.ser2.multi", mac_set_msg_emu_ser2_multi },
	{ "emu.stop", mac_set_msg_emu_stop },
	{ "emu.video.brightness", mac_set_msg_emu_video_brightness },
	{ NULL, NULL }
};


int mac_set_msg (macplus_t *sim, const char *msg, const char *val)
{
	int            r;
	mac_msg_list_t *lst;

	/* a hack, for debugging only */
	if (sim == NULL) {
		sim = par_sim;
	}

	if (msg == NULL) {
		return (1);
	}

	if (val == NULL) {
		val = "";
	}

	lst = set_msg_list;

	while (lst->msg != NULL) {
		if (msg_is_message (lst->msg, msg)) {
			return (lst->set_msg (sim, msg, val));
		}

		lst += 1;
	}

	if (sim->trm != NULL) {
		r = trm_set_msg_trm (sim->trm, msg, val);

		if (r >= 0) {
			return (r);
		}
	}

	pce_log (MSG_INF, "unhandled message (\"%s\", \"%s\")\n", msg, val);

	return (1);
}
