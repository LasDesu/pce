/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ata.h"

/* fake pci-ata symbol */
typedef void * pci_ata_t;
ata_chn_t *pci_ata_get_chn (pci_ata_t *dev, unsigned i)
{
	return NULL;
}
