/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:     src/ibmpc/xms.c                                            *
 * Created:       2003-09-01 by Hampa Hug <hampa@hampa.ch>                   *
 * Last modified: 2003-09-02 by Hampa Hug <hampa@hampa.ch>                   *
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

/* $Id: xms.c,v 1.2 2003/09/02 14:56:25 hampa Exp $ */


#include "pce.h"


xms_emb_t *emb_new (unsigned long size)
{
  xms_emb_t *emb;

  emb = (xms_emb_t *) malloc (sizeof (xms_emb_t));
  if (emb == NULL) {
    return (NULL);
  }

  if (size > 0) {
    emb->data = (unsigned char *) malloc (size);
    if (emb->data == NULL) {
      free (emb);
      return (NULL);
    }
  }
  else {
    emb->data = NULL;
  }

  emb->size = size;
  emb->lock = 0;

  return (emb);
}

void emb_del (xms_emb_t *emb)
{
  if (emb != NULL) {
    free (emb->data);
    free (emb);
  }
}

xms_t *xms_new (unsigned long emb_size, unsigned long umb_size, unsigned long umb_segm)
{
  xms_t *xms;

  umb_size = umb_size / 16;
  umb_segm = (umb_segm + 15) & ~0x0f;

  xms = (xms_t *) malloc (sizeof (xms_t));
  if (xms == NULL) {
    return (NULL);
  }

  xms->emb_cnt = 0;
  xms->emb = NULL;
  xms->emb_used = 0;
  xms->emb_max = emb_size;

  if (umb_size > 0) {
    xms->umbmem = mem_blk_new (16UL * umb_segm, 16UL * umb_size, 1);
    xms->umbmem->ext = xms;
    mem_blk_init (xms->umbmem, 0x00);
  }
  else {
    xms->umbmem = NULL;
  }

  xms->umb_used = 0;
  xms->umb_max = umb_size;

  xms->umb_cnt = 1;
  xms->umb = (xms_umb_t *) malloc (sizeof (xms_umb_t));
  xms->umb[0].segm = umb_segm;
  xms->umb[0].size = umb_size;
  xms->umb[0].alloc = 0;

  pce_log (MSG_INF, "xms:\tEMB=%lu[%luM] UMB=%lu[%luK] at 0x%04x\n",
    emb_size, emb_size / (1024 * 1024),
    16UL * umb_size, umb_size / 64, (unsigned) umb_segm
  );

  return (xms);
}

void xms_del (xms_t *xms)
{
  unsigned i;

  if (xms != NULL) {
    for (i = 0; i < xms->emb_cnt; i++) {
      emb_del (xms->emb[i]);
    }

    mem_blk_del (xms->umbmem);
    free (xms->umb);

    free (xms);
  }
}

xms_emb_t *xms_get_emb (xms_t *xms, unsigned handle)
{
  if ((handle > 0) && (handle <= xms->emb_cnt)) {
    return (xms->emb[handle - 1]);
  }

  return (NULL);
}

int xms_set_emb (xms_t *xms, xms_emb_t *emb, unsigned handle)
{
  if (handle == 0) {
    return (1);
  }

  if (handle > xms->emb_cnt) {
    unsigned i;

    i = xms->emb_cnt;

    xms->emb_cnt = handle;
    xms->emb = (xms_emb_t **) realloc (xms->emb, xms->emb_cnt * sizeof (xms_emb_t));

    while (i < xms->emb_cnt) {
      xms->emb[i] = NULL;
      i += 1;
    }
  }

  xms->emb[handle - 1] = emb;

  return (0);
}

int umb_split (xms_t *xms, unsigned idx, unsigned short size)
{
  unsigned j;

  if (idx >= xms->umb_cnt) {
    return (1);
  }

  if (xms->umb[idx].size <= size) {
    return (1);
  }

  xms->umb_cnt += 1;
  xms->umb = (xms_umb_t *) realloc (xms->umb, xms->umb_cnt * sizeof (xms_umb_t));

  for (j = xms->umb_cnt - 1; j > idx; j--) {
    xms->umb[j] = xms->umb[j - 1];
  }

  xms->umb[idx + 1].segm = xms->umb[idx].segm + size;
  xms->umb[idx + 1].size = xms->umb[idx].size - size;
  xms->umb[idx + 1].alloc = 0;

  xms->umb[idx].size = size;

  return (0);
}

void umb_fix (xms_t *xms)
{
  unsigned i, j;

  i = 1;
  j = 1;

  while (i < xms->umb_cnt) {
    if ((xms->umb[i - 1].alloc == 0) && (xms->umb[i].alloc == 0)) {
      xms->umb[i - 1].size += xms->umb[i].size;
    }
    else if (i != j) {
      xms->umb[j] = xms->umb[i];
      j += 1;
    }

    i += 1;
  }

  if (j < xms->umb_cnt) {
    xms->umb_cnt = j;
    xms->umb = (xms_umb_t *) realloc (xms->umb, xms->umb_cnt * sizeof (xms_umb_t));
  }
}

unsigned short umb_get_max (xms_t *xms)
{
  unsigned       i;
  unsigned short max;

  max = 0;

  for (i = 0; i < xms->umb_cnt; i++) {
    if ((xms->umb[i].alloc == 0) && (xms->umb[i].size > max)) {
      max = xms->umb[i].size;
    }
  }

  return (max);
}

int xms_alloc_emb (xms_t *xms, unsigned long size, unsigned *handle)
{
  unsigned  i;
  xms_emb_t *emb;

  if (size > (xms->emb_max - xms->emb_used)) {
    return (1);
  }

  i = 0;
  while ((i < xms->emb_cnt) && (xms->emb[i] != NULL)) {
    i += 1;
  }

  i += 1;

  *handle = i;

  emb = emb_new (size);
  xms_set_emb (xms, emb, i);

  xms->emb_used += size;

  return (0);
}

int xms_free_emb (xms_t *xms, unsigned handle)
{
  if ((handle == 0) || (handle > xms->emb_cnt)) {
    return (1);
  }

  if (xms->emb[handle - 1] != NULL) {
    xms->emb_used -= xms->emb[handle - 1]->size;
  }

  emb_del (xms->emb[handle - 1]);

  xms->emb[handle - 1] = NULL;

  return (0);
}

int xms_alloc_umb (xms_t *xms, unsigned short size, unsigned *idx)
{
  unsigned i;

  for (i = 0; i < xms->umb_cnt; i++) {
    if ((xms->umb[i].alloc == 0) && (xms->umb[i].size >= size)) {
      if (xms->umb[i].size > size) {
        umb_split (xms, i, size);
      }

      xms->umb[i].alloc = 1;

      *idx = i;

      return (0);
    }
  }

  return (1);
}

int xms_free_umb (xms_t *xms, unsigned short segm)
{
  unsigned i;

  for (i = 0; i < xms->umb_cnt; i++) {
    if (xms->umb[i].segm == segm) {
      if (xms->umb[i].alloc == 0) {
        return (1);
      }

      xms->umb[i].alloc = 0;
      umb_fix (xms);

      return (0);
    }
  }

  return (1);
}


/* 00: get driver version */
void xms_00 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, 0x0300);
  e86_set_bx (cpu, 0x0000);
  e86_set_dx (cpu, 0x0000);
}

/* 01: request high memory area */
void xms_01 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, 0x0000);
  e86_set_bl (cpu, 0x90);
}

/* 02: release high memory area */
void xms_02 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, 0x0000);
  e86_set_bl (cpu, 0x90);
}

/* 03: global enable a20 */
void xms_03 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, 0x0001);
}

/* 04: global disable a20 */
void xms_04 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, 0x0001);
  e86_set_bl (cpu, 0x94);
}

/* 05: local enable a20 */
void xms_05 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, 0x0001);
}

/* 06: local disable a20 */
void xms_06 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, 0x0001);
  e86_set_bl (cpu, 0x94);
}

/* 07: query a20 */
void xms_07 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, 0x0001);
  e86_set_bl (cpu, 0x00);
}

/* 08: query free extended memory */
void xms_08 (xms_t *xms, e8086_t *cpu)
{
  e86_set_ax (cpu, (xms->emb_max - xms->emb_used) / 1024);
  e86_set_dx (cpu, (xms->emb_max - xms->emb_used) / 1024);
  e86_set_bl (cpu, 0x00);
}

/* 09: allocate extended memory block */
void xms_09 (xms_t *xms, e8086_t *cpu)
{
  unsigned      handle;
  unsigned long size;

  size = 1024UL * e86_get_dx (cpu);

  if (size > (xms->emb_max - xms->emb_used)) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa0);
    return;
  }

  if (xms_alloc_emb (xms, size, &handle)) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa1);
    return;
  }

  e86_set_ax (cpu, 0x0001);
  e86_set_dx (cpu, handle);
  e86_set_bl (cpu, 0x00);
}

/* 0A: free extended memory block */
void xms_0a (xms_t *xms, e8086_t *cpu)
{
  unsigned handle;

  handle = e86_get_dx (cpu);

  if (xms_free_emb (xms, handle)) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa2);
    return;
  }

  e86_set_ax (cpu, 0x0001);
  e86_set_bl (cpu, 0x00);
}

static
void inc_addr (unsigned short *seg, unsigned short *ofs, unsigned short val)
{
  *ofs += val;

  *seg += *ofs >> 4;
  *ofs &= 0x000f;
}

/* 0B: move extended memory */
void xms_0b (xms_t *xms, e8086_t *cpu)
{
  unsigned long  i;
  unsigned short seg1, ofs1, seg2, ofs2;
  xms_emb_t      *src, *dst;
  unsigned long  cnt;
  unsigned       srch, dsth;
  unsigned long  srca, dsta;

  seg1 = e86_get_ds (cpu);
  ofs1 = e86_get_si (cpu);

  cnt = e86_get_mem16 (cpu, seg1, ofs1);
  cnt += e86_get_mem16 (cpu, seg1, ofs1 + 2) << 16;
  srch = e86_get_mem16 (cpu, seg1, ofs1 + 4);
  srca = e86_get_mem16 (cpu, seg1, ofs1 + 6);
  srca += e86_get_mem16 (cpu, seg1, ofs1 + 8) << 16;
  dsth = e86_get_mem16 (cpu, seg1, ofs1 + 10);
  dsta = e86_get_mem16 (cpu, seg1, ofs1 + 12);
  dsta += e86_get_mem16 (cpu, seg1, ofs1 + 14) << 16;

  pce_log (MSG_DEB, "xms: move %04x:%08lx -> %04x:%08lx %04lx\n",
    srch, srca, dsth, dsta, cnt
  );

  if ((srch == 0) && (dsth == 0)) {
    seg1 = (srca >> 16) & 0xffff;
    ofs1 = srca & 0xffff;
    seg2 = (dsta >> 16) & 0xffff;
    ofs2 = dsta & 0xffff;

    for (i = 0; i < cnt; i++) {
      e86_set_mem8 (cpu, seg2, ofs2, e86_get_mem8 (cpu, seg1, ofs1));
      inc_addr (&seg1, &ofs1, 1);
      inc_addr (&seg2, &ofs2, 1);
    }
  }
  else if ((srch != 0) && (dsth == 0)) {
    src = xms_get_emb (xms, srch);

    if (src == NULL) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa3);
      return;
    }

    if (srca >= src->size) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa4);
      return;
    }

    if ((srca + cnt) > src->size) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa7);
    }

    seg2 = (dsta >> 16) & 0xffff;
    ofs2 = dsta & 0xffff;

    for (i = 0; i < cnt; i++) {
      e86_set_mem8 (cpu, seg2, ofs2, src->data[srca + i]);
      inc_addr (&seg2, &ofs2, 1);
    }
  }
  else if ((srch == 0) && (dsth != 0)) {
    dst = xms_get_emb (xms, dsth);

    if (dst == NULL) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa5);
      return;
    }

    if (dsta >= dst->size) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa6);
      return;
    }

    if ((dsta + cnt) > dst->size) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa7);
      return;
    }

    seg1 = (srca >> 16) & 0xffff;
    ofs1 = srca & 0xffff;

    for (i = 0; i < cnt; i++) {
      dst->data[dsta + i] = e86_get_mem8 (cpu, seg1, ofs1);
      inc_addr (&seg1, &ofs1, 1);
    }
  }
  else {
    src = xms_get_emb (xms, srch);

    if (src == NULL) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa3);
      return;
    }

    if (srca >= src->size) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa4);
      return;
    }

    if ((srca + cnt) > src->size) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa7);
    }

    dst = xms_get_emb (xms, dsth);

    if (dst == NULL) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa5);
      return;
    }

    if (dsta >= dst->size) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa6);
      return;
    }

    if ((dsta + cnt) > dst->size) {
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0xa7);
    }

    memcpy (dst->data, src->data, cnt);
  }

  e86_set_ax (cpu, 0x001);
  e86_set_bl (cpu, 0x00);
}

/* 0C: lock extended memory block */
void xms_0c (xms_t *xms, e8086_t *cpu)
{
  unsigned  handle;
  xms_emb_t *blk;

  handle = e86_get_dx (cpu);

  blk = xms_get_emb (xms, handle);

  if (blk == NULL) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa2);
    return;
  }

  blk->lock += 1;

  e86_set_ax (cpu, 0x0001);
  e86_set_bx (cpu, 0x0000);
  e86_set_dx (cpu, 0x0001);
}

/* 0D: unlock extended memory block */
void xms_0d (xms_t *xms, e8086_t *cpu)
{
  unsigned  handle;
  xms_emb_t *blk;

  handle = e86_get_dx (cpu);

  blk = xms_get_emb (xms, handle);

  if (blk == NULL) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa2);
    return;
  }

  if (blk->lock == 0) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xaa);
    return;
  }

  blk->lock -= 1;

  e86_set_ax (cpu, 0x0001);
}

/* 0E: get emb handle information */
void xms_0e (xms_t *xms, e8086_t *cpu)
{
  unsigned  handle;
  xms_emb_t *blk;

  handle = e86_get_dx (cpu);
  blk = xms_get_emb (xms, handle);

  if (blk == NULL) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa2);
    return;
  }

  e86_set_ax (cpu, 0x0001);
  e86_set_bh (cpu, blk->lock);
  e86_set_bl (cpu, 255);
  e86_set_dx (cpu, blk->size / 1024);
}

/* 0F: reallocate emb */
void xms_0f (xms_t *xms, e8086_t *cpu)
{
  unsigned      handle;
  unsigned long size;
  unsigned char *tmp;
  xms_emb_t     *blk;

  size = 1024UL * e86_get_bx (cpu);

  if (size > (xms->emb_max - xms->emb_used)) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa0);
    return;
  }

  handle = e86_get_dx (cpu);
  blk = xms_get_emb (xms, handle);

  if (blk == NULL) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa2);
    return;
  }

  tmp = (unsigned char *) realloc (blk->data, size);

  if (tmp == NULL) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0xa0);
    return;
  }

  xms->emb_used += size;
  xms->emb_used -= blk->size;

  blk->data = tmp;
  blk->size = size;

  e86_set_ax (cpu, 0x0001);
}

/* 10: request umb */
void xms_10 (xms_t *xms, e8086_t *cpu)
{
  unsigned       idx;
  unsigned short size;

  size = e86_get_dx (cpu);

  if (xms_alloc_umb (xms, size, &idx)) {
    unsigned short max;

    e86_set_ax (cpu, 0x0000);
    max = umb_get_max (xms);
    e86_set_dx (cpu, max);
    e86_set_bl (cpu, (max == 0) ? 0xb1 : 0xb0);

    return;
  }

  e86_set_ax (cpu, 0x0001);
  e86_set_dx (cpu, xms->umb[idx].size);
  e86_set_bx (cpu, xms->umb[idx].segm);
}

/* 11: release umb */
void xms_11 (xms_t *xms, e8086_t *cpu)
{
  unsigned short segm;

  segm = e86_get_dx (cpu);

  if (xms_free_umb (xms, segm)) {
    e86_set_ax (cpu, 0x0001);
    e86_set_bl (cpu, 0xb2);
  }

  e86_set_ax (cpu, 0x0001);
}

mem_blk_t *xms_get_umb_mem (xms_t *xms)
{
  return (xms->umbmem);
}

void xms_info (xms_t *xms, e8086_t *cpu)
{
  if (xms != NULL) {
    e86_set_ax (cpu, 0x0001);
    e86_set_cx (cpu, xms->umb_max);
    e86_set_dx (cpu, xms->emb_max / 1024UL);
  }
  else {
    e86_set_ax (cpu, 0x0000);
  }
}

void xms_handler (xms_t *xms, e8086_t *cpu)
{
  if (xms == NULL) {
    e86_set_ax (cpu, 0x0000);
    e86_set_bl (cpu, 0x80);
  }

//  pce_log (MSG_DEB, "xms: AH=%02X\n", e86_get_ah (cpu));

  switch (e86_get_ah (cpu)) {
    case 0x00:
      xms_00 (xms, cpu);
      break;

    case 0x01:
      xms_01 (xms, cpu);
      break;

    case 0x02:
      xms_02 (xms, cpu);
      break;

    case 0x03:
      xms_03 (xms, cpu);
      break;

    case 0x04:
      xms_04 (xms, cpu);
      break;

    case 0x05:
      xms_05 (xms, cpu);
      break;

    case 0x06:
      xms_06 (xms, cpu);
      break;

    case 0x07:
      xms_07 (xms, cpu);
      break;

    case 0x08:
      xms_08 (xms, cpu);
      break;

    case 0x09:
      xms_09 (xms, cpu);
      break;

    case 0x0a:
      xms_0a (xms, cpu);
      break;

    case 0x0b:
      xms_0b (xms, cpu);
      break;

    case 0x0c:
      xms_0c (xms, cpu);
      break;

    case 0x0d:
      xms_0d (xms, cpu);
      break;

    case 0x0e:
      xms_0e (xms, cpu);
      break;

    case 0x0f:
      xms_0f (xms, cpu);
      break;

    case 0x10:
      xms_10 (xms, cpu);
      break;

    case 0x11:
      xms_11 (xms, cpu);
      break;

    default:
      e86_set_ax (cpu, 0x0000);
      e86_set_bl (cpu, 0x80);
      break;
  }
}
