/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-05-18     Hehesheng    first version
 */

#ifndef __TGAM_H__
#define __TGAM_H__

#include <board.h>
#include <rtdevice.h>
#include <rtthread.h>

#include "app_config.h"

#define TGAM_USING_DMA

typedef struct tgam_pack
{
    uint8_t sign;
    uint32_t detal;
    uint32_t theta;
    uint32_t low_alpha;
    uint32_t high_alpha;
    uint32_t low_beta;
    uint32_t high_beta;
    uint32_t low_gamma;
    uint32_t middle_gamma;
    uint8_t attention;
    uint8_t relex;
} tgam_pack;

typedef struct tgam_raw
{
    int len;
    int16_t *raw;
} tgam_raw;

typedef struct tgam_upload
{
    base_struct parent;
    tgam_raw *raw_data;
    tgam_pack *pack_data;
} tgam_upload;

#endif  // __TGAM_H__
