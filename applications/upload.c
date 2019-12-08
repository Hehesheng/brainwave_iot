/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-05-24     Hehesheng    first version
 */

#include <board.h>
#include <netdb.h>
#include <onenet.h>
#include <rtdevice.h>
#include <rtthread.h>
#include <sys/socket.h>

#include "optparse.h"

#include "app_config.h"
#include "hmi.h"

#define LOG_TAG "UPLOAD"  //该模块对应的标签。不定义时，默认：NO_TAG
#define LOG_LVL LOG_LVL_DBG  //该模块对应的日志输出级别。不定义时，默认：调试级别
#include <ulog.h>            //必须在 LOG_TAG 与 LOG_LVL 下面

#define THREAD_STACK_SIZE (4096)
#define THREAD_PRIORITY (13)

#define UPLOAD_MAILBOX_DEEPTH (16)

#define BUFSZ (1024)
#define DEFAULT_IP "192.168.43.84"
#define DEFAULT_PORT "9999"

#define ONENET_STREAM_NAME "data_pack"

static base_struct *tmp_upload = RT_NULL;
static rt_mailbox_t upload_mb  = RT_NULL;
static rt_event_t events       = RT_NULL;

static int sock         = 0;
static char url[16]     = {DEFAULT_IP};
static char port_num[6] = {DEFAULT_PORT};

static void upload_thread(void *ptr)
{
    int ret;
    struct hostent *host;
    struct sockaddr_in server_addr;
    int port;
    char *send_data = RT_NULL;

    /* 链接开始前 */
    while (1)
    {
        if (rt_event_recv(events, EVENT_NET_OK, RT_EVENT_FLAG_OR, 0, RT_NULL) != RT_EOK)
        {
            rt_thread_delay(RT_TICK_PER_SECOND);
        }
        else
        {
            if (rt_event_recv(events, EVENT_UPLOAD_OK, RT_EVENT_FLAG_OR, 0, RT_NULL) == RT_EOK)
            {
                log_w("upload thread have been created.");
                return;
            }
            break;
        }
    }

    port = strtoul(port_num, 0, 10);

    /* 通过函数入口参数url获得host地址（如果是域名，会做域名解析） */
    host = gethostbyname(url);

    log_d("\nConnect INFO:\nIP: %s\nPORT: %d", url, port);

    /* 创建一个socket，类型是SOCKET_STREAM，TCP类型 */
    {
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        {
            /* 创建socket失败 */
            log_e("Socket error");

            goto end;
        }

        /* 初始化预连接的服务端地址 */
        server_addr.sin_family = AF_INET;
        server_addr.sin_port   = htons(port);
        server_addr.sin_addr   = *((struct in_addr *)host->h_addr);
        rt_memset(&(server_addr.sin_zero), 0, sizeof(server_addr.sin_zero));

        /* 连接到服务端 */
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1)
        {
            /* 连接失败 */
            log_w("Connect fail!");
            closesocket(sock);

            goto end;
        }
        else
        {
            /* 标记启动事件 */
            rt_event_send(events, EVENT_UPLOAD_OK);
            /* 连接成功 */
            log_i("Connect successful");
        }
    }

    while (1)
    {
        rt_mb_recv(upload_mb, (rt_ubase_t *)&tmp_upload, RT_WAITING_FOREVER);

        if (tmp_upload->create_monitor != RT_NULL)
        {
            send_data = tmp_upload->create_monitor((void *)tmp_upload);
        }
        if (tmp_upload->free != RT_NULL)
        {
            tmp_upload->free((void *)tmp_upload);
        }
        /* 发送数据到sock连接 */
        if (send_data != RT_NULL)
        {
            ret = send(sock, send_data, rt_strlen(send_data), 0);
            rt_free((void *)send_data);
            send_data = RT_NULL;
        }

        if (ret < 0)
        {
            /* 接收失败，关闭这个连接 */
            closesocket(sock);
            log_e("send error,close the socket.");

            break;
        }
        else if (ret == 0)
        {
            /* 打印send函数返回值为0的警告信息 */
            log_w("Send warning,send function return 0.");
        }
    }

end:
    /* 退出线程清除事件 */
    rt_event_recv(events, EVENT_UPLOAD_OK, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, RT_NULL);
}

static int upload_component_create(void)
{
    /* 创建邮箱 */
    upload_mb = rt_mb_create("mUPLOAD", UPLOAD_MAILBOX_DEEPTH, RT_IPC_FLAG_FIFO);
    RT_ASSERT(upload_mb != RT_NULL);

    return 0;
}
INIT_COMPONENT_EXPORT(upload_component_create);

static int upload_app_create(void)
{
    /* 检查是否可以启动上传服务 */
    events = (rt_event_t)rt_object_find("net_event", RT_Object_Class_Event);
    RT_ASSERT(events != RT_NULL);

    rt_thread_t tid =
        rt_thread_create("tUPLOAD", upload_thread, RT_NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, 20);
    /* 创建线程 */
    if (tid != RT_NULL)
    {
        /* 启动线程 */
        rt_thread_startup(tid);
    }

    return 0;
}
// INIT_APP_EXPORT(upload_app_create);

static const struct optparse_long longopts[] = {{"ip", 'i', OPTPARSE_REQUIRED},
                                                {"port", 'p', OPTPARSE_REQUIRED},
                                                {"help", 'h', OPTPARSE_NONE},
                                                {"stop", 's', OPTPARSE_NONE},
                                                {0}};

static void upload_begin(int argc, char **argv)
{
    /* 命令行分析 */
    {
        int option;
        struct optparse options;
        int option_index;
        rt_thread_t tid = RT_NULL;

        optparse_init(&options, argv);
        while ((option = optparse_long(&options, longopts, &option_index)) != -1)
        {
            switch (option)
            {
                case 'i':
                    rt_memset(url, 0, rt_strlen(url));
                    rt_strncpy(url, options.optarg, rt_strlen(options.optarg));
                    break;
                case 'p':
                    rt_memset(port_num, 0, rt_strlen(port_num));
                    rt_strncpy(port_num, options.optarg, rt_strlen(options.optarg));
                    break;
                case '?':
                    rt_kprintf("%s\n", options.errmsg);
                case 'h':
                    return;
                case 's':
                    tid = rt_thread_find("tUPLOAD");
                    if (tid != RT_NULL)
                    {
                        rt_thread_delete(tid);
                        closesocket(sock);
                        /* 退出线程清除事件 */
                        rt_event_recv(events, EVENT_UPLOAD_OK,
                                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, RT_NULL);
                        log_d("Socket close: %d", sock);
                    }
                    return;
            }
        }
    }
    upload_app_create();
}
MSH_CMD_EXPORT(upload_begin, upload task begin);

/* OneNET Thread */
static void onenet_send_entry(void *param)
{
    int ret;
    char *send_data = RT_NULL;

    /* 链接开始前 */
    while (1)
    {
        if (rt_event_recv(events, EVENT_NET_OK, RT_EVENT_FLAG_OR, 0, RT_NULL) != RT_EOK)
        {
            rt_thread_delay(RT_TICK_PER_SECOND);
        }
        else
        {
            if (rt_event_recv(events, EVENT_UPLOAD_OK, RT_EVENT_FLAG_OR, 0, RT_NULL) == RT_EOK)
            {
                log_w("onenet thread have been created.");
                return;
            }
            break;
        }
    }
    hmi_send("main.debug", "txt", "\"onenet opened\"");

    rt_event_send(events, EVENT_UPLOAD_OK);

    while (1)
    {
        rt_mb_recv(upload_mb, (rt_ubase_t *)&tmp_upload, RT_WAITING_FOREVER);

        if (tmp_upload->create_monitor != RT_NULL)
        {
            send_data = tmp_upload->create_monitor((void *)tmp_upload);
        }
        /* 发送数据到OneNET */
        if (send_data != RT_NULL)
        {
            ret = onenet_mqtt_upload_string(tmp_upload->stream_name, send_data);
            if (ret != 0)
            {
                log_w("OneNET Error: %d", ret);
                // break;
            }
            rt_free((void *)send_data);
            send_data = RT_NULL;
        }
        if (tmp_upload->free != RT_NULL)
        {
            tmp_upload->free((void *)tmp_upload);
        }
    }

    rt_event_recv(events, EVENT_UPLOAD_OK, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, RT_NULL);
}

static int onenet_app_create(void)
{
    /* 检查是否可以启动上传服务 */
    events = (rt_event_t)rt_object_find("net_event", RT_Object_Class_Event);
    RT_ASSERT(events != RT_NULL);

    rt_thread_t tid = rt_thread_create("tOneNET", onenet_send_entry, RT_NULL, THREAD_STACK_SIZE,
                                       THREAD_PRIORITY, 20);
    /* 创建线程 */
    if (tid != RT_NULL)
    {
        /* 启动线程 */
        rt_thread_startup(tid);
    }

    return 0;
}

static void onenet_begin(int argc, char **argv)
{
    /* 命令行分析 */
    {
        int option;
        struct optparse options;
        int option_index;
        rt_thread_t tid = RT_NULL;

        optparse_init(&options, argv);
        while ((option = optparse_long(&options, longopts, &option_index)) != -1)
        {
            switch (option)
            {
                case 'i':
                case '?':
                case 'h':
                    return;
                case 's':
                    tid = rt_thread_find("tOneNET");
                    if (tid != RT_NULL)
                    {
                        rt_thread_delete(tid);
                        /* 退出线程清除事件 */
                        rt_event_recv(events, EVENT_UPLOAD_OK,
                                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, 0, RT_NULL);
                        hmi_send("main.debug", "txt", "\"onenet closed\"");
                        log_d("OneNET close.");
                    }
                    return;
            }
        }
    }
    onenet_app_create();
}
MSH_CMD_EXPORT(onenet_begin, onenet task begin);

static int onenetIsOnline(int argc, char **argv)
{
    if (rt_thread_find("tOneNET") != NULL)
    {
        hmi_send("onenet_state", "val", "1");
    }
    else
    {
        hmi_send("onenet_state", "val", "0");
    }

    return 0;
}
MSH_CMD_EXPORT(onenetIsOnline, NONE);

static int onenetList(int argc, char **argv)
{
    rt_mailbox_t mail = RT_NULL;
    rt_event_t e = RT_NULL;

    mail = (rt_mailbox_t)rt_object_find("mUPLOAD", RT_Object_Class_MailBox);

    if (mail == NULL) return -1;
    hmi_send("list", "txt", "\"%d/%d\"", mail->entry, mail->size);

    e = (rt_event_t)rt_object_find("net_event", RT_Object_Class_Event);
    if (e->set & EVENT_TGAM_ONLINE)
    {
        hmi_send("tgam_state", "val", "1");
    }
    else
    {
        hmi_send("tgam_state", "val", "0");
    }

    return 0;
}
MSH_CMD_EXPORT(onenetList, NONE);
