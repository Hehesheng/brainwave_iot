/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-05-31     Hehesheng    first version
 */

#include <board.h>
#include <rtdevice.h>
#include <rtthread.h>

#include <cJSON.h>
#include "onenet.h"

#include "app_config.h"
#include "onenet_service.h"

#define LOG_TAG "ONE_SER"  //该模块对应的标签。不定义时，默认：NO_TAG
#define LOG_LVL LOG_LVL_DBG  //该模块对应的日志输出级别。不定义时，默认：调试级别
#include <ulog.h>            //必须在 LOG_TAG 与 LOG_LVL 下面

#define ONENET_THREAD_STACK_SIZE (2048)
#define ONENET_THREAD_PRIORITY (15)

static void onenet_start_thread(void* par)
{
    rt_event_t events = RT_NULL;

    do
    {
        events = (rt_event_t)rt_object_find("net_event", RT_Object_Class_Event);
        rt_thread_delay(RT_TICK_PER_SECOND);
    } while (events == RT_NULL);
    rt_event_recv(events, EVENT_NET_OK, RT_EVENT_FLAG_OR, RT_WAITING_FOREVER, RT_NULL);

    onenet_mqtt_init();

    rt_thread_delay(RT_TICK_PER_SECOND);

    onenet_mqtt_upload_string("test_2", "this is test string");

    log_d("Current Device ID: %s", ONENET_INFO_DEVID);
}

static int onenet_service_start(void)
{
    rt_thread_t tid = RT_NULL;

    tid = rt_thread_create("tONE_START", onenet_start_thread, RT_NULL, ONENET_THREAD_STACK_SIZE,
                           ONENET_THREAD_PRIORITY, 20);
    /* 创建线程 */
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }

    return 0;
}
INIT_APP_EXPORT(onenet_service_start);

static int cjson_big_test(int argc, char** argv)
{
    cJSON* monitor = RT_NULL;
    cJSON* arr     = RT_NULL;
    cJSON* num     = RT_NULL;
    char* str      = RT_NULL;

    monitor = cJSON_CreateObject();
    arr     = cJSON_AddArrayToObject(monitor, "arr");

    for (int i = 0; i < atoi(argv[1]); i++)
    {
        num = cJSON_CreateNumber(i);
        cJSON_AddItemToArray(arr, num);
    }

    str = cJSON_Print(monitor);
    
    cJSON_Delete(monitor);
    rt_free(str);

    return 0;
}
MSH_CMD_EXPORT(cjson_big_test, NONE);
