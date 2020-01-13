#ifndef __HMI_H__
#define __HMI_H__

#include <board.h>
#include <rtdevice.h>
#include <rtthread.h>

#define ALLOW_SPLIT_CHARS "/,="

enum hmi_device_status
{
    HMI_WAIT,
    HMI_REC,
    HMI_SUSPEND,
};

typedef struct hmi_device
{
    struct rt_device parent;
    rt_device_t serial;
    enum hmi_device_status stat;
    struct rt_semaphore count;
    char rx_buff[FINSH_CMD_SIZE];
    uint32_t rx_len;
    char tx_buff[FINSH_CMD_SIZE];
    uint32_t tx_len;
} hmi_device;

int hmi_send(char *name, char *attribute, char *format, ...);

#endif  // __HMI_H__
