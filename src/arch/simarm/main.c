/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/simarm/main.c                                       *
 * Created:     2004-11-04 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2004-2019 Hampa Hug <hampa@hampa.ch>                     *
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
#include "cmd_arm.h"
#include "simarm.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <signal.h>

#include <lib/cfg.h>
#include <lib/console.h>
#include <lib/log.h>
#include <lib/monitor.h>
#include <lib/sysdep.h>


char      *par_cpu = NULL;

unsigned  par_xlat = ARM_XLAT_CPU;

simarm_t  *par_sim = NULL;

unsigned  par_sig_int = 0;

ini_sct_t *par_cfg = NULL;

static monitor_t par_mon;


static
void prt_help (void)
{
	fputs (
		"usage: pce-simarm [options]\n"
		"  -c, --config string    Set the config file\n"
		"  -l, --log string       Set the log file\n"
		"  -p, --cpu string       Set the cpu model\n"
		"  -q, --quiet            Quiet operation [no]\n"
		"  -r, --run              Start running immediately\n"
		"  -v, --verbose          Verbose operation [no]\n",
		stdout
	);

	fflush (stdout);
}

static
void prt_version (void)
{
	fputs (
		"pce-simarm version " PCE_VERSION_STR
		"\n\n"
		"Copyright (C) 1995-2019 Hampa Hug <hampa@hampa.ch>\n",
		stdout
	);

	fflush (stdout);
}

static
void sig_int (int s)
{
	signal (s, sig_int);

	par_sig_int = 1;
}

static
void sig_terminate (int s)
{
	fprintf (stderr, "pce-simarm: signal %d\n", s);

	if ((par_sim != NULL) && (par_sim->cpu != NULL)) {
		sarm_prt_state_cpu (par_sim->cpu, stderr);
	}

	fflush (stderr);

	pce_set_fd_interactive (0, 1);

	exit (1);
}

static
int cmd_get_sym (simarm_t *sim, const char *sym, unsigned long *val)
{
	if (arm_get_reg (sim->cpu, sym, val) == 0) {
		return (0);
	}

	return (1);
}

static
int cmd_set_sym (simarm_t *sim, const char *sym, unsigned long val)
{
	if (arm_set_reg (sim->cpu, sym, val) == 0) {
		return (0);
	}

	return (1);
}

static
unsigned char sarm_get_mem8 (simarm_t *sim, unsigned long addr)
{
	unsigned char val;

	if (arm_get_mem8 (sim->cpu, addr, par_xlat, &val)) {
		val = 0xff;
	}

	return (val);
}

static
void sarm_set_mem8 (simarm_t *sim, unsigned long addr, unsigned char val)
{
	if (arm_set_mem8 (sim->cpu, addr, par_xlat, val)) {
		; /* TLB miss */
	}
}

int str_isarg1 (const char *str, const char *arg)
{
	if (strcmp (str, arg) == 0) {
		return (1);
	}

	return (0);
}

int str_isarg2 (const char *str, const char *arg1, const char *arg2)
{
	if (strcmp (str, arg1) == 0) {
		return (1);
	}

	if (strcmp (str, arg2) == 0) {
		return (1);
	}

	return (0);
}

int main (int argc, char *argv[])
{
	int       i;
	int       run;
	char      *cfg;
	ini_sct_t *sct;

	if (argc == 2) {
		if (str_isarg1 (argv[1], "--help")) {
			prt_help();
			return (0);
		}
		else if (str_isarg1 (argv[1], "--version")) {
			prt_version();
			return (0);
		}
	}

	cfg = NULL;
	run = 0;

	pce_log_init();
	pce_log_add_fp (stderr, 0, MSG_INF);

	par_cfg = ini_sct_new (NULL);

	if (par_cfg == NULL) {
		return (1);
	}

	i = 1;
	while (i < argc) {
		if (str_isarg2 (argv[i], "-v", "--verbose")) {
			pce_log_set_level (stderr, MSG_DEB);
		}
		else if (str_isarg2 (argv[i], "-q", "--quiet")) {
			pce_log_set_level (stderr, MSG_ERR);
		}
		else if (str_isarg2 (argv[i], "-c", "--config")) {
			i += 1;
			if (i >= argc) {
				return (1);
			}
			cfg = argv[i];
		}
		else if (str_isarg2 (argv[i], "-l", "--log")) {
			i += 1;
			if (i >= argc) {
				return (1);
			}
			pce_log_add_fname (argv[i], MSG_DEB);
		}
		else if (str_isarg2 (argv[i], "-p", "--cpu")) {
			i += 1;
			if (i >= argc) {
				return (1);
			}

			par_cpu = argv[i];
		}
		else if (str_isarg2 (argv[i], "-r", "--run")) {
			run = 1;
		}
		else {
			printf ("%s: unknown option (%s)\n", argv[0], argv[i]);
			return (1);
		}

		i += 1;
	}

	pce_log (MSG_INF,
		"pce-simarm version " PCE_VERSION_STR "\n"
		"Copyright (C) 1995-2012 Hampa Hug <hampa@hampa.ch>\n"
	);

	if (pce_load_config (par_cfg, cfg)) {
		return (1);
	}

	sct = ini_next_sct (par_cfg, NULL, "simarm");

	if (sct == NULL) {
		sct = par_cfg;
	}

	par_sim = sarm_new (sct);

	signal (SIGINT, sig_int);
	signal (SIGTERM, sig_terminate);
	signal (SIGSEGV, sig_terminate);

#ifdef SIGPIPE
	signal (SIGPIPE, SIG_IGN);
#endif

	pce_console_init (stdin, stdout);

	mon_init (&par_mon);
	mon_set_cmd_fct (&par_mon, sarm_do_cmd, par_sim);
	mon_set_msg_fct (&par_mon, sarm_set_msg, par_sim);
	mon_set_get_mem_fct (&par_mon, par_sim, sarm_get_mem8);
	mon_set_set_mem_fct (&par_mon, par_sim, sarm_set_mem8);
	mon_set_memory_mode (&par_mon, 0);

	cmd_init (par_sim, cmd_get_sym, cmd_set_sym);
	sarm_cmd_init (par_sim, &par_mon);

	sarm_reset (par_sim);

	if (run) {
		sarm_run (par_sim);
		if (par_sim->brk != PCE_BRK_ABORT) {
			fputs ("\n", stdout);
		}
	}
	else {
		pce_log (MSG_INF, "type 'h' for help\n");
	}

	if (par_sim->brk != PCE_BRK_ABORT) {
		mon_run (&par_mon);
	}

	sarm_del (par_sim);

	mon_free (&par_mon);
	pce_console_done();
	pce_log_done();

	return (0);
}
