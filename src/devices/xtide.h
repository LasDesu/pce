/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/


#ifndef PCE_DEVICES_XTIDE_H
#define PCE_DEVICES_XTIDE 1


#include <stdio.h>

#include <devices/memory.h>
#include <devices/ata.h>


typedef struct {
	ata_chn_t	ata;
	mem_blk_t   mem;
	
	unsigned rdbuf, wrbuf;	
} xtide_t;



#endif
