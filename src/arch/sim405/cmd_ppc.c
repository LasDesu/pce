/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/sim405/cmd_ppc.c                                    *
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
#include "cmd_ppc.h"
#include "hook.h"
#include "sim405.h"

#include <stdio.h>

#include <chipset/ppc405/uic.h>

#include <cpu/ppc405/ppc405.h>

#include <lib/cmd.h>
#include <lib/console.h>
#include <lib/monitor.h>
#include <lib/sysdep.h>


static mon_cmd_t par_cmd[] = {
	{ "c", "[cnt]", "clock" },
	{ "gb", "[addr...]", "run with breakpoints" },
	{ "g", "", "run" },
	{ "key", "[val...]", "send keycodes to the serial console" },
	{ "p", "[cnt]", "execute cnt instructions, skip calls [1]" },
	{ "rfi", "", "execute to next rfi or rfci" },
	{ "r", "reg [val]", "get or set a register" },
	{ "s", "[what]", "print status (mem|ppc|spr|time|uart0|uart1|uic)" },
	{ "tlb", "l [first [count]]", "list TLB entries" },
	{ "tlb", "s addr", "search the TLB" },
	{ "t", "[cnt]", "execute cnt instructions [1]" },
	{ "u", "[addr [cnt]]", "disassemble" },
	{ "x", "[c|r|v]", "set the translation mode (cpu, real, virtual)" }
};


void ppc_disasm_str (char *dst, p405_disasm_t *op)
{
	switch (op->argn) {
	case 0:
		sprintf (dst, "%08lX  %s", (unsigned long) op->ir, op->op);
		break;

	case 1:
		sprintf (dst, "%08lX  %-8s %s",
			(unsigned long) op->ir, op->op, op->arg1
		);
		break;

	case 2:
		sprintf (dst, "%08lX  %-8s %s, %s",
			(unsigned long) op->ir, op->op, op->arg1, op->arg2
		);
		break;

	case 3:
		sprintf (dst, "%08lX  %-8s %s, %s, %s",
			(unsigned long) op->ir, op->op,
			op->arg1, op->arg2, op->arg3
		);
		break;

	case 4:
		sprintf (dst, "%08lX  %-8s %s, %s, %s, %s",
			(unsigned long) op->ir, op->op,
			op->arg1, op->arg2, op->arg3, op->arg4
		);
		break;

	case 5:
		sprintf (dst, "%08lX  %-8s %s, %s, %s, %s, %s",
			(unsigned long) op->ir, op->op,
			op->arg1, op->arg2, op->arg3, op->arg4, op->arg5
		);
		break;

	default:
		strcpy (dst, "---");
		break;
	}
}

static
void prt_tlb_entry (p405_tlbe_t *ent, unsigned idx)
{
	pce_printf (
		"%02x: %08lx -> %08lx %06lx V%c E%c R%c W%c X%c  Z=%X TID=%02x  %08lx",
		idx,
		(unsigned long) p405_get_tlbe_epn (ent),
		(unsigned long) p405_get_tlbe_rpn (ent),
		(unsigned long) p405_get_tlbe_sizeb (ent),
		p405_get_tlbe_v (ent) ? '+' : '-',
		p405_get_tlbe_e (ent) ? '+' : '-',
		p405_get_tlbe_v (ent) ? '+' : '-',
		p405_get_tlbe_wr (ent) ? '+' : '-',
		p405_get_tlbe_ex (ent) ? '+' : '-',
		(unsigned) p405_get_tlbe_zsel (ent),
		(unsigned) p405_get_tlbe_tid (ent),
		(unsigned long) ent->mask
	);
}

void s405_prt_state_ppc (sim405_t *sim)
{
	unsigned           i;
	unsigned long long opcnt, clkcnt;
	unsigned long      delay;
	double             t, efrq;
	p405_t             *c;
	p405_disasm_t      op;
	char               str[256];

	pce_prt_sep ("PPC405");

	c = sim->ppc;

	opcnt = p405_get_opcnt (c);
	clkcnt = p405_get_clkcnt (c);
	delay = p405_get_delay (c);

	t = (double) (clock() - sim->real_clk) / CLOCKS_PER_SEC;
	efrq = (t < 0.001) ? 0.0 : (clkcnt / (1000.0 * 1000.0 * t));

	pce_printf (
		"CLK=%llx  OP=%llx  DLY=%lx  CPI=%.4f  TIME=%.4f  EFRQ=%.6f\n",
		clkcnt, opcnt, delay,
		(opcnt > 0) ? ((double) (clkcnt + delay) / (double) opcnt) : 1.0,
		t, efrq
	);

	pce_printf (
		" MSR=%08lX  XER=%08lX   CR=%08lX  XER=[%c%c%c]"
		"     CR0=[%c%c%c%c]\n",
		(unsigned long) p405_get_msr (c),
		(unsigned long) p405_get_xer (c),
		(unsigned long) p405_get_cr (c),
		(p405_get_xer_so (c)) ? 'S' : '-',
		(p405_get_xer_ov (c)) ? 'O' : '-',
		(p405_get_xer_ca (c)) ? 'C' : '-',
		(p405_get_cr_lt (c, 0)) ? 'L' : '-',
		(p405_get_cr_gt (c, 0)) ? 'G' : '-',
		(p405_get_cr_eq (c, 0)) ? 'E' : '-',
		(p405_get_cr_so (c, 0)) ? 'O' : '-'
	);

	pce_printf (
		"  PC=%08lX  CTR=%08lX   LR=%08lX  PID=%08lX  ZPR=%08lX\n",
		(unsigned long) p405_get_pc (c),
		(unsigned long) p405_get_ctr (c),
		(unsigned long) p405_get_lr (c),
		(unsigned long) p405_get_pid (c),
		(unsigned long) p405_get_zpr (c)
	);

	pce_printf (
		"SRR0=%08lX SRR1=%08lX SRR2=%08lX SRR3=%08lX  ESR=%08lX\n",
		(unsigned long) p405_get_srr (c, 0),
		(unsigned long) p405_get_srr (c, 1),
		(unsigned long) p405_get_srr (c, 2),
		(unsigned long) p405_get_srr (c, 3),
		(unsigned long) p405_get_esr (c)
	);

	for (i = 0; i < 8; i++) {
		pce_printf (
			" R%02u=%08lX  R%02u=%08lX  R%02u=%08lX  R%02u=%08lX"
			"  SP%u=%08lX\n",
			i + 0, (unsigned long) p405_get_gpr (c, i + 0),
			i + 8, (unsigned long) p405_get_gpr (c, i + 8),
			i + 16, (unsigned long) p405_get_gpr (c, i + 16),
			i + 24, (unsigned long) p405_get_gpr (c, i + 24),
			i, (unsigned long) p405_get_sprg (c, i)
		);
	}

	p405_disasm_mem (c, &op, p405_get_pc (c), par_xlat | P405_XLAT_EXEC);
	ppc_disasm_str (str, &op);

	pce_printf ("%08lX  %s\n", (unsigned long) p405_get_pc (c), str);
}

void s405_prt_state_spr (p405_t *c)
{
	uint32_t msr;

	pce_prt_sep ("PPC405 SPR");

	msr = p405_get_msr (c);

	pce_printf (
		" MSR=%08lX  [%s %s %s %s %s %s %s %s %s %s %s %s %s %s]\n",
		(unsigned long) msr,
		(msr & P405_MSR_AP) ? "AP" : "ap",
		(msr & P405_MSR_APE) ? "APE" : "ape",
		(msr & P405_MSR_WE) ? "WE" : "we",
		(msr & P405_MSR_CE) ? "CE" : "ce",
		(msr & P405_MSR_EE) ? "EE" : "ee",
		(msr & P405_MSR_PR) ? "PR" : "pr",
		(msr & P405_MSR_FP) ? "FP" : "fp",
		(msr & P405_MSR_ME) ? "ME" : "me",
		(msr & P405_MSR_FE0) ? "FE0" : "fe0",
		(msr & P405_MSR_DWE) ? "DWE" : "dwe",
		(msr & P405_MSR_DE) ? "DE" : "de",
		(msr & P405_MSR_FE0) ? "FE1" : "fe1",
		(msr & P405_MSR_IR) ? "IR" : "ir",
		(msr & P405_MSR_DR) ? "DR" : "dr"
	);

	pce_printf (
		"  PC=%08lX   XER=%08lX    CR=%08lX   PID=%08lX   ZPR=%08lX\n",
		(unsigned long) p405_get_pc (c),
		(unsigned long) p405_get_xer (c),
		(unsigned long) p405_get_cr (c),
		(unsigned long) p405_get_pid (c),
		(unsigned long) p405_get_zpr (c)
	);

	pce_printf (
		"SPG0=%08lX  SRR0=%08lX    LR=%08lX   CTR=%08lX   ESR=%08lX\n",
		(unsigned long) p405_get_sprg (c, 0),
		(unsigned long) p405_get_srr (c, 0),
		(unsigned long) p405_get_lr (c),
		(unsigned long) p405_get_ctr (c),
		(unsigned long) p405_get_esr (c)
	);

	pce_printf (
		"SPG1=%08lX  SRR1=%08lX   TBU=%08lX   TBL=%08lX  DEAR=%08lX\n",
		(unsigned long) p405_get_sprg (c, 1),
		(unsigned long) p405_get_srr (c, 1),
		(unsigned long) p405_get_tbu (c),
		(unsigned long) p405_get_tbl (c),
		(unsigned long) p405_get_dear (c)
	);

	pce_printf ("SPG2=%08lX  SRR2=%08lX  EVPR=%08lX\n",
		(unsigned long) p405_get_sprg (c, 2),
		(unsigned long) p405_get_srr (c, 2),
		(unsigned long) p405_get_evpr (c)
	);

	pce_printf (
		"SPG3=%08lX  SRR3=%08lX  DCCR=%08lX  DCWR=%08lX  ICCR=%08lX\n",
		(unsigned long) p405_get_sprg (c, 3),
		(unsigned long) p405_get_srr (c, 3),
		(unsigned long) p405_get_dccr (c),
		(unsigned long) p405_get_dcwr (c),
		(unsigned long) p405_get_iccr (c)
	);

	pce_printf ("SPG4=%08lX  DBC0=%08lX  PIT0=%08lX  PIT1=%08lX\n",
		(unsigned long) p405_get_sprg (c, 4),
		(unsigned long) p405_get_dbcr0 (c),
		(unsigned long) p405_get_pit (c, 0),
		(unsigned long) p405_get_pit (c, 1)
	);

	pce_printf ("SPG5=%08lX  DBC1=%08lX   TCR=%08lX   TSR=%08lX\n",
		(unsigned long) p405_get_sprg (c, 5),
		(unsigned long) p405_get_dbcr0 (c),
		(unsigned long) p405_get_tcr (c),
		(unsigned long) p405_get_tsr (c)
	);

	pce_printf ("SPG6=%08lX  DBSR=%08lX\n",
		(unsigned long) p405_get_sprg (c, 6),
		(unsigned long) p405_get_dbsr (c)
	);

	pce_printf ("SPG7=%08lX\n",
		(unsigned long) p405_get_sprg (c, 7)
	);
}

void s405_prt_state_uic (p405_uic_t *uic)
{
	pce_prt_sep ("UIC");

	pce_printf (
		"  L=%08lX  N00=%08lX  N08=%08lX  N16=%08lX  N24=%08lX\n",
		(unsigned long) p405uic_get_levels (uic),
		p405uic_get_int_cnt (uic, 0),
		p405uic_get_int_cnt (uic, 8),
		p405uic_get_int_cnt (uic, 16),
		p405uic_get_int_cnt (uic, 24)
	);
	pce_printf (
		" SR=%08lX  N01=%08lX  N09=%08lX  N17=%08lX  N25=%08lX\n",
		(unsigned long) p405uic_get_sr (uic),
		p405uic_get_int_cnt (uic, 1),
		p405uic_get_int_cnt (uic, 9),
		p405uic_get_int_cnt (uic, 17),
		p405uic_get_int_cnt (uic, 25)
	);
	pce_printf (
		" ER=%08lX  N02=%08lX  N10=%08lX  N18=%08lX  N26=%08lX\n",
		(unsigned long) p405uic_get_er (uic),
		p405uic_get_int_cnt (uic, 2),
		p405uic_get_int_cnt (uic, 10),
		p405uic_get_int_cnt (uic, 18),
		p405uic_get_int_cnt (uic, 26)
	);
	pce_printf (
		"MSR=%08lX  N03=%08lX  N11=%08lX  N19=%08lX  N27=%08lX\n",
		(unsigned long) p405uic_get_msr (uic),
		p405uic_get_int_cnt (uic, 3),
		p405uic_get_int_cnt (uic, 11),
		p405uic_get_int_cnt (uic, 19),
		p405uic_get_int_cnt (uic, 27)
	);
	pce_printf (
		" CR=%08lX  N04=%08lX  N12=%08lX  N20=%08lX  N28=%08lX\n",
		(unsigned long) p405uic_get_cr (uic),
		p405uic_get_int_cnt (uic, 4),
		p405uic_get_int_cnt (uic, 12),
		p405uic_get_int_cnt (uic, 20),
		p405uic_get_int_cnt (uic, 28)
	);
	pce_printf (
		" PR=%08lX  N05=%08lX  N13=%08lX  N21=%08lX  N29=%08lX\n",
		(unsigned long) p405uic_get_pr (uic),
		p405uic_get_int_cnt (uic, 5),
		p405uic_get_int_cnt (uic, 13),
		p405uic_get_int_cnt (uic, 21),
		p405uic_get_int_cnt (uic, 29)
	);
	pce_printf (
		" TR=%08lX  N06=%08lX  N14=%08lX  N22=%08lX  N30=%08lX\n",
		(unsigned long) p405uic_get_tr (uic),
		p405uic_get_int_cnt (uic, 6),
		p405uic_get_int_cnt (uic, 14),
		p405uic_get_int_cnt (uic, 22),
		p405uic_get_int_cnt (uic, 30)
	);
	pce_printf (
		"VCR=%08lX  N07=%08lX  N15=%08lX  N23=%08lX  N31=%08lX\n",
		(unsigned long) p405uic_get_vcr (uic),
		p405uic_get_int_cnt (uic, 7),
		p405uic_get_int_cnt (uic, 15),
		p405uic_get_int_cnt (uic, 23),
		p405uic_get_int_cnt (uic, 31)
	);
	pce_printf (" VR=%08lX\n", (unsigned long) p405uic_get_vr (uic));
}

static
void s405_print_state_uart (e8250_t *uart, unsigned long base)
{
	char          p;
	unsigned char lsr, msr;

	lsr = e8250_get_lsr (uart);
	msr = e8250_get_msr (uart);

	switch (e8250_get_parity (uart)) {
	case E8250_PARITY_N:
		p = 'N';
		break;

	case E8250_PARITY_E:
		p = 'E';
		break;

	case E8250_PARITY_O:
		p = 'O';
		break;

	case E8250_PARITY_M:
		p = 'M';
		break;

	case E8250_PARITY_S:
		p = 'S';
		break;

	default:
		p = '?';
		break;
	}

	pce_prt_sep ("8250-UART");

	pce_printf (
		"IO=%08lX  %lu %u%c%u  DTR=%d  RTS=%d   DSR=%d  CTS=%d  DCD=%d  RI=%d\n"
		"TxD=%02X%c RxD=%02X%c SCR=%02X  DIV=%04X\n"
		"IER=%02X  IIR=%02X  LCR=%02X  LSR=%02X  MCR=%02X  MSR=%02X\n",
		base,
		e8250_get_bps (uart), e8250_get_databits (uart), p,
		e8250_get_stopbits (uart),
		e8250_get_dtr (uart), e8250_get_rts (uart),
		(msr & E8250_MSR_DSR) != 0,
		(msr & E8250_MSR_CTS) != 0,
		(msr & E8250_MSR_DCD) != 0,
		(msr & E8250_MSR_RI) != 0,
		uart->txd, (lsr & E8250_LSR_TBE) ? ' ' : '*',
		uart->rxd, (lsr & E8250_LSR_RRD) ? '*' : ' ',
		uart->scratch, uart->divisor,
		uart->ier, uart->iir, uart->lcr, lsr, uart->mcr, msr
	);
}

static
void s405_print_state_time (sim405_t *sim)
{
	p405_t        *c;
	unsigned long tcr, tsr, tbl, msk, rem;

	c = sim->ppc;

	tcr = p405_get_tcr (c);
	tsr = p405_get_tsr (c);
	tbl = p405_get_tbl (c);

	msk = 0x100UL << ((tcr >> 22) & 0x0c);
	rem = msk - (tbl & (msk - 1)) + ((tbl & msk) ? msk : 0);

	pce_prt_sep ("TIME");

	pce_printf ("TBU=%08lX TBL=%08lX\n",
		(unsigned long) p405_get_tbu (c),
		tbl
	);

	pce_printf ("TCR=%08lX [PIE=%d ARE=%d FIE=%d FP=%X WIE=%d]\n",
		tcr,
		(tcr & P405_TCR_PIE) != 0,
		(tcr & P405_TCR_ARE) != 0,
		(tcr & P405_TCR_FIE) != 0,
		(unsigned) ((tcr >> 24) & 3),
		(tcr & P405_TCR_WIE) != 0
	);

	pce_printf ("TSR=%08lX [PIS=%d FIS=%d WIS=%d]\n",
		tsr,
		(tsr & P405_TSR_PIS) != 0,
		(tsr & P405_TSR_FIS) != 0,
		(tsr & P405_TSR_WIS) != 0
	);

	pce_printf ("PIT VAL=%08lX REL=%08lX ARE=%d      PIE=%d PIS=%d\n",
		(unsigned long) (p405_get_pit (c, 0)),
		(unsigned long) (p405_get_pit (c, 1)),
		(tcr & P405_TCR_ARE) != 0,
		(tcr & P405_TCR_PIE) != 0,
		(tsr & P405_TSR_PIS) != 0
	);

	pce_printf ("FIT REM=%08lX TBL=%08lx MSK=%06lX FIE=%d FIS=%d\n",
		rem,
		tbl,
		msk,
		(tcr & P405_TCR_FIE) != 0,
		(tsr & P405_TSR_FIS) != 0
	);
}

void s405_prt_state_mem (sim405_t *sim)
{
	FILE *fp;

	pce_prt_sep ("PPC MEM");

	mem_prt_state (sim->mem, pce_get_fp_out());

	fp = pce_get_redir_out();
	if (fp != NULL) {
		mem_prt_state (sim->mem, fp);
	}
}

void prt_state (sim405_t *sim, const char *str)
{
	cmd_t cmd;

	cmd_set_str (&cmd, str);

	if (cmd_match_eol (&cmd)) {
		return;
	}

	while (!cmd_match_eol (&cmd)) {
		if (cmd_match (&cmd, "ppc") || cmd_match (&cmd, "cpu")) {
			s405_prt_state_ppc (sim);
		}
		else if (cmd_match (&cmd, "spr")) {
			s405_prt_state_spr (sim->ppc);
		}
		else if (cmd_match (&cmd, "uic")) {
			s405_prt_state_uic (&sim->uic);
		}
		else if (cmd_match (&cmd, "uart0")) {
			if (sim->serport[0] != NULL) {
				s405_print_state_uart (&sim->serport[0]->uart, sim->serport[0]->io);
			}
		}
		else if (cmd_match (&cmd, "uart1")) {
			if (sim->serport[1] != NULL) {
				s405_print_state_uart (&sim->serport[1]->uart, sim->serport[1]->io);
			}
		}
		else if (cmd_match (&cmd, "time")) {
			s405_print_state_time (sim);
		}
		else if (cmd_match (&cmd, "mem")) {
			s405_prt_state_mem (sim);
		}
		else {
			pce_printf ("unknown component (%s)\n",
				cmd_get_str (&cmd)
			);
			return;
		}
	}
}


static
int ppc_check_break (sim405_t *sim)
{
	if (bps_check (&sim->bps, 0, p405_get_pc (sim->ppc), stdout)) {
		return (1);
	}

	if (sim->brk) {
		return (1);
	}

	return (0);
}

static
void ppc_exec (sim405_t *sim)
{
	unsigned long long old;

	old = p405_get_opcnt (sim->ppc);

	while (p405_get_opcnt (sim->ppc) == old) {
		s405_clock (sim, 1);
	}
}

static
int ppc_exec_to (sim405_t *sim, unsigned long addr)
{
	while (p405_get_pc (sim->ppc) != addr) {
		s405_clock (sim, 1);

		if (sim->brk) {
			return (1);
		}
	}

	return (0);
}

static
int ppc_exec_off (sim405_t *sim, unsigned long addr)
{
	while (p405_get_pc (sim->ppc) == addr) {
		s405_clock (sim, 1);

		if (sim->brk) {
			return (1);
		}
	}

	return (0);
}

void ppc_run (sim405_t *sim)
{
	pce_start (&sim->brk);
	s405_clock_discontinuity (sim);

	while (1) {
		s405_clock (sim, 64);

		if (sim->brk) {
			break;
		}
	}

	pce_stop();
}


static
int s405_trap (sim405_t *sim, unsigned ofs)
{
	int  log;
	char *name;

	log = 0;

	switch (ofs) {
	case 0x0300:
		name = "data store";
		break;

	case 0x0400:
		name = "instruction store";
		break;

	case 0x0500:
		name = "external interrupt";
		break;

	case 0x0700:
		name = "program";
		if (sim->ppc->exception_esr & P405_ESR_PIL) {
			log = 0;
		}
		else if (sim->ppc->exception_esr & P405_ESR_PTR) {
			log = 1;
		}
		break;

	case 0x0800:
		name = "fpu unavailable";
		log = 1;
		break;

	case 0x0c00:
		name = "system call";
		break;

	case 0x1000:
		name = "PIT";
		break;

	case 0x1010:
		name = "FIT";
		break;

	case 0x1100:
		name = "TLB miss data";
		break;

	case 0x1200:
		name = "TLB miss instruction";
		break;

	default:
		name = "unknown";
		log = 1;
		break;
	}

	if (log) {
		pce_log (MSG_DEB, "%08lX: ESR=%08lX DEAR=%08lX IR=%0lX OFS=%04X %s\n",
			(unsigned long) p405_get_pc (sim->ppc),
			(unsigned long) sim->ppc->exception_esr,
			(unsigned long) sim->ppc->exception_dear,
			(unsigned long) sim->ppc->ir,
			ofs, name
		);
	}

	return (0);
}

static
void ppc_log_undef (void *ext, unsigned long ir)
{
	unsigned op1, op2;
	sim405_t *sim;

	sim = (sim405_t *) ext;

	op1 = (ir >> 26) & 0x3f;
	op2 = (ir >> 1) & 0x3ff;

	if (op1 == 4) {
		/* various multiply-accumulate instructions */
		return;
	}

	pce_log (MSG_DEB,
		"%08lX: undefined operation [%08lX] op1=%02X op2=%03X\n",
		(unsigned long) p405_get_pc (sim->ppc), ir, op1, op2
	);

	/* s405_break (sim, PCE_BRK_STOP); */
}

#if 0
static
void ppc_log_mem (void *ext, unsigned mode,
	unsigned long raddr, unsigned long vaddr, unsigned long val)
{
	sim405_t *sim;

	sim = (sim405_t *) ext;

	if ((raddr >= 0xe0000000UL) && (raddr < 0xef600000UL)) {
		pce_log (MSG_DEB, "mem: 0x%08lx 0x%08lx %02x\n", raddr, val, mode);
	}
}
#endif

static
void do_c (cmd_t *cmd, sim405_t *sim)
{
	unsigned long cnt;

	cnt = 1;

	cmd_match_uint32 (cmd, &cnt);

	if (!cmd_match_end (cmd)) {
		return;
	}

	while (cnt > 0) {
		s405_clock (sim, 1);
		cnt -= 1;
	}

	s405_prt_state_ppc (sim);
}

static
void do_g_b (cmd_t *cmd, sim405_t *sim)
{
	unsigned long addr;
	breakpoint_t  *bp;

	while (cmd_match_uint32 (cmd, &addr)) {
		bp = bp_addr_new (addr);
		bps_bp_add (&sim->bps, bp);
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	pce_start (&sim->brk);
	s405_clock_discontinuity (sim);

	while (1) {
		ppc_exec (sim);

		if (ppc_check_break (sim)) {
			break;
		}
	}

	pce_stop();
}

static
void do_g (cmd_t *cmd, sim405_t *sim)
{
	if (cmd_match (cmd, "b")) {
		do_g_b (cmd, sim);
		return;
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	ppc_run (sim);
}

static
void do_key (cmd_t *cmd, sim405_t *sim)
{
	unsigned short c;

	while (cmd_match_uint16 (cmd, &c)) {
		s405_set_keycode (sim, c);
	}

	if (!cmd_match_end (cmd)) {
		return;
	}
}

static
void do_p (cmd_t *cmd, sim405_t *sim)
{
	unsigned long cnt;
	p405_disasm_t dis;

	cnt = 1;

	cmd_match_uint32 (cmd, &cnt);

	if (!cmd_match_end (cmd)) {
		return;
	}

	pce_start (&sim->brk);
	s405_clock_discontinuity (sim);

	while (cnt > 0) {
		p405_disasm_mem (sim->ppc, &dis, p405_get_pc (sim->ppc), P405_XLAT_CPU | P405_XLAT_EXEC);

		if (dis.flags & P405_DFLAG_CALL) {
			if (ppc_exec_to (sim, p405_get_pc (sim->ppc) + 4)) {
				break;
			}
		}
		else {
			uint32_t msr;

			msr = p405_get_msr (sim->ppc);

			if (ppc_exec_off (sim, p405_get_pc (sim->ppc))) {
				break;
			}

			if (msr & P405_MSR_PR) {
				/* check if exception occured */
				while ((p405_get_msr (sim->ppc) & P405_MSR_PR) == 0) {
					s405_clock (sim, 1);

					if (sim->brk) {
						break;
					}
				}
			}
		}

		cnt -= 1;
	}

	pce_stop();

	s405_prt_state_ppc (sim);
}

static
void do_rfi (cmd_t *cmd, sim405_t *sim)
{
	p405_disasm_t dis;

	if (!cmd_match_end (cmd)) {
		return;
	}

	pce_start (&sim->brk);
	s405_clock_discontinuity (sim);

	while (1) {
		p405_disasm_mem (sim->ppc, &dis, p405_get_pc (sim->ppc),
			P405_XLAT_CPU | P405_XLAT_EXEC
		);

		if (ppc_exec_off (sim, p405_get_pc (sim->ppc))) {
			break;
		}

		if (dis.flags & P405_DFLAG_RFI) {
			break;
		}
	}

	pce_stop();

	s405_prt_state_ppc (sim);
}

static
void do_r (cmd_t *cmd, sim405_t *sim)
{
	unsigned long val;
	char          sym[256];

	if (cmd_match_eol (cmd)) {
		s405_prt_state_ppc (sim);
		return;
	}

	if (!cmd_match_ident (cmd, sym, 256)) {
		pce_printf ("missing register\n");
		return;
	}

	if (p405_get_reg (sim->ppc, sym, &val)) {
		pce_printf ("bad register (%s)\n", sym);
		return;
	}

	if (cmd_match_eol (cmd)) {
		pce_printf ("%08lX\n", val);
		return;
	}

	if (!cmd_match_uint32 (cmd, &val)) {
		pce_printf ("missing value\n");
		return;
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	p405_set_reg (sim->ppc, sym, val);

	s405_prt_state_ppc (sim);
}

static
void do_s (cmd_t *cmd, sim405_t *sim)
{
	if (cmd_match_eol (cmd)) {
		s405_prt_state_ppc (sim);
		return;
	}

	prt_state (sim, cmd_get_str (cmd));
}

static
void do_tlb_l (cmd_t *cmd, sim405_t *sim)
{
	unsigned long i, n, max;
	p405_tlbe_t *ent;

	i = 0;
	n = p405_get_tlb_entry_cnt (sim->ppc);

	max = n;

	if (cmd_match_uint32 (cmd, &i)) {
		cmd_match_uint32 (cmd, &n);
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	i = i % max;

	while (n > 0) {
		ent = p405_get_tlb_entry_idx (sim->ppc, i);

		if (ent != NULL) {
			prt_tlb_entry (ent, i);
			fputs ("\n", stdout);
		}

		i = (i + 1) % max;
		n -= 1;
	}
}

static
void do_tlb_s (cmd_t *cmd, sim405_t *sim)
{
	unsigned long addr;
	unsigned      idx;
	p405_tlbe_t *ent;

	if (!cmd_match_uint32 (cmd, &addr)) {
		cmd_error (cmd, "expecting address");
		return;
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	idx = p405_get_tlb_index (sim->ppc, addr);
	ent = p405_get_tlb_entry_idx (sim->ppc, idx);

	if (ent == NULL) {
		pce_printf ("no match\n");
	}
	else {
		prt_tlb_entry (ent, idx);
		pce_puts ("\n");
	}
}

static
void do_tlb (cmd_t *cmd, sim405_t *sim)
{
	if (cmd_match (cmd, "l")) {
		do_tlb_l (cmd, sim);
	}
	else if (cmd_match (cmd, "s")) {
		do_tlb_s (cmd, sim);
	}
	else {
		cmd_error (cmd, "unknown tlb command");
	}
}

static
void do_t (cmd_t *cmd, sim405_t *sim)
{
	unsigned long i, n;

	n = 1;

	cmd_match_uint32 (cmd, &n);

	if (!cmd_match_end (cmd)) {
		return;
	}

	pce_start (&sim->brk);
	s405_clock_discontinuity (sim);

	for (i = 0; i < n; i++) {
		ppc_exec (sim);
	}

	pce_stop();

	s405_prt_state_ppc (sim);
}

static
void do_u (cmd_t *cmd, sim405_t *sim)
{
	unsigned             i;
	int                  to;
	unsigned long        addr, cnt;
	static unsigned int  first = 1;
	static unsigned long saddr = 0;
	p405_disasm_t        op;
	char                 str[256];

	if (first) {
		first = 0;
		saddr = p405_get_pc (sim->ppc);
	}

	to = 0;
	addr = saddr;
	cnt = 16;

	if (cmd_match (cmd, "-")) {
		to = 1;
	}

	if (cmd_match_uint32 (cmd, &addr)) {
		cmd_match_uint32 (cmd, &cnt);
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	if (to) {
		addr -= 4 * (cnt - 1);
	}

	for (i = 0; i < cnt; i++) {
		p405_disasm_mem (sim->ppc, &op, addr, par_xlat | P405_XLAT_EXEC);
		ppc_disasm_str (str, &op);

		pce_printf ("%08lX  %s\n", addr, str);

		addr += 4;
	}

	saddr = addr;
}

static
void do_x (cmd_t *cmd, sim405_t *sim)
{
	unsigned xlat;

	if (cmd_match_eol (cmd)) {
		switch (par_xlat) {
		case P405_XLAT_CPU:
			pce_printf ("xlat cpu\n");
			break;

		case P405_XLAT_REAL:
			pce_printf ("xlat real\n");
			break;

		case P405_XLAT_VIRTUAL:
			pce_printf ("xlat virtual\n");
			break;

		default:
			pce_printf ("xlat unknown\n");
			break;
		}

		return;
	}

	if (cmd_match (cmd, "c")) {
		xlat = P405_XLAT_CPU;
	}
	else if (cmd_match (cmd, "r")) {
		xlat = P405_XLAT_REAL;
	}
	else if (cmd_match (cmd, "v")) {
		xlat = P405_XLAT_VIRTUAL;
	}
	else {
		cmd_error (cmd, "unknown translation type");
		return;
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	par_xlat = xlat;
}

int ppc_do_cmd (sim405_t *sim, cmd_t *cmd)
{
	if (cmd_match (cmd, "b")) {
		cmd_do_b (cmd, &sim->bps);
	}
	else if (cmd_match (cmd, "c")) {
		do_c (cmd, sim);
	}
	else if (cmd_match (cmd, "g")) {
		do_g (cmd, sim);
	}
	else if (cmd_match (cmd, "key")) {
		do_key (cmd, sim);
	}
	else if (cmd_match (cmd, "p")) {
		do_p (cmd, sim);
	}
	else if (cmd_match (cmd, "rfi")) {
		do_rfi (cmd, sim);
	}
	else if (cmd_match (cmd, "r")) {
		do_r (cmd, sim);
	}
	else if (cmd_match (cmd, "s")) {
		do_s (cmd, sim);
	}
	else if (cmd_match (cmd, "tlb")) {
		do_tlb (cmd, sim);
	}
	else if (cmd_match (cmd, "t")) {
		do_t (cmd, sim);
	}
	else if (cmd_match (cmd, "u")) {
		do_u (cmd, sim);
	}
	else if (cmd_match (cmd, "x")) {
		do_x (cmd, sim);
	}
	else {
		return (1);
	}

	return (0);
}

void ppc_cmd_init (sim405_t *sim, monitor_t *mon)
{
	mon_cmd_add (mon, par_cmd, sizeof (par_cmd) / sizeof (par_cmd[0]));
	mon_cmd_add_bp (mon);

	sim->ppc->log_ext = sim;
	sim->ppc->log_opcode = NULL;
	sim->ppc->log_undef = &ppc_log_undef;
	sim->ppc->log_mem = NULL;

	p405_set_hook_fct (sim->ppc, sim, s405_hook);
	p405_set_trap_fct (sim->ppc, sim, s405_trap);
}
