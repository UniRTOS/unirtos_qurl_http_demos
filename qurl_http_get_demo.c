/*****************************************************************/ /**
* @file qurl_http_get_demo.c
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
#define UNIR_HTTP_TEST                 1

#define QURL_HTTP_APP_MAX(a, b)        (((a) > (b)) ? (a) : (b))
#define QURL_HTTP_APP_MIN(a, b)        (((a) < (b)) ? (a) : (b))

//Here using /user/ directory, can be shared with qcm
#define QURL_APPS_HTTP_FILENAME        "/user/http_test1.txt"
#define QURL_APPS_HTTP_HEAD_FILE       "/user/http_header.txt"

struct qurl_app_r_buf_s
{
    char *buf_ptr;
    long  buf_len;
    long  r_index;
};
typedef struct qurl_app_r_buf_s qurl_app_r_buf_t;

/**
 * @brief Define unir_fota_http_t structure type, used to store various states and configuration information during HTTP transmission.
 */
typedef struct
{
    qurl_core_t   http_hd;                      /*!< HTTP header core information. */
    int           http_mode;                    /*!< HTTP mode. */
    int           ssl_ctxid;                    /*!< SSL context ID. */
    char          file_name[QOSA_VFS_PATH_MAX]; /*!< File name to download or upload. */
    int           fd;                           /*!< File descriptor, used for file operations. */
    int           pdp_id;                       /*!< PDP context ID, used for mobile network data connection. */
    qosa_uint32_t time_out;                     /*!< Timeout time, unit may be milliseconds. */
    qosa_uint32_t start_pos;                    /*!< Start position for download or upload. */
    qosa_uint32_t dload_want_size;              /*!< Expected download size. */
    qosa_uint32_t resume_dload_count;           /*!< Resume count, used for restarting after download interruption. */
    qosa_uint32_t fs_free_size;                 /*!< File system free space size. */
    int           event_errcode;                /*!< Event error code, used to record errors that occurred during the process. */
    int           write_errcode;                /*!< Write error code, records errors in write operations. */
    int           chunk_encode;                 /*!< Chunked encoding flag, used for handling HTTP chunked transfer. */
    int           first_flag;                   /*!< Flag indicating whether it's the first transmission. */
    qosa_uint32_t write_size;                   /*!< Actual write size */
    qosa_uint32_t total_recv_cnt;               /*!< Current cumulative total write size */
    qosa_uint32_t need_save;                    /*!< Whether need to save to file */
} unir_demo_http_t;

static unir_demo_http_t *g_demo_http = QOSA_NULL;
static char              head_data[] = "GET /get HTTP/1.1\r\nHost: httpbin.org\r\nAccept: */*\r\n\r\n";

/*===========================================================================
 *  Static API Functions
 ===========================================================================*/

/**
 * @brief Get available space size
 *
 * This function obtains the available space size by querying the root filesystem statistics.
 *
 * @param None
 *
 * @return Returns available space size (bytes), returns 0 if failed to get
 */
static qosa_int64_t qurl_http_demo_get_space_free_size(void)
{
    // Get filesystem free space
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
 * @return Returns 0 for success, -1 for failure
 */
static qosa_int32_t unir_http_demo_vfs_write(unir_demo_http_t *http_ptr, char *buf, long size)
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
 * HTTP application request header callback function
 * This function is used to upload custom request headers
 *
 * @param buf   Input buffer pointer, used to store headers to be uploaded
 * @param size  Allowed upload size
 * @param arg   User-defined parameter, points to qurl_app_r_buf_t structure
 *
 * @return Actual upload size, returns less than size indicates failure
 */
static long qurl_http_app_r_raw_head_cb(char *buf, long size, void *arg)
{
    /* Get application read buffer structure pointer */
    qurl_app_r_buf_t *r_buf_ptr = (qurl_app_r_buf_t *)arg;

    /* Check if request read size is valid */
    if (size < 1)
    {
        return 0;
    }

    /* Calculate actual readable data size to avoid exceeding buffer boundaries */
    size = QURL_HTTP_APP_MIN(size, r_buf_ptr->buf_len - r_buf_ptr->r_index);

    // Upload data
    qosa_memcpy(buf, r_buf_ptr->buf_ptr, size);

    /* Update read index position */
    r_buf_ptr->r_index += size;

    return size;
}

/**
 * @brief HTTP response header callback function
 *
 * @param buf  Pointer to receive data buffer
 * @param size Size of received data
 * @param arg  User-defined parameter pointer (not used in this function)
 *
 * @return Returns processed data size, which is the value of size parameter
 */
static long qurl_http_app_w_h_cb(char *buf, long size, void *arg)
{
    // Prevent unused parameter warning
    (void)arg;

    // Print received data size and content to application log
    QLOGD("size=%d,%s", size, buf);
    return size;
}

/**
 * @brief Print HTTP response body content
 *
 * This function is used to print HTTP response body data content and size
 *
 * @param buf Pointer to response body data buffer
 * @param size Size of response body data (bytes)
 *
 * @return No return value
 */
static void qurl_http_demo_print_body(char *buf, long size)
{
    QLOGD("size=%d,%s", size, buf);
}

/**
 * @brief Save HTTP response data to file
 *
 * This function handles data reception during HTTP download process and writes data to filesystem.
 * It determines whether download can continue based on response status code and content length,
 * and handles chunked encoding or fixed-length data streams.
 *
 * @param http_ptr HTTP context structure pointer, contains current download state information
 * @param buf      Received data buffer
 * @param size     Currently received data size (bytes)
 *
 * @return Actual bytes written to file; returns 0 if error occurs
 * @note This method of saving data in callback is only for demonstration, can be used,
 * but if downloading large files, this method is inefficient. Reasons:
 * 1: Single write file size is not fixed, not necessarily optimal length, write efficiency is low
 * 2. File writing blocks HTTP download, if server sends quickly, may cause frequent retransmissions
 * For large files, recommend custom task and use watermark cache mechanism, write 4K data each time to improve file write efficiency
 */
static long qurl_http_demo_save_file(unir_demo_http_t *http_ptr, char *buf, long size)
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
                // Content_length exists, known total body length, check if remaining space is sufficient to save all data
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
                // Content_length does not exist, belongs to chunked encoding, total body length uncertain
                http_ptr->chunk_encode = 1;
                http_ptr->dload_want_size = 0;
            }
            QLOGD("resp_code=%d,content_length=%d", resp_code, content_length);
        }
        else
        {
            // Other resp_code indicates failure
            QLOGD("resp_code=%d", resp_code);
            goto exit;
        }
        QLOGV("%d", size);
        http_ptr->first_flag = 1;  // Mark that initial state check has been completed
    }

    // If in chunked encoding mode, need to check remaining space each time to ensure sufficient for current data block
    if (http_ptr->chunk_encode == 1)
    {
        free_size = qurl_http_demo_get_space_free_size();
        if (size >= free_size)
        {
            // No space left, do not write file, recommend reserving a few KB space here to prevent system operation anomalies
            return 0;
        }
    }

    // Write data to filesystem
    ret = unir_http_demo_vfs_write(http_ptr, buf, size);
    if (ret != 0)
    {
        // Write error
        return 0;
    }

    // Update total received and written bytes count
    http_ptr->total_recv_cnt += size;
    http_ptr->write_size += size;
    QLOGD("total_recv_cnt=%d", http_ptr->total_recv_cnt);
    return size;
exit:
    return 0;
}

/**
 * HTTP response data write callback function
 *
 * This function serves as the HTTP client's data write callback handler, responsible for processing HTTP response data received from server.
 * Based on configuration in HTTP parameter structure, decides whether to save data to file or directly print output.
 *
 * @param buf: Pointer to received data buffer
 * @param size: Received data size
 * @param arg: Pointer to HTTP parameter structure
 *
 * @return long: Actual processed data size, returning value less than size indicates processing failure, returning size indicates processing success and continue receiving data
 */
static long qurl_http_app_w_cb(char *buf, long size, void *arg)
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
        ret = qurl_http_demo_save_file(http_ptr, buf, size);
        return ret;
    }
    else
    {
        // Directly print HTTP response body content
        qurl_http_demo_print_body(buf, size);
    }

    // Continue getting body, then return original length
    return size;
}

/**
 * @brief Execute an HTTP GET request, access specified URL and output response information.
 *
 * This function demonstrates how to use the qurl library to initiate an HTTP GET request, set request options,
 * process response data, and clean up resources after completion. The target URL is "http://httpbin.org/get".
 *
 * @return qurl_ecode_t Returns operation result status code, QURL_OK indicates success.
 */
qurl_ecode_t qurl_http_app_get(void)
{
    qurl_ecode_t ret = QURL_OK;
    qurl_core_t  core = QOSA_NULL;
    qurl_slist_t headers = QOSA_NULL;

    qosa_memset(g_demo_http, 0, sizeof(unir_demo_http_t));
    // Initialize global qurl environment
    qurl_global_init();

    // Create qurl core handle
    g_demo_http->http_hd = core;
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGV("%x\r\n", ret);
    }

    // Required parameters
    // Set network ID (PDP context ID)
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // Set request URL, default HTTP:80 HTTPS:443
    ret = qurl_core_setopt(core, QURL_OPT_URL, "http://httpbin.org/get");
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
        goto exit;
    }
    // Configure as HTTP GET method
    qurl_core_setopt(core, QURL_OPT_HTTP_GET, 1L);

    // Optional parameters
    // Set response body callback, receive body in this function, can get content_length in this function
    // If body processing is not needed, can be omitted
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);
    qurl_core_setopt(core, QURL_OPT_WRITE_CB_ARG, g_demo_http);

    // Set response header callback function, receive headers in this function
    qurl_core_setopt(core, QURL_OPT_WRITE_HEAD_CB, qurl_http_app_w_h_cb);
    // Set request timeout time (milliseconds)
    qurl_core_setopt(core, QURL_OPT_TIMEOUT_MS, (5 * 1000));
    // Set allow redirects
    qurl_core_setopt(core, QURL_OPT_FOLLOWLOCATION, 1L);
    // Set server port
    qurl_core_setopt(core, QURL_OPT_PORT, 80);
    // Add custom HTTP request header content
    headers = qurl_slist_add_strdup(headers, "User-Agent: Quectel-Module");
    qurl_core_setopt(core, QURL_OPT_HTTP_HEADER, headers);

    // Customize whether to save to filesystem during testing
    g_demo_http->need_save = 1;
    // If need to save file, need to get remaining space size to prevent exceeding filesystem
    if (g_demo_http->need_save)
    {
        g_demo_http->fs_free_size = qurl_http_demo_get_space_free_size();
        qosa_memset(g_demo_http->file_name, 0, sizeof(g_demo_http->file_name));
        qosa_memcpy(g_demo_http->file_name, QURL_APPS_HTTP_FILENAME, qosa_strlen(QURL_APPS_HTTP_FILENAME));

        // Open file for writing
        g_demo_http->fd = qosa_vfs_open(QURL_APPS_HTTP_FILENAME, QOSA_VFS_O_CREAT | QOSA_VFS_O_RDWR);
        if (g_demo_http->fd < 0)
        {
            QLOGE("open dir error!!");
            goto exit;
        }
    }

    // Execute HTTP request
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

exit:

    if (g_demo_http->fd >= 0)
    {
        qosa_vfs_close(g_demo_http->fd);
        // After testing, can choose not to delete immediately, first check if file content is correct, then delete file after checking
        //qosa_vfs_unlink(QURL_APPS_HTTP_FILENAME);
    }
    // Delete qurl core handle, release resources
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Release request header linked list resources
    qurl_slist_del_all(headers);

    // Output test completion log
    QLOGV("test end");

    return QURL_OK;
}

/**
 * @brief Execute HTTP request using custom request headers
 *
 * This function is used to initialize HTTP client, set related options (such as URL, callback functions, etc.),
 * initiate HTTP request, and after request completion get response status code and content length information.
 * Finally clean up resources and return execution result.
 *
 * @return qurl_ecode_t Returns operation result, QURL_OK indicates success, other values indicate error codes
 */
static qurl_ecode_t qurl_http_app_get_raw_head(void)
{
    qurl_ecode_t     ret = QURL_OK;
    qurl_core_t      core = QOSA_NULL;
    long             resp_code = 0;
    long             content_length = 0;
    qurl_app_r_buf_t r_head_buf = {0};

    // Custom upload data
    r_head_buf.buf_ptr = head_data;
    r_head_buf.buf_len = qosa_strlen(head_data);
    r_head_buf.r_index = 0;

    // Initialize global qurl environment
    qurl_global_init();

    // Create qurl core handle
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set request URL
    ret = qurl_core_setopt(core, QURL_OPT_URL, "http://httpbin.org/get");
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set network ID and other callback functions
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // Request header callback function, can input complete request headers in callback
    qurl_core_setopt(core, QURL_OPT_READ_HEAD_CB, qurl_http_app_r_raw_head_cb);
    qurl_core_setopt(core, QURL_OPT_READ_HEAD_CB_ARG, &r_head_buf);
    // Response header callback function
    qurl_core_setopt(core, QURL_OPT_WRITE_HEAD_CB, qurl_http_app_w_h_cb);
    // Response body receive callback function
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);

    // Execute HTTP request
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Get response status code and content length
    qurl_core_getinfo(core, QURL_INFO_RESP_CODE, &resp_code);
    QLOGV("resp_code:[%d]\r\n", resp_code);

    qurl_core_getinfo(core, QURL_INFO_RESP_CONTENT_LENGTH, &content_length);
    QLOGV("content_length:[%d]\r\n", content_length);

    // Delete HTTP core handle, release resources
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    return QURL_OK;
}

/**
 * @brief Execute HTTP GET request and upload custom request headers, while receiving response headers and response body.
 *
 * This function demonstrates how to use the qurl library to initiate an HTTP GET request and attach custom request headers.
 * It also sets up response header and response body callback processing functions to receive data returned from the server.
 * After the request is completed, it prints the response status code and content length, and releases related resources.
 *
 * @return qurl_ecode_t Returns operation result, QURL_OK indicates success, other values indicate failure.
 */
static qurl_ecode_t qurl_http_app_get_upload_head(void)
{
    qurl_ecode_t ret = QURL_OK;
    qurl_core_t  core = QOSA_NULL;
    long         resp_code = 0;
    long         content_length = 0;

    // Initialize global qurl environment
    qurl_global_init();

    // Create qurl core handle
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set request URL to test interface
    ret = qurl_core_setopt(core, QURL_OPT_URL, "http://httpbin.org/get");
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Configure network parameters and upload request header data
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // Request header callback function, can input complete request headers in callback
    qurl_core_setopt(core, QURL_OPT_UPLOAD_HEAD_DATA, head_data);
    qurl_core_setopt(core, QURL_OPT_UPLOAD_HEAD_SIZE, qosa_strlen(head_data));
    // Response header callback function
    qurl_core_setopt(core, QURL_OPT_WRITE_HEAD_CB, qurl_http_app_w_h_cb);
    // Response body receive callback function
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);

    // Initiate HTTP request
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Get and print response status code and content length
    qurl_core_getinfo(core, QURL_INFO_RESP_CODE, &resp_code);
    QLOGD("resp_code:[%d]\r\n", resp_code);

    qurl_core_getinfo(core, QURL_INFO_RESP_CONTENT_LENGTH, &content_length);
    QLOGD("content_length:[%d]\r\n", content_length);

    // Delete HTTP core handle, release resources
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    return QURL_OK;
}

/**
 * @brief Execute an HTTP GET request and upload request headers via file
 *
 * This function demonstrates how to use the qurl library to initiate an HTTP GET request, set related options,
 * and upload request headers through a file
 *
 * @return qurl_ecode_t Returns execution result status code, QURL_OK indicates success.
 */
static qurl_ecode_t qurl_http_app_get_upload_file_head(void)
{
    qurl_ecode_t ret = QURL_OK;
    qurl_core_t  core = QOSA_NULL;
    long         resp_code = 0;
    long         content_length = 0;

    // Initialize global qurl environment
    qurl_global_init();

    // Create qurl core handle
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set request URL to test interface
    ret = qurl_core_setopt(core, QURL_OPT_URL, "http://httpbin.org/get");
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Configure network parameters and callback functions
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // Set upload request header file path, used to save complete request header information
    qurl_core_setopt(core, QURL_OPT_UPLOAD_HEAD_FILE, QURL_APPS_HTTP_HEAD_FILE);
    // Set response header callback function
    qurl_core_setopt(core, QURL_OPT_WRITE_HEAD_CB, qurl_http_app_w_h_cb);
    // Set response body receive callback function
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);

    // Execute HTTP request operation
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Get and print response status code and content length
    qurl_core_getinfo(core, QURL_INFO_RESP_CODE, &resp_code);
    QLOGV("resp_code:[%d]\r\n", resp_code);

    qurl_core_getinfo(core, QURL_INFO_RESP_CONTENT_LENGTH, &content_length);
    QLOGV("content_length:[%d]\r\n", content_length);

    // Delete HTTP core handle, release resources
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    return QURL_OK;
}

/**
 * @brief Example function for executing HTTP GET request with resume (Range) capability
 *
 * This function demonstrates how to use the qurl library to initiate an HTTP GET request and implement partial content download by setting Range parameters.
 * It also shows how to configure callback functions to handle response headers and response body, set timeout time, allow redirects and other options.
 * If file saving is needed, it will also open the filesystem for write operations.
 *
 * @return qurl_ecode_t Returns execution result status code, QURL_OK indicates success
 */
qurl_ecode_t qurl_http_app_get_range(void)
{
    qurl_ecode_t ret = QURL_OK;
    qurl_core_t  core = QOSA_NULL;
    char         range_buf[32] = {0};

    // Initialize global HTTP context structure
    qosa_memset(g_demo_http, 0, sizeof(unir_demo_http_t));

    // Initialize global qurl environment
    qurl_global_init();

    // Create qurl core handle
    g_demo_http->http_hd = core;
    ret = qurl_core_create(&core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Set necessary network parameters and URL
    // Set network ID (PDP context ID)
    qurl_core_setopt(core, QURL_OPT_NETWORK_ID, UNIR_HTTP_DEMP_PDPID);
    // Set request URL, default HTTP:80 HTTPS:443
    //http://httpbin.org/#/Dynamic_data
    // This URL requires finding the Dynamic data option on the webpage, under which find /range/{numbytes}, click try it out and fill in a number,
    // The last number in the URL is the one you filled in
    ret = qurl_core_setopt(core, QURL_OPT_URL, "http://httpbin.org/range/100");
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
        goto exit;
    }
    // Configure as HTTP GET method
    qurl_core_setopt(core, QURL_OPT_HTTP_GET, 1L);

    // Set optional parameters
    // Set response body callback function, receive response body content in this function
    qurl_core_setopt(core, QURL_OPT_WRITE_CB, qurl_http_app_w_cb);
    qurl_core_setopt(core, QURL_OPT_WRITE_CB_ARG, g_demo_http);

    // Set response header callback function, receive response header information in this function
    qurl_core_setopt(core, QURL_OPT_WRITE_HEAD_CB, qurl_http_app_w_h_cb);
    // Set request timeout time to 5 seconds
    qurl_core_setopt(core, QURL_OPT_TIMEOUT_MS, (5 * 1000));
    // Allow server redirects
    qurl_core_setopt(core, QURL_OPT_FOLLOWLOCATION, 1L);
    // Set server port to 80
    qurl_core_setopt(core, QURL_OPT_PORT, 80);
    // Set breakpoint download range, from byte 10 to the end
    // If only downloading a middle portion, can also change to something like "10-30"
    qosa_snprintf(range_buf, 32 - 1, "10-");
    qurl_core_setopt(core, QURL_OPT_RANGE, range_buf);

    // Determine whether to save response content to file
    g_demo_http->need_save = 1;
    if (g_demo_http->need_save)
    {
        // Get filesystem remaining space size
        g_demo_http->fs_free_size = qurl_http_demo_get_space_free_size();
        qosa_memset(g_demo_http->file_name, 0, sizeof(g_demo_http->file_name));
        qosa_memcpy(g_demo_http->file_name, QURL_APPS_HTTP_FILENAME, qosa_strlen(QURL_APPS_HTTP_FILENAME));

        // Open file for writing response content
        g_demo_http->fd = qosa_vfs_open(QURL_APPS_HTTP_FILENAME, QOSA_VFS_O_CREAT | QOSA_VFS_O_RDWR);
        if (g_demo_http->fd < 0)
        {
            QLOGE("open dir error!!");
            goto exit;
        }
    }

    // Execute HTTP request
    ret = qurl_core_perform(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

exit:

    // Close opened file descriptor
    if (g_demo_http->fd >= 0)
    {
        qosa_vfs_close(g_demo_http->fd);
        // After testing, can choose not to delete immediately, first check if file content is correct, then delete file after checking
        //qosa_vfs_unlink(QURL_APPS_HTTP_FILENAME);
    }

    // Delete qurl core handle, release resources
    ret = qurl_core_delete(core);
    if (ret != QURL_OK)
    {
        QLOGE("%x\r\n", ret);
    }

    // Output test completion log
    QLOGV("test end");

    return QURL_OK;
}

static void unir_qurl_http_app_init(void *argv)
{
    qurl_ecode_t ret = QURL_OK;

    qosa_task_sleep_sec(10);
    if (UNIR_HTTP_TEST == 1)
    {
        // HTTP GET test
        ret = qurl_http_app_get();
    }
    else if (UNIR_HTTP_TEST == 2)
    {
        // HTTP GET custom request header test, upload header via callback
        ret = qurl_http_app_get_raw_head();
    }
    else if (UNIR_HTTP_TEST == 3)
    {
        // HTTP GET custom request header test, upload via QURL_OPT_UPLOAD_HEAD_DATA
        ret = qurl_http_app_get_upload_head();
    }
    else if (UNIR_HTTP_TEST == 4)
    {
        // HTTP GET custom request header test, upload via file
        ret = qurl_http_app_get_upload_file_head();
    }
    else if (UNIR_HTTP_TEST == 5)
    {
        // HTTP GET breakpoint download
        ret = qurl_http_app_get_range();
    }

    QLOGD("ret=%d", ret);
}

void unir_qurl_http_get_demo_init(void)
{
    int         err = 0;
    qosa_task_t http_task = QOSA_NULL;

    if (g_demo_http == QOSA_NULL)
    {
        g_demo_http = qosa_malloc(sizeof(unir_demo_http_t));
        if (g_demo_http == QOSA_NULL)
        {
            QLOGE("http demo malloc error");
            return;
        }
    }
    err = qosa_task_create(&http_task, UNIR_HTTP_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "http_demo", unir_qurl_http_app_init, QOSA_NULL);
    if (err != QOSA_OK)
    {
        QLOGE("task create error");
        return;
    }
}

UNIRTOS_APP_EXPORT(360, "qurl_http_get_demo", unir_qurl_http_get_demo_init);