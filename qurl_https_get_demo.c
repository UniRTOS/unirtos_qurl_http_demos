/*****************************************************************/ /**
* @file qurl_https_get_demo.c
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
#include "unirtos_app_init_registry.h"
#include "qosa_virtual_file.h"

#define QOS_LOG_TAG                    LOG_TAG

#define UNIR_HTTP_DEMO_TASK_STACK_SIZE 4096
#define UNIR_HTTP_DEMP_PDPID           1

#define QURL_HTTP_APP_MAX(a, b)        (((a) > (b)) ? (a) : (b))
#define QURL_HTTP_APP_MIN(a, b)        (((a) < (b)) ? (a) : (b))

//Here using /user/ directory, can be shared with qcm
#define QURL_APPS_HTTPS_FILENAME       "/user/https_test1.txt"

/**
 * @brief Define unir_fota_http_t structure type, used to store various states and configuration information during HTTP transmission.
 */
typedef struct
{
    qurl_core_t   http_hd;                      /*!< HTTP header core information. */
    int           http_mode;                    /*!< HTTP mode. */
    int           ssl_ctxid;                    /*!< SSL context ID. */
    char          file_name[QOSA_VFS_PATH_MAX]; /*!< File name to download or upload. */
    int           fd;                           /*!< File descriptor for file operations. */
    int           pdp_id;                       /*!< PDP context ID for mobile network data connection. */
    qosa_uint32_t time_out;                     /*!< Timeout duration, unit may be milliseconds. */
    qosa_uint32_t start_pos;                    /*!< Starting position for download or upload. */
    qosa_uint32_t dload_want_size;              /*!< Expected download size. */
    qosa_uint32_t resume_dload_count;           /*!< Resume count for handling restart after download interruption. */
    qosa_uint32_t fs_free_size;                 /*!< File system free space size. */
    int           event_errcode;                /*!< Event error code for recording errors during the process. */
    int           write_errcode;                /*!< Write error code for recording errors in write operations. */
    int           chunk_encode;                 /*!< Chunk encoding flag for handling HTTP chunked transfer. */
    int           first_flag;                   /*!< Flag indicating if it's the first transmission. */
    qosa_uint32_t write_size;                   /*!< Actual write size */
    qosa_uint32_t total_recv_cnt;               /*!< Current accumulated total write size */
    qosa_uint32_t need_save;                    /*!< Whether need to save to file */
} unir_demo_http_t;

static unir_demo_http_t *g_demo_https = QOSA_NULL;

/*===========================================================================
 *  Static API Functions
 ===========================================================================*/

/**
 * @brief Print HTTP response body content
 *
 * This function is used to print the data content and size of HTTP response body
 *
 * @param buf Pointer to the response body data buffer
 * @param size Size of the response body data (in bytes)
 *
 * @return No return value
 */
static void qurl_https_demo_print_body(char *buf, long size)
{
    QLOGD("size=%d,%s", size, buf);
}

/**
 * @brief Get available space size
 *
 * This function obtains the available space size by querying the root file system statistics.
 *
 * @param None
 *
 * @return Returns available space size (bytes), returns 0 if failed to get
 */
static qosa_int64_t qurl_https_demo_get_space_free_size(void)
{
    // Get file system free space
    qosa_int64_t              free_size = 0;
    qosa_int32_t              ret = 0;
    struct qosa_vfs_statvfs_t stat = {0};

    // Get root directory space size
    ret = qosa_vfs_statvfs("/", (struct qosa_vfs_statvfs_t *)&stat);
    if (ret < 0)
    {
        return 0;
    }
    free_size = (stat.f_bavail * stat.f_bsize);
    return free_size;
}

/**
 * @brief Write data to HTTP download file
 * @param http_ptr HTTP download control structure pointer
 * @param buf Data buffer to write
 * @param size Data size to write
 * @return Returns 0 on success, -1 on failure
 */
static qosa_int32_t qurl_https_demo_vfs_write(unir_demo_http_t *http_ptr, char *buf, long size)
{
    qosa_size_t temp_len = 0;
    int         ret = 0;

    // Loop write data until all written
    while (temp_len < size)
    {
        ret = qosa_vfs_write(http_ptr->fd, buf + temp_len, size - temp_len);
        // File write error handling
        if (ret <= 0)
        {
            qosa_vfs_close(http_ptr->fd);
            http_ptr->fd = 0;
            // If file write fails, no need to continue processing this time
            return -1;
        }
        temp_len += ret;
    }
    return 0;
}

/**
 * @brief Save HTTP response data to file
 *
 * This function handles data reception during HTTP download process and writes data to file system.
 * It determines whether download can continue based on response status code and content length,
 * and handles chunked encoding or fixed-length data streams.
 *
 * @param http_ptr HTTP context structure pointer, containing current download status information
 * @param buf      Received data buffer
 * @param size     Currently received data size (bytes)
 *
 * @return Actual bytes written to file; returns 0 if error occurs
 * @note This method of saving data in callback is only for demonstration, can be used,
 * but if downloading large files, this method is inefficient. Reasons:
 * 1: Single write file size is not fixed, not necessarily optimal length, write efficiency is low
 * 2. Writing file blocks HTTP download, if server sends fast, may cause frequent retransmissions
 * For large files, it's recommended to use custom task and watermark cache mechanism,
 * writing 4K data each time to improve file write efficiency
 */
static long qurl_https_demo_save_file(unir_demo_http_t *http_ptr, char *buf, long size)
{
    long         resp_code = 0;        // Response status code
    long         content_length = -1;  // Response content length
    qosa_bool_t  ret = 0;
    qosa_int64_t free_size = 0;

    // First time receiving body, can get body length at this time
    if (http_ptr->first_flag)
    {
        // Get response status code
        qurl_core_getinfo(http_ptr->http_hd, QURL_INFO_RESP_CODE, &resp_code);
        QLOGV("resp_code:[%d]\r\n", resp_code);

        // Get response content length
        qurl_core_getinfo(http_ptr->http_hd, QURL_INFO_RESP_CONTENT_LENGTH, &content_length);
        QLOGV("content_length:[%d]\r\n", content_length);

        // Set download method based on response status code and content length
        if (resp_code >= 200 && resp_code < 300)
        {
            if (content_length > 0)
            {
                // Content_length exists, known total body length, check if free space is enough to save all data
                if (content_length > http_ptr->fs_free_size)
                {
                    QLOGE("download file size large");
                    http_ptr->write_errcode = -1;
                    goto exit;
                }
                http_ptr->chunk_encode = 0;
                http_ptr->dload_want_size = content_length;
            }
            else
            {
                // Content_length doesn't exist, belongs to chunked encoding, total body length uncertain
                http_ptr->chunk_encode = 1;
                http_ptr->dload_want_size = 0;
            }
            QLOGD("resp_code=%d,content_length=%d", resp_code, content_length);
        }
        else
        {
            // Other resp_code means failure
            QLOGE("resp_code=%d", resp_code);
            goto exit;
        }
        QLOGV("%d", size);
        http_ptr->first_flag = 1;  // Mark that initial state check is completed
    }

    // If in chunked encoding mode, need to check each time if free space is enough for current data block
    if (http_ptr->chunk_encode == 1)
    {
        free_size = qurl_https_demo_get_space_free_size();
        if (size >= free_size)
        {
            // No space left, don't write file, recommend reserving a few KB space here to prevent system operation abnormalities
            return 0;
        }
    }

    // Write data to file system
    ret = qurl_https_demo_vfs_write(http_ptr, buf, size);
    if (ret != 0)
    {
        // Write error
        return 0;
    }

    // Update total received and written bytes
    http_ptr->total_recv_cnt += size;
    http_ptr->write_size += size;
    QLOGD("total_recv_cnt=%d", http_ptr->total_recv_cnt);
    return size;
exit:
    return 0;
}

static long qurl_https_app_w_cb(char *buf, long size, void *arg)
{
    unir_demo_http_t *http_ptr = QOSA_NULL;  // HTTP download parameter structure pointer
    long              ret = 0;

    // Get HTTP parameter structure pointer
    http_ptr = (unir_demo_http_t *)arg;
    if (http_ptr == QOSA_NULL)
    {
        return 0;
    }

    // Determine data processing method based on need_save flag
    if (http_ptr->need_save)
    {
        // Save data to file
        ret = qurl_https_demo_save_file(http_ptr, buf, size);
        return ret;
    }
    else
    {
        // Directly print HTTP response body content
        qurl_https_demo_print_body(buf, size);
    }

    // Continue to get body, return original length
    return size;
}

static qurl_ecode_t qurl_https_app_get(void)
{
    qurl_ecode_t   ret = QURL_OK;
    qurl_core_t    core = QOSA_NULL;
    qurl_tls_cfg_t tls_cfg = {0};

    qosa_memset(g_demo_https, 0, sizeof(unir_demo_http_t));
    qurl_global_init();
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
        goto exit;
    }

    g_demo_https->http_hd = core;
    ret = qurl_core_setopt(core, QURL_OPT_URL, "https://httpbin.org/get");
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
        goto exit;
    }

    // Necessary parameters
    // Set network ID (PDP context ID)
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // Set response body callback, receive body in this function, can get content_length in this function
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_https_app_w_cb);
    qurl_core_setopt(core, QURL_OPT_WRITE_CB_ARG, g_demo_https);
    // Initialize tls structure, default no certificate verification, disable sni
    qurl_tls_cfg_init(&tls_cfg);
    // If configuration needed, can manually modify tls_cfg
    tls_cfg.negotiate_timeout = 30;
    qurl_core_setopt(core, QURL_OPT_TLS_CFG, &tls_cfg);

    // Non-essential parameters
    // If port is default value, can be ignored
    qurl_core_setopt(core, QURL_OPT_PORT, 443);

    // Customize whether to save file system during testing
    g_demo_https->need_save = 1;
    // If need to save file, need to get free space size to prevent exceeding file system
    if (g_demo_https->need_save)
    {
        g_demo_https->fs_free_size = qurl_https_demo_get_space_free_size();
        qosa_memset(g_demo_https->file_name, 0, sizeof(g_demo_https->file_name));
        qosa_memcpy(g_demo_https->file_name, QURL_APPS_HTTPS_FILENAME, qosa_strlen(QURL_APPS_HTTPS_FILENAME));

        // Open file waiting for writing
        g_demo_https->fd = qosa_vfs_open(QURL_APPS_HTTPS_FILENAME, QOSA_VFS_O_CREAT | QOSA_VFS_O_RDWR);
        if (g_demo_https->fd < 0)
        {
            QLOGE("open dir error!!");
            goto exit;
        }
    }
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

exit:
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }
    // If malloc content is set in tls_cfg, need to actively release here
    if (g_demo_https->fd >= 0)
    {
        qosa_vfs_close(g_demo_https->fd);
        // After testing, can choose not to delete first, check if file content is correct, then delete file after checking
        //qosa_vfs_unlink(QURL_APPS_HTTP_FILENAME);
    }
    return QURL_OK;
}

static void unir_qurl_https_app_init(void *argv)
{
    qurl_ecode_t ret = QURL_OK;

    qosa_task_sleep_sec(10);
    // https get test
    ret = qurl_https_app_get();
    QLOGE("ret=%d", ret);
}

void unir_qurl_https_demo_init(void)
{
    int         err = 0;
    qosa_task_t http_task = QOSA_NULL;

    if (g_demo_https == QOSA_NULL)
    {
        g_demo_https = qosa_malloc(sizeof(unir_demo_http_t));
        if (g_demo_https == QOSA_NULL)
        {
            QLOGE("http demo malloc error");
            return;
        }
    }
    err = qosa_task_create(&http_task, UNIR_HTTP_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "https_demo", unir_qurl_https_app_init, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("task create error");
        return;
    }
}

// UNIRTOS_APP_EXPORT(363, "qurl_https_demo", unir_qurl_https_demo_init);