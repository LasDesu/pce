/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/ibmpc/cmd.c                                         *
 * Created:     2010-09-21 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2010-2019 Hampa Hug <hampa@hampa.ch>                     *
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
#include "ibmpc.h"

#include <stdio.h>
#include <string.h>

#include <lib/brkpt.h>
#include <lib/cmd.h>
#include <lib/console.h>
#include <lib/log.h>
#include <lib/monitor.h>
#include <lib/msgdsk.h>
#include <lib/sysdep.h>


static mon_cmd_t par_cmd[] = {
	{ "boot", "[drive]", "set the boot drive" },
	{ "c", "[cnt]", "clock [1]" },
	{ "gb", "[addr...]", "run with breakpoints" },
	{ "g", "far", "run until CS changes" },
	{ "g", "", "run" },
	{ "hm", "", "print help on messages" },
	{ "i", "[b|w] port", "input a byte or word from a port" },
	{ "key", "[[+|-]key...]", "simulate pressing or releasing keys" },
	{ "log", "int l", "list interrupt log expressions" },
	{ "log", "int n [expr]", "set interrupt n log expression to expr" },
	{ "o", "[b|w] port val", "output a byte or word to a port" },
	{ "pq", "[c|f|s]", "prefetch queue clear/fill/status" },
	{ "p", "[cnt]", "execute cnt instructions, without trace in calls [1]" },
	{ "r", "[reg val]", "set a register" },
	{ "s", "[what]", "print status (pc|cpu|disks|ems|mem|pic|pit|ports|ppi|time|uart|video|xms)" },
	{ "trace", "on|off|expr", "turn trace on or off" },
	{ "t", "[cnt]", "execute cnt instructions [1]" },
	{ "u", "[addr [cnt [mode]]]", "disassemble" }
};


static
void disasm_str (char *dst, e86_disasm_t *op)
{
	unsigned     i;
	unsigned     dst_i;

	dst_i = 2;
	sprintf (dst, "%02X", op->dat[0]);

	for (i = 1; i < op->dat_n; i++) {
		sprintf (dst + dst_i, " %02X", op->dat[i]);
		dst_i += 3;
	}

	dst[dst_i++] = ' ';
	while (dst_i < 20) {
		dst[dst_i++] = ' ';
	}

	if ((op->flags & ~(E86_DFLAGS_CALL | E86_DFLAGS_LOOP)) != 0) {
		unsigned flg;

		flg = op->flags;

		dst[dst_i++] = '[';

		if (flg & E86_DFLAGS_186) {
			dst_i += sprintf (dst + dst_i, "186");
			flg &= ~E86_DFLAGS_186;
		}

		if (flg != 0) {
			if (flg != op->flags) {
				dst[dst_i++] = ' ';
			}
			dst_i += sprintf (dst + dst_i, " %04X", flg);
		}
		dst[dst_i++] = ']';
		dst[dst_i++] = ' ';
	}

	strcpy (dst + dst_i, op->op);
	while (dst[dst_i] != 0) {
		dst_i += 1;
	}

	if (op->arg_n > 0) {
		dst[dst_i++] = ' ';
		while (dst_i < 26) {
			dst[dst_i++] = ' ';
		}
	}

	if (op->arg_n == 1) {
		dst_i += sprintf (dst + dst_i, "%s", op->arg1);
	}
	else if (op->arg_n == 2) {
		dst_i += sprintf (dst + dst_i, "%s, %s", op->arg1, op->arg2);
	}

	dst[dst_i] = 0;
}

static
void prt_uint8_bin (unsigned char val)
{
	unsigned      i;
	unsigned char m;
	char          str[16];

	m = 0x80;

	for (i = 0; i < 8; i++) {
		str[i] = (val & m) ? '1' : '0';
		m = m >> 1;
	}

	str[8] = 0;

	pce_puts (str);
}

static
void prt_state_video (video_t *vid)
{
	FILE *fp;

	pce_prt_sep ("video");

	pce_video_print_info (vid, pce_get_fp_out());

	fp = pce_get_redir_out();
	if (fp != NULL) {
		pce_video_print_info (vid, fp);
	}
}

static
void prt_state_ems (ems_t *ems)
{
	pce_prt_sep ("EMS");
	ems_prt_state (ems);
}

static
void prt_state_xms (xms_t *xms)
{
	pce_prt_sep ("XMS");
	xms_prt_state (xms);
}

static
void prt_state_pit (e8253_t *pit)
{
	unsigned        i;
	e8253_counter_t *cnt;

	pce_prt_sep ("8253-PIT");

	for (i = 0; i < 3; i++) {
		cnt = &pit->counter[i];

		pce_printf (
			"C%d: SR=%02X M=%u RW=%d  CE=%04X  %s=%02X %s=%02X  %s=%02X %s=%02X  "
			"G=%u O=%u R=%d\n",
			i,
			cnt->sr, cnt->mode, cnt->rw,
			cnt->ce,
			(cnt->cr_wr & 2) ? "cr1" : "CR1", cnt->cr[1],
			(cnt->cr_wr & 1) ? "cr0" : "CR0", cnt->cr[0],
			(cnt->ol_rd & 2) ? "ol1" : "OL1", cnt->ol[1],
			(cnt->ol_rd & 1) ? "ol0" : "OL0", cnt->ol[0],
			(unsigned) cnt->gate_val,
			(unsigned) cnt->out_val,
			cnt->counting
		);
	}
}

static
void prt_state_ppi (e8255_t *ppi)
{
	pce_prt_sep ("8255-PPI");

	pce_printf (
		"MOD=%02X  MODA=%u  MODB=%u",
		ppi->mode, ppi->group_a_mode, ppi->group_b_mode
	);

	if (ppi->port[0].inp != 0) {
		pce_printf ("  A=I[%02X]", e8255_get_inp (ppi, 0));
	}
	else {
		pce_printf ("  A=O[%02X]", e8255_get_out (ppi, 0));
	}

	if (ppi->port[1].inp != 0) {
		pce_printf ("  B=I[%02X]", e8255_get_inp (ppi, 1));
	}
	else {
		pce_printf ("  B=O[%02X]", e8255_get_out (ppi, 1));
	}

	switch (ppi->port[2].inp) {
	case 0xff:
		pce_printf ("  C=I[%02X]", e8255_get_inp (ppi, 2));
		break;

	case 0x00:
		pce_printf ("  C=O[%02X]", e8255_get_out (ppi, 2));
		break;

	case 0x0f:
		pce_printf ("  CH=O[%X]  CL=I[%X]",
			(e8255_get_out (ppi, 2) >> 4) & 0x0f,
			e8255_get_inp (ppi, 2) & 0x0f
		);
		break;

	case 0xf0:
		pce_printf ("  CH=I[%X]  CL=O[%X]",
			(e8255_get_inp (ppi, 2) >> 4) & 0x0f,
			e8255_get_out (ppi, 2) & 0x0f
		);
		break;
	}

	pce_puts ("\n");
}

static
void prt_state_dma (e8237_t *dma)
{
	unsigned i;

	pce_prt_sep ("8237-DMAC");

	pce_printf ("CMD=%02X  PRI=%02X  CHK=%d\n",
		e8237_get_command (dma),
		e8237_get_priority (dma),
		dma->check != 0
	);

	for (i = 0; i < 4; i++) {
		unsigned short state;

		state = e8237_get_state (dma, i);

		pce_printf (
			"CHN %u: MODE=%02X ADDR=%04X[%04X] CNT=%04X[%04X] DREQ=%d SREQ=%d MASK=%d\n",
			i,
			e8237_get_mode (dma, i) & 0xfcU,
			e8237_get_addr (dma, i),
			e8237_get_addr_base (dma, i),
			e8237_get_cnt (dma, i),
			e8237_get_cnt_base (dma, i),
			(state & E8237_STATE_DREQ) != 0,
			(state & E8237_STATE_SREQ) != 0,
			(state & E8237_STATE_MASK) != 0
		);
	}
}

static
void prt_state_pic (e8259_t *pic)
{
	unsigned i;

	pce_prt_sep ("8259A-PIC");

	pce_puts ("INP=");
	prt_uint8_bin (pic->irq_inp);
	pce_puts ("\n");

	pce_puts ("IRR=");
	prt_uint8_bin (e8259_get_irr (pic));
	pce_printf ("  PRIO=%u\n", pic->priority);

	pce_puts ("IMR=");
	prt_uint8_bin (e8259_get_imr (pic));
	pce_printf ("  INTR=%d\n", pic->intr_val != 0);

	pce_puts ("ISR=");
	prt_uint8_bin (e8259_get_isr (pic));
	pce_puts ("\n");

	pce_printf ("ICW=[%02X %02X %02X %02X]  OCW=[%02X %02X %02X]\n",
		e8259_get_icw (pic, 0), e8259_get_icw (pic, 1), e8259_get_icw (pic, 2),
		e8259_get_icw (pic, 3),
		e8259_get_ocw (pic, 0), e8259_get_ocw (pic, 1), e8259_get_ocw (pic, 2)
	);

	pce_printf ("N0=%04lX", pic->irq_cnt[0]);
	for (i = 1; i < 8; i++) {
		pce_printf ("  N%u=%04lX", i, pic->irq_cnt[i]);
	}

	pce_puts ("\n");
}

static
void prt_state_uart (e8250_t *uart, unsigned base)
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
		"IO=%04X  %lu %u%c%u  DTR=%d  RTS=%d   DSR=%d  CTS=%d  DCD=%d  RI=%d\n"
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

void prt_state_cpu (e8086_t *c)
{
	pce_prt_sep ("8086");

	pce_printf (
		"AX=%04X  BX=%04X  CX=%04X  DX=%04X  "
		"SP=%04X  BP=%04X  SI=%04X  DI=%04X INT=%02X%c\n",
		e86_get_ax (c), e86_get_bx (c), e86_get_cx (c), e86_get_dx (c),
		e86_get_sp (c), e86_get_bp (c), e86_get_si (c), e86_get_di (c),
		par_pc->current_int & 0xff,
		(par_pc->current_int & 0x100) ? '*' : ' '
	);

	pce_printf ("DS=%04X  ES=%04X  SS=%04X  CS=%04X  IP=%04X  F =%04X",
		e86_get_ds (c), e86_get_es (c), e86_get_ss (c), e86_get_cs (c),
		e86_get_ip (c), e86_get_flags (c)
	);

	pce_printf ("  [%c%c%c%c%c%c%c%c%c]\n",
		e86_get_df (c) ? 'D' : '-',
		e86_get_if (c) ? 'I' : '-',
		e86_get_tf (c) ? 'T' : '-',
		e86_get_of (c) ? 'O' : '-',
		e86_get_sf (c) ? 'S' : '-',
		e86_get_zf (c) ? 'Z' : '-',
		e86_get_af (c) ? 'A' : '-',
		e86_get_pf (c) ? 'P' : '-',
		e86_get_cf (c) ? 'C' : '-'
	);

	if (e86_get_halt (c)) {
		pce_printf ("HALT=1\n");
	}
}

void pc_print_trace (e8086_t *c)
{
	e86_disasm_t op;
	char         str[256];

	e86_disasm_cur (c, &op);

	switch (op.arg_n) {
	case 0:
		strcpy (str, op.op);
		break;

	case 1:
		sprintf (str, "%-5s %s", op.op, op.arg1);
		break;

	case 2:
		sprintf (str, "%-5s %s,%s", op.op, op.arg1, op.arg2);
		break;

	default:
		strcpy (str, "****");
		break;
	}

	pce_printf (
		"AX=%04X BX=%04X CX=%04X DX=%04X "
		"SP=%04X BP=%04X SI=%04X DI=%04X "
		"DS=%04X ES=%04X SS=%04X F=%04X %04X:%04X %s\n",
		e86_get_ax (c), e86_get_bx (c), e86_get_cx (c), e86_get_dx (c),
		e86_get_sp (c), e86_get_bp (c), e86_get_si (c), e86_get_di (c),
		e86_get_ds (c),	e86_get_es (c), e86_get_ss (c), e86_get_flags (c),
		e86_get_cs (c), e86_get_ip (c), str
	);
}

static
void prt_state_mem (ibmpc_t *pc)
{
	pce_prt_sep ("PC MEM");
	mem_prt_state (pc->mem, stdout);
}

static
void prt_state_ports (ibmpc_t *pc)
{
	pce_prt_sep ("PC PORTS");
	mem_prt_state (pc->prt, stdout);
}

static
void prt_state_pc (ibmpc_t *pc)
{
	prt_state_video (pc->video);
	prt_state_ppi (&pc->ppi);
	prt_state_pit (&pc->pit);
	prt_state_pic (&pc->pic);
	prt_state_dma (&pc->dma);
	prt_state_cpu (pc->cpu);
}

static
void prt_state (ibmpc_t *pc)
{
	e86_disasm_t op;
	char         str[256];

	e86_disasm_cur (pc->cpu, &op);
	disasm_str (str, &op);

	prt_state_cpu (pc->cpu);

	pce_printf ("%04X:%04X  %s\n",
		(unsigned) e86_get_cs (pc->cpu),
		(unsigned) e86_get_ip (pc->cpu),
		str
	);
}

static
int pc_check_break (ibmpc_t *pc)
{
	unsigned short seg, ofs;

	seg = e86_get_cs (pc->cpu);
	ofs = e86_get_ip (pc->cpu);

	if (bps_check (&pc->bps, seg, ofs, stdout)) {
		return (1);
	}

	if (pc->brk) {
		return (1);
	}

	return (0);
}

static
void pc_exec (ibmpc_t *pc)
{
	unsigned old;

	pc->current_int &= 0xff;

	old = e86_get_opcnt (pc->cpu);

	while (e86_get_opcnt (pc->cpu) == old) {
		pc_clock (pc, 1);

		if (pc->brk) {
			break;
		}
	}
}

void pc_run (ibmpc_t *pc)
{
	pce_start (&pc->brk);

	pc_clock_discontinuity (pc);

	if (pc->pause == 0) {
		while (pc->brk == 0) {
			pc_clock (pc, 4 * pc->speed_current);
		}
	}
	else {
		while (pc->brk == 0) {
			pce_usleep (100000);
			trm_check (pc->trm);
		}
	}

	pc->current_int &= 0xff;

	pce_stop();
}

#if 0
static
void pce_op_stat (void *ext, unsigned char op1, unsigned char op2)
{
	ibmpc_t *pc;

	pc = (ibmpc_t *) ext;

}
#endif

static
void pce_op_int (void *ext, unsigned char n)
{
	unsigned seg;
	ibmpc_t  *pc;

	pc = ext;

	pc->current_int = n | 0x100;

	if (pc_intlog_check (pc, n)) {
		pce_printf ("%04X:%04X: int %02X"
			" [AX=%04X BX=%04X CX=%04X DX=%04X DS=%04X ES=%04X]\n",
			e86_get_cs (pc->cpu), e86_get_cur_ip (pc->cpu),
			n,
			e86_get_ax (pc->cpu), e86_get_bx (pc->cpu),
			e86_get_cx (pc->cpu), e86_get_dx (pc->cpu),
			e86_get_ds (pc->cpu), e86_get_es (pc->cpu)
		);
	}

	if (n == 0x13) {
		if (pc->dsk0 == NULL) {
			return;
		}

		if ((e86_get_ah (pc->cpu) != 0x02) || (e86_get_dl (pc->cpu) == 0)) {
			return;
		}

		dsks_add_disk (pc->dsk, pc->dsk0);

		pc->dsk0 = NULL;
	}
	else if (n == 0x19) {
		if ((pc->bootdrive != 0) && (pc->dsk0 == NULL)) {
			/* If we are not booting from drive 0 then
			 * temporarily remove it. */
			pc->dsk0 = dsks_get_disk (pc->dsk, 0);
			dsks_rmv_disk (pc->dsk, pc->dsk0);
		}

		if (pc->patch_bios_int19 == 0) {
			return;
		}

		if (pc->patch_bios_init) {
			return;
		}

		seg = pc_get_pcex_seg (pc);

		if (seg == 0) {
			return;
		}

		pc_log_deb ("patching int 19 (0x%04x)\n", seg);

		e86_set_mem16 (pc->cpu, 0, 4 * 0x19 + 0, 0x0010);
		e86_set_mem16 (pc->cpu, 0, 4 * 0x19 + 2, seg);
	}
}

static
void pce_op_undef (void *ext, unsigned char op1, unsigned char op2)
{
	ibmpc_t *pc;

	pc = (ibmpc_t *) ext;

	pce_log (MSG_DEB, "%04X:%04X: undefined operation [%02X %02x]\n",
		e86_get_cs (pc->cpu), e86_get_ip (pc->cpu), op1, op2
	);

	pce_usleep (100000UL);

	trm_check (pc->trm);
}


static
void pc_cmd_boot (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned short val;

	if (cmd_match_eol (cmd)) {
		pce_printf ("boot drive is 0x%02x\n", pc_get_bootdrive (pc));
		return;
	}

	if (!cmd_match_uint16 (cmd, &val)) {
		cmd_error (cmd, "expecting boot drive");
		return;
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	pc_set_bootdrive (pc, val);
}

static
void pc_cmd_c (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned long cnt;

	cnt = 1;

	cmd_match_uint32 (cmd, &cnt);

	if (!cmd_match_end (cmd)) {
		return;
	}

	pc_clock_discontinuity (pc);

	while (cnt > 0) {
		pc_clock (pc, 1);
		cnt -= 1;
	}

	prt_state (pc);
}

static
void pc_cmd_g_b (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned long seg, ofs;
	breakpoint_t  *bp;

	while (cmd_match_uint32 (cmd, &seg)) {
		if (cmd_match (cmd, ":")) {
			if (!cmd_match_uint32 (cmd, &ofs)) {
				cmd_error (cmd, "expecting offset");
				return;
			}

			bp = bp_segofs_new (seg, ofs);
		}
		else {
			bp = bp_addr_new (seg);
		}

		bps_bp_add (&pc->bps, bp);
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	pce_start (&pc->brk);

	pc_clock_discontinuity (pc);

	while (1) {
		if (pc->trace) {
			pc_print_trace (pc->cpu);
		}

		pc_exec (pc);

		if (pc_check_break (pc)) {
			break;
		}
	}

	pce_stop();
}

static
void pc_cmd_g_far (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned short seg;

	if (!cmd_match_end (cmd)) {
		return;
	}

	seg = e86_get_cs (pc->cpu);

	pce_start (&pc->brk);

	pc_clock_discontinuity (pc);

	while (1) {
		if (pc->trace) {
			pc_print_trace (pc->cpu);
		}

		pc_exec (pc);

		if (e86_get_cs (pc->cpu) != seg) {
			prt_state (pc);
			break;
		}

		if (pc_check_break (pc)) {
			break;
		}
	}

	pce_stop();
}

static
void pc_cmd_g (cmd_t *cmd, ibmpc_t *pc)
{
	if (cmd_match (cmd, "b")) {
		pc_cmd_g_b (cmd, pc);
	}
	else if (cmd_match (cmd, "far")) {
		pc_cmd_g_far (cmd, pc);
	}
	else {
		if (!cmd_match_end (cmd)) {
			return;
		}

		pc_run (pc);
	}
}

static
void pc_cmd_hm (cmd_t *cmd)
{
	pce_puts (
		"emu.config.save      <filename>\n"
		"emu.exit\n"
		"emu.stop\n"
		"emu.pause            \"0\" | \"1\"\n"
		"emu.pause.toggle\n"
		"emu.reset\n"
		"\n"
		"emu.cpu.model        \"8086\" | \"8088\" | \"80186\" | \"80188\"\n"
		"emu.cpu.speed        <factor>\n"
		"emu.cpu.speed.step   <adjustment>\n"
		"\n"
		"emu.disk.boot        <bootdrive>\n"
		"emu.fdc.accurate     \"0\" | \"1\"\n"
		"\n"
		"emu.parport.driver   <driver>\n"
		"emu.parport.file     <filename>\n"
		"\n"
		"emu.serport.driver   <driver>\n"
		"emu.serport.file     <filename>\n"
		"\n"
		"emu.tape.append\n"
		"emu.tape.file        <filename>\n"
		"emu.tape.load        [<position> | \"end\"]\n"
		"emu.tape.read        <filename>\n"
		"emu.tape.rewind\n"
		"emu.tape.save        [<position> | \"end\"]\n"
		"emu.tape.state\n"
		"emu.tape.write       <filename>\n"
		"\n"
		"emu.term.fullscreen  \"0\" | \"1\"\n"
		"emu.term.fullscreen.toggle\n"
		"emu.term.grab\n"
		"emu.term.release\n"
		"emu.term.screenshot  [<filename>]\n"
		"emu.term.title       <title>\n"
		"\n"
		"emu.video.blink      <blink-rate>\n"
		"emu.video.redraw     [\"now\"]\n"
		"\n"
	);

	msg_dsk_print_help();
}

static
void pc_cmd_i (cmd_t *cmd, ibmpc_t *pc)
{
	int            word;
	unsigned short port;

	if (cmd_match (cmd, "w")) {
		word = 1;
	}
	else if (cmd_match (cmd, "b")) {
		word = 0;
	}
	else {
		word = 0;
	}

	if (!cmd_match_uint16 (cmd, &port)) {
		cmd_error (cmd, "need a port address");
		return;
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	if (word) {
		pce_printf ("%04X: %04X\n", port, e86_get_prt16 (pc->cpu, port));
	}
	else {
		pce_printf ("%04X: %02X\n", port, e86_get_prt8 (pc->cpu, port));
	}
}

static
void pc_cmd_key (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned       i;
	unsigned       event;
	pce_key_t      key;
	char           str[256];

	while (cmd_match_str (cmd, str, 256)) {
		i = 0;

		event = PCE_KEY_EVENT_DOWN;

		if (str[0] == '+') {
			i += 1;
		}
		else if (str[0] == '-') {
			i += 1;
			event = PCE_KEY_EVENT_UP;
		}

		key = pce_key_from_string (str + i);

		if (key == PCE_KEY_NONE) {
			pce_printf ("unknown key: %s\n", str);
		}
		else {
			pce_printf ("key: %s%s\n",
				(event == PCE_KEY_EVENT_DOWN) ? "+" : "-",
				str + i
			);

			pc_kbd_set_key (&pc->kbd, event, key);
		}
	}

	if (!cmd_match_end (cmd)) {
		return;
	}
}

static
void pc_cmd_log_int_l (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned   i;
	const char *str;

	for (i = 0; i < 256; i++) {
		str = pc_intlog_get (pc, i);

		if (str != NULL) {
			pce_printf ("%02X: %s\n", i, str);
		}
	}
}

static
void pc_cmd_log_int (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned short n;
	char           buf[256];

	if (cmd_match_eol (cmd)) {
		pc_cmd_log_int_l (cmd, pc);
		return;
	}

	if (cmd_match (cmd, "l")) {
		pc_cmd_log_int_l (cmd, pc);
		return;
	}

	if (!cmd_match_uint16 (cmd, &n)) {
		cmd_error (cmd, "need an interrupt number");
		return;
	}

	if (cmd_match_eol (cmd)) {
		pc_intlog_set (pc, n, NULL);
		pce_printf ("%02X: <deleted>\n", n);
		return;
	}

	if (!cmd_match_str (cmd, buf, 256)) {
		cmd_error (cmd, "need an expression");
		return;
	}

	pce_printf ("%02X: %s\n", n, buf);

	pc_intlog_set (pc, n, buf);
}

static
void pc_cmd_log (cmd_t *cmd, ibmpc_t *pc)
{
	if (cmd_match (cmd, "int")) {
		pc_cmd_log_int (cmd, pc);
	}
	else {
		cmd_error (cmd, "log what?");
	}
}

static
void pc_cmd_o (cmd_t *cmd, ibmpc_t *pc)
{
	int            word;
	unsigned short port, val;

	if (cmd_match (cmd, "w")) {
		word = 1;
	}
	else if (cmd_match (cmd, "b")) {
		word = 0;
	}
	else {
		word = 0;
	}

	if (!cmd_match_uint16 (cmd, &port)) {
		cmd_error (cmd, "need a port address");
		return;
	}

	if (!cmd_match_uint16 (cmd, &val)) {
		cmd_error (cmd, "need a value");
		return;
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	if (word) {
		e86_set_prt16 (pc->cpu, port, val);
	}
	else {
		e86_set_prt8 (pc->cpu, port, val);
	}
}

static
void pc_cmd_pqc (cmd_t *cmd, ibmpc_t *pc)
{
	if (!cmd_match_end (cmd)) {
		return;
	}

	e86_pq_init (pc->cpu);
}

static
void pc_cmd_pqs (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned i;

	if (!cmd_match_end (cmd)) {
		return;
	}

	pce_puts ("PQ:");

	for (i = 0; i < pc->cpu->pq_cnt; i++) {
		pce_printf (" %02X", pc->cpu->pq[i]);
	}

	pce_puts ("\n");
}

static
void pc_cmd_pqf (cmd_t *cmd, ibmpc_t *pc)
{
	if (!cmd_match_end (cmd)) {
		return;
	}

	e86_pq_fill (pc->cpu);
}

static
void pc_cmd_pq (cmd_t *cmd, ibmpc_t *pc)
{
	if (cmd_match (cmd, "c")) {
		pc_cmd_pqc (cmd, pc);
	}
	else if (cmd_match (cmd, "f")) {
		pc_cmd_pqf (cmd, pc);
	}
	else if (cmd_match (cmd, "s")) {
		pc_cmd_pqs (cmd, pc);
	}
	else if (cmd_match_eol (cmd)) {
		pc_cmd_pqs (cmd, pc);
	}
	else {
		cmd_error (cmd, "pq: unknown command (%s)\n");
	}
}

static
void pc_cmd_p (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned       cnt, opcnt1, opcnt2;
	unsigned short seg, ofs, seg2, ofs2;
	unsigned long  i, n;
	int            brk, skip;
	e86_disasm_t   op;

	n = 1;

	cmd_match_uint32 (cmd, &n);

	if (!cmd_match_end (cmd)) {
		return;
	}

	brk = 0;

	pce_start (&pc->brk);

	pc->current_int &= 0xff;

	pc_clock_discontinuity (pc);

	for (i = 0; i < n; i++) {
		e86_disasm_cur (pc->cpu, &op);

		seg = e86_get_cs (pc->cpu);
		ofs = e86_get_ip (pc->cpu);

		cnt = pc->cpu->int_cnt;

		if (pc->trace) {
			pc_print_trace (pc->cpu);
		}

		while ((e86_get_cs (pc->cpu) == seg) && (e86_get_ip (pc->cpu) == ofs)) {
			pc_clock (pc, 1);

			if (pc_check_break (pc)) {
				brk = 1;
				break;
			}
		}

		if (brk) {
			break;
		}

		skip = 0;

		if (op.flags & (E86_DFLAGS_CALL | E86_DFLAGS_LOOP)) {
			seg2 = seg;
			ofs2 = ofs + op.dat_n;
			skip = 1;
		}
		else if (pc->cpu->int_cnt != cnt) {
			seg2 = pc->cpu->int_cs;
			ofs2 = pc->cpu->int_ip;
			skip = 1;
		}

		if (skip) {
			opcnt1 = -e86_get_opcnt (pc->cpu);

			while ((e86_get_cs (pc->cpu) != seg2) || (e86_get_ip (pc->cpu) != ofs2)) {
				if (pc->trace) {
					opcnt2 = e86_get_opcnt (pc->cpu);

					if (opcnt1 != opcnt2) {
						opcnt1 = opcnt2;
						pc_print_trace (pc->cpu);
					}
				}

				pc_clock (pc, 1);

				if (pc_check_break (pc)) {
					brk = 1;
					break;
				}
			}
		}

		if (brk) {
			break;
		}

		if (pc_check_break (pc)) {
			break;
		}
	}

	pce_stop();

	prt_state (pc);
}

static
void pc_cmd_r (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned long val;
	char          sym[256];

	if (cmd_match_eol (cmd)) {
		prt_state_cpu (pc->cpu);
		return;
	}

	if (!cmd_match_ident (cmd, sym, 256)) {
		pce_printf ("missing register\n");
		return;
	}

	if (e86_get_reg (pc->cpu, sym, &val)) {
		pce_printf ("bad register (%s)\n", sym);
		return;
	}

	if (cmd_match_eol (cmd)) {
		pce_printf ("%04lX\n", val);
		return;
	}

	if (!cmd_match_uint32 (cmd, &val)) {
		pce_printf ("missing value\n");
		return;
	}

	if (!cmd_match_end (cmd)) {
		return;
	}

	e86_set_reg (pc->cpu, sym, val);

	prt_state (pc);
}

static
void pc_cmd_s (cmd_t *cmd, ibmpc_t *pc)
{
	if (cmd_match_eol (cmd)) {
		prt_state (pc);
		return;
	}

	while (!cmd_match_eol (cmd)) {
		if (cmd_match (cmd, "pc")) {
			prt_state_pc (pc);
		}
		else if (cmd_match (cmd, "cpu")) {
			prt_state_cpu (pc->cpu);
		}
		else if (cmd_match (cmd, "dma")) {
			prt_state_dma (&pc->dma);
		}
		else if (cmd_match (cmd, "disks")) {
			dsks_print_info (pc->dsk);
		}
		else if (cmd_match (cmd, "ems")) {
			prt_state_ems (pc->ems);
		}
		else if (cmd_match (cmd, "mem")) {
			prt_state_mem (pc);
		}
		else if (cmd_match (cmd, "pic")) {
			prt_state_pic (&pc->pic);
		}
		else if (cmd_match (cmd, "pit")) {
			prt_state_pit (&pc->pit);
		}
		else if (cmd_match (cmd, "ppi")) {
			prt_state_ppi (&pc->ppi);
		}
		else if (cmd_match (cmd, "ports")) {
			prt_state_ports (pc);
		}
		else if (cmd_match (cmd, "uart")) {
			unsigned short i;
			if (!cmd_match_uint16 (cmd, &i)) {
				i = 0;
			}
			if ((i < 4) && (pc->serport[i] != NULL)) {
				prt_state_uart (
					&pc->serport[i]->uart,
					pc->serport[i]->io
				);
			}
		}
		else if (cmd_match (cmd, "video")) {
			prt_state_video (pc->video);
		}
		else if (cmd_match (cmd, "xms")) {
			prt_state_xms (pc->xms);
		}
		else {
			cmd_error (cmd, "unknown component (%s)\n");
			return;
		}
	}
}

static
void pc_cmd_trace (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned short v;

	if (cmd_match_eol (cmd)) {
		pce_printf ("trace is %s\n", pc->trace ? "on" : "off");
		return;
	}

	if (cmd_match (cmd, "on")) {
		pc->trace = 1;
	}
	else if (cmd_match (cmd, "off")) {
		pc->trace = 0;
	}
	else if (cmd_match_uint16 (cmd, &v)) {
		pc->trace = (v != 0);
	}
	else {
		cmd_error (cmd, "on or off expected\n");
	}
}

static
void pc_cmd_t (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned long i, n;

	n = 1;

	cmd_match_uint32 (cmd, &n);

	if (!cmd_match_end (cmd)) {
		return;
	}

	pce_start (&pc->brk);

	pc_clock_discontinuity (pc);

	for (i = 0; i < n; i++) {
		if (pc->trace) {
			pc_print_trace (pc->cpu);
		}

		pc_exec (pc);

		if (pc_check_break (pc)) {
			break;
		}
	}

	pce_stop();

	prt_state (pc);
}

static
void pc_cmd_u (cmd_t *cmd, ibmpc_t *pc)
{
	unsigned short        seg, ofs, cnt, mode;
	static unsigned int   first = 1;
	static unsigned short sseg = 0;
	static unsigned short sofs = 0;
	e86_disasm_t          op;
	char                  str[256];

	if (first) {
		first = 0;
		sseg = e86_get_cs (pc->cpu);
		sofs = e86_get_ip (pc->cpu);
	}

	seg = sseg;
	ofs = sofs;
	cnt = 16;
	mode = 0;

	if (cmd_match_uint16_16 (cmd, &seg, &ofs)) {
		cmd_match_uint16 (cmd, &cnt);
	}

	cmd_match_uint16 (cmd, &mode);

	if (!cmd_match_end (cmd)) {
		return;
	}

	while (cnt > 0) {
		e86_disasm_mem (pc->cpu, &op, seg, ofs);
		disasm_str (str, &op);

		pce_printf ("%04X:%04X  %s\n", seg, ofs, str);

		ofs = (ofs + op.dat_n) & 0xffff;

		if (mode == 0) {
			cnt -= 1;
		}
		else {
			cnt = (cnt < op.dat_n) ? 0 : (cnt - op.dat_n);
		}
	}

	sseg = seg;
	sofs = ofs;
}

int pc_cmd (ibmpc_t *pc, cmd_t *cmd)
{
	if (pc->trm != NULL) {
		trm_check (pc->trm);
	}

	if (cmd_match (cmd, "boot")) {
		pc_cmd_boot (cmd, pc);
	}
	else if (cmd_match (cmd, "b")) {
		cmd_do_b (cmd, &pc->bps);
	}
	else if (cmd_match (cmd, "c")) {
		pc_cmd_c (cmd, pc);
	}
	else if (cmd_match (cmd, "g")) {
		pc_cmd_g (cmd, pc);
	}
	else if (cmd_match (cmd, "hm")) {
		pc_cmd_hm (cmd);
	}
	else if (cmd_match (cmd, "i")) {
		pc_cmd_i (cmd, pc);
	}
	else if (cmd_match (cmd, "key")) {
		pc_cmd_key (cmd, pc);
	}
	else if (cmd_match (cmd, "log")) {
		pc_cmd_log (cmd, pc);
	}
	else if (cmd_match (cmd, "o")) {
		pc_cmd_o (cmd, pc);
	}
	else if (cmd_match (cmd, "pq")) {
		pc_cmd_pq (cmd, pc);
	}
	else if (cmd_match (cmd, "p")) {
		pc_cmd_p (cmd, pc);
	}
	else if (cmd_match (cmd, "r")) {
		pc_cmd_r (cmd, pc);
	}
	else if (cmd_match (cmd, "s")) {
		pc_cmd_s (cmd, pc);
	}
	else if (cmd_match (cmd, "trace")) {
		pc_cmd_trace (cmd, pc);
	}
	else if (cmd_match (cmd, "t")) {
		pc_cmd_t (cmd, pc);
	}
	else if (cmd_match (cmd, "u")) {
		pc_cmd_u (cmd, pc);
	}
	else {
		return (1);
	}

	return (0);
}

void pc_cmd_init (ibmpc_t *pc, monitor_t *mon)
{
	mon_cmd_add (mon, par_cmd, sizeof (par_cmd) / sizeof (par_cmd[0]));
	mon_cmd_add_bp (mon);

	pc->cpu->op_int = &pce_op_int;
	pc->cpu->op_undef = &pce_op_undef;
#if 0
	pc->cpu->op_stat = pce_op_stat;
#endif

}
