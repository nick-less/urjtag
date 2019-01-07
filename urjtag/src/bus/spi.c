/*
 * $Id: spi.c 1729 2010-01-24 11:31:51Z jgstroud $
 *
 * Copyright (C) 2002, 2003 ETC s.r.o.
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
 * Written by Jon Stroud <jstroud@breakingpoint.com>, 2010.
 *
 * Patch Note: 
 * it seems the SPI driver was originally written for a SPI RAM
 * the SPI bus assumes memory read/write via cmd 0x03 and 0x02, so the readem command works, but 
 * writemem will not correctly program SPI flash devices, you should use flashmem instead
 *
 */

#include "sysdep.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "part.h"
#include "bus.h"
#include "chain.h"
#include "bssignal.h"
#include "jtag.h"
#include "buses.h"
#include "generic_bus.h"

typedef struct {
    urj_part_signal_t *cs;
    urj_part_signal_t *sck;
    urj_part_signal_t *mosi;
    urj_part_signal_t *miso;
    int csa;
    int ashift;
    int dshift;
} bus_params_t;

#define CS      ((bus_params_t *) bus->params)->cs
#define SCK     ((bus_params_t *) bus->params)->sck
#define MOSI    ((bus_params_t *) bus->params)->mosi
#define MISO    ((bus_params_t *) bus->params)->miso

#define CSA     ((bus_params_t *) bus->params)->csa
#define ASHIFT  ((bus_params_t *) bus->params)->ashift
#define DSHIFT  ((bus_params_t *) bus->params)->dshift


static urj_bus_t *
spi_bus_new (urj_chain_t *chain, const urj_bus_driver_t *driver,
                   const urj_param_t *cmd_params[])
{
    urj_bus_t *bus;
    urj_part_signal_t *sig;
    int i;
    int failed = 0;
    const char *value;

    bus = urj_bus_generic_new (chain, driver, sizeof (bus_params_t));
    if (bus == NULL)
        return NULL;

    CS = SCK = MOSI = MISO = NULL;
    ASHIFT = 2;
    DSHIFT = 0;
    for (i = 0; cmd_params[i] != NULL; i++)
    {

        if (cmd_params[i]->key == URJ_BUS_PARAM_KEY_AMODE)
        {
            switch (cmd_params[i]->value.lu)
            {
            case 8:
                ASHIFT = 0;
                break;
            case 16:
                ASHIFT = 1;
                break;
            case 32:
                ASHIFT = 3;
                break;
            case 0:
                ASHIFT = 1;
                break;

            default:
                urj_error_set (URJ_ERROR_INVALID,
                        _("value %lu not defined for parameter %s"),
                        cmd_params[i]->value.lu,
                        urj_param_string(&urj_bus_param_list,
                        cmd_params[i]));
                failed = 1;     // @@@@ RFHH
                break;
            }

        }
        else if (cmd_params[i]->key == URJ_BUS_PARAM_KEY_WIDTH)
        {
            switch (cmd_params[i]->value.lu)
            {
            case 8:
                DSHIFT = 0;
                break;
            case 16:
                DSHIFT = 1;
                break;
            case 32:
                DSHIFT = 3;
                break;
            case 0: 
                DSHIFT = 0;
                break;

            default:
                urj_error_set (URJ_ERROR_INVALID,
                        _("value %lu not defined for parameter %s"),
                        cmd_params[i]->value.lu,
                        urj_param_string(&urj_bus_param_list,
                        cmd_params[i]));
                failed = 1;     // @@@@ RFHH
                break;
            }
        }
        else
        {
            if (cmd_params[i]->type != URJ_PARAM_TYPE_STRING)
            {
                urj_error_set (URJ_ERROR_SYNTAX,
                               "parameter must be of type string");
                failed = 1;
                continue;
            }

            value = cmd_params[i]->value.string;

            sig = urj_part_find_signal (bus->part, value);
            if (!sig)
            {
                urj_error_set (URJ_ERROR_NOTFOUND, _("signal '%s' not found"),
                               value);
                failed = 1;
                continue;
            }

            switch (cmd_params[i]->key)
            {
            case URJ_BUS_PARAM_KEY_SCK:
                SCK = sig;
                break;
            case URJ_BUS_PARAM_KEY_CS:
            case URJ_BUS_PARAM_KEY_NCS:
                CS = sig;
                CSA = (cmd_params[i]->key == URJ_BUS_PARAM_KEY_CS);
                break;
            case URJ_BUS_PARAM_KEY_MOSI:
                MOSI = sig;
                break;
            case URJ_BUS_PARAM_KEY_MISO:
                MISO = sig;
                break;
            default:
                urj_error_set (URJ_ERROR_INVALID, _("parameter %s is unknown"),
                               urj_param_string(&urj_bus_param_list,
                               cmd_params[i]));
                failed = 1;
                break;
            }
        }
    }

    if (!CS) 
    {
        CS = urj_part_find_signal (bus->part, "CSN");
        if (!CS) 
        {
            urj_error_set (URJ_ERROR_INVALID, _("signal '%s' not found\n"), "NCS");
            failed = 1;
        }
    }
    if (!SCK) 
    {
        SCK = urj_part_find_signal (bus->part, "SCK");
        if (!SCK) 
        {
            urj_error_set (URJ_ERROR_INVALID, _("signal '%s' not found\n"), "SCK");
            failed = 1;
        }
    }
    if (!MOSI) 
    {
        MOSI = urj_part_find_signal (bus->part, "MOSI");
        if (!MOSI) 
        {
            urj_error_set (URJ_ERROR_INVALID, _("signal '%s' not found\n"), "MOSI");
            failed = 1;
        }
    }
    if (!MISO) 
    {
        MISO = urj_part_find_signal (bus->part, "MISO");
        if (!MISO) 
        {
            urj_error_set (URJ_ERROR_INVALID, _("signal '%s' not found\n"), "MISO");
            failed = 1;
        }
    }
    if (failed)
    {
        urj_bus_generic_free (bus);
        return NULL;
    }

    return bus;
}


/**
 * bus->driver->(*printinfo)
 *
 */
static void
spi_bus_printinfo (urj_log_level_t ll, urj_bus_t *bus)
{
    int i;

    for (i = 0; i < bus->chain->parts->len; i++)
        if (bus->part == bus->chain->parts->parts[i])
            break;
    urj_log (ll, _("SPI Flash bus driver via BSR (JTAG part No. %d)\n"), i);
}

/**
 * bus->driver->(*area)
 *
 */
static int
spi_bus_area (urj_bus_t *bus, uint32_t adr, urj_bus_area_t *area)
{
    uint64_t asize = (uint64_t)((ASHIFT + 1) * 8);
    asize = (uint64_t)1 << asize;
    if (adr < asize) 
    {
        area->description = "SPI";
        area->start = UINT32_C (0x00000000);
        area->length = (uint64_t)asize;
        area->width = (DSHIFT + 1) * 8;
    } 
    else 
    {
        area->description = "NONE";
        area->start = asize;
        area->length = (uint64_t)(0x100000000 - asize);
        area->width = 0;
    }
    return URJ_STATUS_OK;
}

static void
spi_write_byte (urj_bus_t *bus, uint8_t data)
{
    urj_part_t *p = bus->part;
    urj_chain_t *chain = bus->chain;
    int32_t i;
    for (i = 7; i >= 0; i--) 
    {
        urj_part_set_signal (p, MOSI, 1, (data & (1 << i) ? 1 : 0));
        urj_part_set_signal (p, SCK, 1, 0);
        urj_tap_chain_shift_data_registers (chain, 0);
        urj_part_set_signal (p, SCK, 1, 1);
        urj_tap_chain_shift_data_registers (chain, 0);
    }
}

static uint8_t
spi_read_byte (urj_bus_t *bus)
{
    urj_part_t *p = bus->part;
    urj_chain_t *chain = bus->chain;
    uint8_t data = 0;

    int8_t i;
    for (i = 7; i >= 0; i--) 
    {
        urj_part_set_signal (p, MOSI, 1, 0);
        urj_part_set_signal (p, SCK, 1, 0);
        urj_tap_chain_shift_data_registers (chain, 0);
        urj_part_set_signal (p, SCK, 1, 1);
        urj_tap_chain_shift_data_registers (chain, 1);
        data <<= 1;
        data |= (uint32_t)urj_part_get_signal (p, MISO);
    }
    return data;
}

/**
 * bus->driver->(*write_start)
 *
 */
static int
spi_bus_write_start (urj_bus_t *bus, uint32_t adr)
{
    int i;
    urj_part_t *p = bus->part;
    urj_chain_t *chain = bus->chain;

    urj_part_set_signal (p, CS, 1, CSA);
    urj_part_set_signal (p, MOSI, 1, 0);
    urj_part_set_signal (p, SCK, 1, 0);
    urj_part_set_signal (p, MISO, 0, 0);
    urj_tap_chain_shift_data_registers (chain, 0);

    spi_write_byte (bus, 0x02); //write command
    for (i = ASHIFT; i >= 0; i--) {
        spi_write_byte(bus, (uint8_t)(adr >> (i * 8)));
    }

    return URJ_STATUS_OK;
}

/**
 * bus->driver->(*read_start)
 *
 */
static int
spi_bus_read_start (urj_bus_t *bus, uint32_t adr)
{
    int i;
    urj_part_t *p = bus->part;
    urj_chain_t *chain = bus->chain;

    urj_part_set_signal (p, CS, 1, CSA);
    urj_part_set_signal (p, MOSI, 1, 0);
    urj_part_set_signal (p, SCK, 1, 0);
    urj_part_set_signal (p, MISO, 0, 0);
    urj_tap_chain_shift_data_registers (chain, 0);

    spi_write_byte (bus, 0x03); //read command
    for (i = ASHIFT; i >= 0; i--) 
    {
        spi_write_byte (bus, (uint8_t)(adr >> (i * 8)));
    }

    return URJ_STATUS_OK;
}

/**
 * bus->driver->(*read_next)
 *
 */
static uint32_t
spi_bus_read_next (urj_bus_t *bus, uint32_t adr)
{
    uint32_t data = 0;
    uint8_t temp;
    int i;

    for (i = 0; i <= DSHIFT; i++) 
    {
        temp = spi_read_byte (bus);
        data |= (temp << (i * 8));
    }
    return data;

}

/**
 * bus->driver->(*read_end)
 *
 */
static uint32_t
spi_bus_read_end (urj_bus_t *bus)
{
    urj_part_t *p = bus->part;
    urj_chain_t *chain = bus->chain;

    uint32_t d = 0;
    d = spi_bus_read_next (bus, 0);
    urj_part_set_signal (p, CS, 1, !CSA);
    urj_tap_chain_shift_data_registers (chain, 0);
    return d;
}

/**
 * bus->driver->(*write)
 *
 */
static void
spi_bus_write (urj_bus_t *bus, uint32_t adr, uint32_t data)
{
    int i;
    for (i = 0; i <= DSHIFT; i++) 
    {
        spi_write_byte (bus, data);
        data >>= 8;
    }
}

/**
 * bus->driver->(*enable)
 *
 */
static int
spi_bus_enable (urj_bus_t *bus)
{
    urj_part_t *p = bus->part;

    urj_part_set_signal (p, CS, 1, CSA);
    bus->enabled = 1;
    return URJ_STATUS_OK;
}

/**
 * bus->driver->(*disable)
 *
 */
static int
spi_bus_disable (urj_bus_t *bus)
{
    urj_part_t *p = bus->part;
    urj_chain_t *chain = bus->chain;

    urj_part_set_signal (p, CS, 1, !CSA);
    urj_tap_chain_shift_data_registers (chain, 0);
    bus->enabled = 0;
    return URJ_STATUS_OK;
}

/**
 * bus->driver->(*free)
 *
 */
static void
spi_bus_free (urj_bus_t *bus)
{
    urj_part_set_instruction (bus->part, "BYPASS");
    urj_tap_chain_shift_instructions (bus->chain);

    urj_bus_generic_free (bus);
}

const urj_bus_driver_t urj_bus_spi_bus = {
    "spi",
    N_("SPI driver via BSR, requires parameters:\n"
       "           NCS=<CS#>|CS=<CS> SCK=<SCK> MISO=<MISO> MOSI=<MOSI> [AMODE=8|16|32] [WIDTH=8|16|32]"),
    spi_bus_new,
    spi_bus_free,
    spi_bus_printinfo,
    urj_bus_generic_prepare_extest,
    spi_bus_area,
    spi_bus_read_start,
    spi_bus_read_next,
    spi_bus_read_end,
    urj_bus_generic_read,
    spi_bus_write_start,
    spi_bus_write,
    urj_bus_generic_no_init,
    spi_bus_enable,
    spi_bus_disable,
    URJ_BUS_TYPE_SPI,
};
