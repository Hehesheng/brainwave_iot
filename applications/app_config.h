/*
 * Copyright (c) 2018-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-06-19     Hehesheng    add config file
 */
#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#define EVENT_WLAN_OK (1 << 0)
#define EVENT_NET_OK (1 << 1)
#define EVENT_UPLOAD_OK (1 << 2)
#define EVENT_TGAM_ONLINE (1 << 3)

#define HMI_DEVICE_SERIAL_NAME "uart2"
#define TGAM_DEVICE_SERIAL "uart3"

#define TGAM_ONENET_STREAM_NAME "tgam_pack"
#define AD59_ONENET_STREAM_NAME "ad59_pack"

typedef struct base_struct
{
    char *stream_name;
    unsigned int tick;
    char *(*create_monitor)(void *data);
    void (*free)(void *data);
} base_struct;

#endif  // __APP_CONFIG_H__
