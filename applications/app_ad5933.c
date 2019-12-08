/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-06-19     Hehesheng    first version
 */

#include "app_config.h"
#include "cJSON.h"
#include "drv_ad5933.h"
#include "optparse.h"

#include <math.h>
#include "hmi.h"

#define DBG_LEVEL DBG_LOG
#define DBG_SECTION_NAME "AD59.app"
#include <rtdbg.h>

#define STANDARD_R 200000
#define GAIN_NUM 5.15819E-10

typedef struct ad5933_upload
{
    base_struct parent;
    uint16_t start;
    uint16_t end;
    int len;
    int16_t *real;
    int16_t *image;
    double *res;
    double ave;
    double height;
    double weight;
} ad5933_upload;

static rt_device_t serial     = RT_NULL;
static rt_mailbox_t upload_mb = RT_NULL;
static rt_event_t events      = RT_NULL;

// Partition by pivot
int partition(double *array, int start, int end)
{
    int pivot = array[end];
    int i     = start - 1;
    int temp  = 0;
    for (int j = start; j < end; j++)
    {
        if (array[j] <= pivot)
        {
            i++;
            temp     = array[i];
            array[i] = array[j];
            array[j] = temp;
        }
    }
    temp         = array[i + 1];
    array[i + 1] = array[end];
    array[end]   = temp;

    return i + 1;
}

void quick_sort(double *array, int start, int end)
{
    if (start < end)
    {
        int q = partition(array, start, end);
        quick_sort(array, start, q - 1);
        quick_sort(array, q + 1, end);
    }
}

static char *ad5933_create_monitor(void *data)
{
    ad5933_upload *upload = (ad5933_upload *)data;
    char *string          = NULL;
    cJSON *arr            = NULL;
    cJSON *one_point      = NULL;

    cJSON *monitor = cJSON_CreateObject();

    if (monitor == NULL)
    {
        goto end;
    }
    /* 时间轴 */
    cJSON_AddNumberToObject(monitor, "tick", upload->parent.tick);
    /* 包类型为AD5933 */
    cJSON_AddStringToObject(monitor, "type", "AD5933");
    cJSON_AddNumberToObject(monitor, "start", upload->start);
    cJSON_AddNumberToObject(monitor, "end", upload->end);
    cJSON_AddNumberToObject(monitor, "len", upload->len);

    arr = cJSON_AddArrayToObject(monitor, "real");
    for (int i = 0; i < upload->len; i++)
    {
        one_point = cJSON_CreateNumber(upload->real[i]);
        cJSON_AddItemToArray(arr, one_point);
    }

    arr = cJSON_AddArrayToObject(monitor, "image");
    for (int i = 0; i < upload->len; i++)
    {
        one_point = cJSON_CreateNumber(upload->image[i]);
        cJSON_AddItemToArray(arr, one_point);
    }
    cJSON_AddNumberToObject(monitor, "ave", upload->ave);
    cJSON_AddNumberToObject(monitor, "weight", upload->weight);
    cJSON_AddNumberToObject(monitor, "height", upload->height);

    string = cJSON_Print(monitor);
    if (string == NULL)
    {
        rt_kprintf("Failed to print monitor.\n");
    }

end:
    cJSON_Delete(monitor);
    return string;
}

static void ad5933_free(void *upload_data)
{
    ad5933_upload *upload = (ad5933_upload *)upload_data;
    rt_free((void *)upload->real);
    rt_free((void *)upload->image);
    rt_free(upload->res);
    rt_free((void *)upload);
}

static const struct optparse_long longopts[] = {{"start", 's', OPTPARSE_REQUIRED},
                                                {"end", 'e', OPTPARSE_REQUIRED},
                                                {"weight", 'w', OPTPARSE_REQUIRED},
                                                {"height", 'l', OPTPARSE_REQUIRED},
                                                {"point", 'p', OPTPARSE_REQUIRED},
                                                {"help", 'h', OPTPARSE_NONE},
                                                {"display", 'd', OPTPARSE_NONE},
                                                {"hmi", 'c', OPTPARSE_NONE},
                                                {0}};

static void display_usage(void)
{
    rt_kprintf("\n");
    rt_kprintf("ad59_run\n");
    rt_kprintf("-h --help: Command help.\n");
    rt_kprintf("-s --start: Start freq. <0-500000>\n");
    rt_kprintf("-e --end: End freq. <0-500000>\n");
    rt_kprintf("-p --point: Number of points. <1-511>\n");
    rt_kprintf("-d --display: Disable print result to console.\n");
    rt_kprintf("-c --hmi: Disable print result to hmi.\n");
    rt_kprintf("Example: ad59_run -s 300 -e 30000 -p 500\n");
    rt_kprintf("Default: start: 1000, end: 3000, points: 10\n");
}

static void ad5933_send_to_hmi(rt_device_t dev, ad5933_upload *msg)
{
    rt_kprintf("What Fuck Send Anythings.\n");
}

static void ad5933_run(int argc, char **argv)
{
    uint8_t status, display = 1, hmi = 1;
    uint16_t points     = 100;
    uint32_t start_freq = 300000, end_freq = 310000;
    ad5933_upload *upload = RT_NULL;
    double weight         = 0;
    double height         = 0;

    /* 命令行分析 */
    {
        int option;
        struct optparse options;
        int option_index;

        optparse_init(&options, argv);
        while ((option = optparse_long(&options, longopts, &option_index)) != -1)
        {
            switch (option)
            {
                case 's':
                    start_freq = atoi(options.optarg);
                    break;
                case 'e':
                    end_freq = atoi(options.optarg);
                    break;
                case 'p':
                    points = atoi(options.optarg);
                    break;
                case 'c':
                    hmi = 0;
                case 'd':
                    display = 0;
                    break;
                case 'w':
                    weight = atof(options.optarg);
                    break;
                case 'l':
                    height = atof(options.optarg);
                    break;
                case '?':
                    rt_kprintf("%s\n", options.errmsg);
                case 'h':
                    display_usage();
                    return;
            }
        }
        /* 检查参数 */
        if (start_freq > end_freq || points > 511 || end_freq > 5000000)
        {
            rt_kprintf("\nError Arguments.\n");
            display_usage();
            return;
        }
    }
    /* 申请内存池及挂载 */
    {
        upload = (ad5933_upload *)rt_malloc(sizeof(ad5933_upload));
        if (upload != RT_NULL)
        {
            rt_memset(upload, 0, sizeof(ad5933_upload));
            upload->real  = (int16_t *)rt_malloc(points * sizeof(int16_t));
            upload->image = (int16_t *)rt_malloc(points * sizeof(int16_t));
            upload->res   = (double *)rt_malloc(points * sizeof(double));
        }
        if (upload == RT_NULL || upload->real == RT_NULL || upload->image == RT_NULL)
        {
            log_w("Mem alloc error.");
            return;
        }
        rt_memset(upload->real, 0, points * sizeof(int16_t));
        rt_memset(upload->image, 0, points * sizeof(int16_t));

        upload->parent.stream_name    = AD59_ONENET_STREAM_NAME;
        upload->parent.create_monitor = ad5933_create_monitor;
        upload->parent.free           = ad5933_free;
        upload->parent.tick           = rt_tick_get();
        upload->start                 = start_freq;
        upload->end                   = end_freq;
    }

    if (ad5933_start(start_freq, end_freq, points) != RT_EOK)
    {
        log_w("AD5933 start fail.");
        rt_free(upload->real);
        rt_free(upload->image);
        rt_free(upload);
        return;
    }

    for (int i = 0; i < points + 10;)
    {
        /* 过程中提醒 */
        status = ad5933_get_status();
        if (status & AD5933_STATUS_DATA_OK)
        {
            if ((i % 50 == 0) && (i != 0))
            {
                rt_kprintf("The run point %d.\n", i);
            }
            if (ad5933_get_fft_res(upload->real + i, upload->image + i, 300) == -RT_ERROR)
            {
                log_e("ad5933 get fft %d points fail.", i);
                break;
            }
            i++;
            ad5933_scan_next();
            if (status & AD5933_STATUS_SCAN_OK)
            {
                if (points != i)
                {
                    log_w("points != i");
                }
                upload->len = i;

                rt_kprintf("this time have got %d points, the status: %X\n", i, status);
                break;
            }
        }
        rt_thread_delay(1);
    }

    // /* 输出至HMI */
    // if (hmi != 0)
    // {
    //     ad5933_send_to_hmi(serial, upload);
    // }
    /* 输出至命令行 */
    if (display != 0)
    {
        double ave = 0;

        for (int i = 0; i < upload->len; i++)
        {
            upload->res[i] =
                sqrt(pow((double)upload->real[i], 2) + pow((double)upload->image[i], 2));
            rt_kprintf("[%3d]: Real: %8d; Img: %8d; len: %8d\n", i, upload->real[i],
                       upload->image[i], (int)upload->res[i]);
        }
        quick_sort(upload->res, 1, upload->len - 1);
        ave = 1 / GAIN_NUM / upload->res[0];
        for (int i = upload->len / 2 - upload->len / 4; i < upload->len / 2 + upload->len / 4; i++)
        {
            ave = (ave + 1 / GAIN_NUM / upload->res[i]) / 2;
        }
        rt_kprintf("Ave: %d.%03d\n", (int)ave, (int)((ave - (int)ave) * 1000));
        hmi_send("resistance", "txt", "\"%d.%03d\"", (int)ave, (int)((ave - (int)ave) * 1000));
        upload->ave = ave;
    }
    upload->weight = weight;
    upload->height = height;
    /* 上传 */
    if (rt_event_recv(events, EVENT_UPLOAD_OK, RT_EVENT_FLAG_OR, 0, RT_NULL) == RT_EOK)
    {
        if (rt_mb_send(upload_mb, (rt_base_t)upload) != RT_EOK)
        {
            ad5933_free((void *)upload);
            log_w("UPLOAD mailbox send fail!!!");
        }
    }
    else
    {
        ad5933_free((void *)upload);
    }
}
MSH_CMD_EXPORT_ALIAS(ad5933_run, ad59_run, <ad59_run - h> get help);

static int app_ad5933_init(void)
{
    // /* 打开串口 */
    // serial = rt_device_find(HMI_DEVICE_SERIAL_NAME);
    // if (serial != RT_NULL)
    // {
    //     /* 以读写及中断接收方式打开串口设备 */
    //     rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    // }
    /* 获取上传给上层的信箱 */
    upload_mb = (rt_mailbox_t)rt_object_find("mUPLOAD", RT_Object_Class_MailBox);
    RT_ASSERT(upload_mb != RT_NULL);
    /* 获取事件 */
    events = (rt_event_t)rt_object_find("net_event", RT_Object_Class_Event);
    RT_ASSERT(events != RT_NULL);

    return 0;
}
INIT_APP_EXPORT(app_ad5933_init);
