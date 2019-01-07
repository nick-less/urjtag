/*
 * $Id$
 *
 * Copyright (C) 2003 Matan Ziv-Av
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by Matan Ziv-Av, 2003.
 *
 */

#ifndef URJ_FLASH_JEDEC_H
#define URJ_FLASH_JEDEC_H

#include <urjtag/types.h>
#include <urjtag/flash.h>

/* Manufacturers */
#define MANUFACTURER_AMD        0x0001
#define MANUFACTURER_ATMEL      0x001F
#define MANUFACTURER_FUJITSU    0x0004
#define MANUFACTURER_ST         0x0020
#define MANUFACTURER_SST        0x00BF
#define MANUFACTURER_TOSHIBA    0x0098
#define MANUFACTURER_MX         0x00C2

/* AMD */
#define AM29F800BB      0x2258
#define AM29F800BT      0x22D6
#define AM29LV800BB     0x225B
#define AM29LV800BT     0x22DA
#define AM29LV400BT     0x22B9
#define AM29LV400BB     0x22BA
#define AM29LV160DT     0x22C4
#define AM29LV160DB     0x2249
#define AM29BDS323D     0x22D1
#define AM29BDS643D     0x227E
#define AM29LV081B      0x0038
#define AM29LV040B      0x004F

/* Atmel */
#define AT49xV16x       0x00C0
#define AT49xV16xT      0x00C2

/* Fujitsu */
#define MBM29LV160TE    0x22C4
#define MBM29LV160BE    0x2249
#define MBM29LV800BB    0x225B

/* ST - www.st.com */
#define M29W800T        0x00D7
#define M29W800B        0x005B
#define M29W160DT       0x22C4
#define M29W160DB       0x2249

/* SST */
#define SST39LF800      0x2781
#define SST39LF160      0x2782

/* Toshiba */
#define TC58FVT160      0x00C2
#define TC58FVB160      0x0043

/* MX */
#define MX29LV400T      0x22B9
#define MX29LV400B      0x22BA


struct mtd_erase_region_info
{
    uint32_t offset;            /* At which this region starts, from the beginning of the MTD */
    uint32_t erasesize;         /* For this region */
    uint32_t numblocks;         /* Number of blocks of erasesize in this region */
};

struct amd_flash_info
{
    const int mfr_id;
    const int dev_id;
    const char *name;
    const long size;
    const uint8_t interface_width;
    const int numeraseregions;
    const struct mtd_erase_region_info regions[4];
};


int urj_flash_jedec_detect (urj_bus_t *bus, uint32_t adr,
                            urj_flash_cfi_array_t **urj_flash_cfi_array);
#ifdef JEDEC_EXP
int urj_flash_jedec_exp_detect (urj_bus_t *bus, uint32_t adr,
                                urj_flash_cfi_array_t **urj_flash_cfi_array);
#endif

#endif /* ndef URJ_FLASH_JEDEC_H */
