/*****************************************************************/ /**
* @file qurl_http_auth_demo.c
* @brief
* @author harry.li@quectel.com
* @date 2025-08-18
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2025-05-7 <td>1.0 <td>harry.li <td> Init
* </table>
**********************************************************************/

#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qurl.h"

#define QOS_LOG_TAG                    LOG_TAG

#define UNIR_HTTP_DEMO_TASK_STACK_SIZE 4096
#define UNIR_HTTP_DEMP_PDPID           1

static long qurl_http_app_w_cb(char *buf, long size, void *arg)
{
    QOSA_UNUSED(arg);

    QLOGD("size=%d,%s", size, buf);
    return size;
}

/**
 * @brief Initialize HTTP Authentication Application
 * @details This function demonstrates how to use the qurl library for HTTP basic authentication requests
 * @param None
 * @return qurl_ecode_t Returns QURL_OK for success, other values indicate failure
 */
static void qurl_http_app_auth(void)
{
    qurl_ecode_t ret = QURL_OK;
    qurl_core_t  core = QOSA_NULL;

    // Initialize global qurl environment
    qurl_global_init();
    /* Create qurl core handle */
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("%x", ret);
    }

    // Set network ID (PDP context ID)
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // Configure as HTTP GET method
    qurl_core_setopt(core, QURL_OPT_HTTP_GET, 1L);
    /* Authentication */
    /* Set target URL, using httpbin.org's basic-auth test interface */
    ret = qurl_core_setopt(core, QURL_OPT_URL, "http://httpbin.org/basic-auth/test/123");
    if (ret != QURL_OK)
    {
        QLOGE("%x", ret);
    }

    /* Configure HTTP basic authentication parameters */
    // Configure authentication method as no authentication and basic authentication, HTTP will first attempt no authentication connection, if failed will then try basic authentication
    //ret = qurl_core_setopt(core, QURL_OPT_HTTP_AUTH, QURL_HTTP_AUTH_ONLY | QURL_HTTP_AUTH_BASIC);
    // Configure basic authentication
    ret = qurl_core_setopt(core, QURL_OPT_HTTP_AUTH, QURL_HTTP_AUTH_BASIC);
    /* Configure username */
    ret = qurl_core_setopt(core, QURL_OPT_USERNAME, "test");
    /* Configure password */
    ret = qurl_core_setopt(core, QURL_OPT_PASSWORD, "123");

    /* Set response header callback function */
    ret = qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);

    /* Execute HTTP request */
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x", ret);
    }

    /* Release qurl core handle resources */
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x", ret);
    }

    return;
}

static void qurl_http_app_auth_init(void *argv)
{
    qosa_task_sleep_sec(10);
    qurl_http_app_auth();
}

void unir_qurl_http_auth_demo_init(void)
{
    int         err = 0;
    qosa_task_t http_task = QOSA_NULL;

    err = qosa_task_create(&http_task, UNIR_HTTP_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "http_auth_demo", qurl_http_app_auth_init, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("task create error");
        return;
    }
}
