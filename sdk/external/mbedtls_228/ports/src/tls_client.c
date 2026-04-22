/*
 *  Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rtthread.h>
#include <sys/time.h>
#if defined(RT_USING_SAL)
#include <sys/socket.h>
#else
#include <lwip/sockets.h>
#endif

#include "tls_client.h"
#include "tls_certificate.h"

#define HIVETON_TLS_IO_TIMEOUT_MS 10000

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

RT_WEAK void *hiveton_tls_malloc(size_t size)
{
    return rt_malloc(size);
}

RT_WEAK void *hiveton_tls_calloc(size_t count, size_t size)
{
    return rt_calloc(count, size);
}

RT_WEAK void hiveton_tls_free(void *ptr)
{
    rt_free(ptr);
}

static void hiveton_tls_apply_io_timeout(MbedTLSSession *session)
{
    struct timeval timeout;

    if (session == RT_NULL || session->server_fd.fd < 0)
    {
        return;
    }

    timeout.tv_sec = HIVETON_TLS_IO_TIMEOUT_MS / 1000;
    timeout.tv_usec = (HIVETON_TLS_IO_TIMEOUT_MS % 1000) * 1000;
    setsockopt(session->server_fd.fd, SOL_SOCKET, SO_RCVTIMEO,
               (void *)&timeout, sizeof(timeout));
    setsockopt(session->server_fd.fd, SOL_SOCKET, SO_SNDTIMEO,
               (void *)&timeout, sizeof(timeout));
}

static rt_bool_t hiveton_tls_timeout_expired(rt_tick_t start_tick)
{
    return ((rt_tick_t)(rt_tick_get() - start_tick) >=
            rt_tick_from_millisecond(HIVETON_TLS_IO_TIMEOUT_MS))
               ? RT_TRUE
               : RT_FALSE;
}

#undef tls_malloc
#undef tls_free
#undef tls_calloc
#define tls_malloc  hiveton_tls_malloc
#define tls_free    hiveton_tls_free
#define tls_calloc  hiveton_tls_calloc

#if defined(MBEDTLS_DEBUG_C)
#define DEBUG_LEVEL (2)
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L    /* C99 or later */
#include "mbedtls/debug.h"
#endif

#define DBG_ENABLE
#define DBG_COLOR
#define DBG_SECTION_NAME    "mbedtls.clnt"
#ifdef MBEDTLS_DEBUG_C
#define DBG_LEVEL           DBG_LOG
#else
#define DBG_LEVEL           DBG_INFO
#endif /* MBEDTLS_DEBUG_C */
#include <rtdbg.h>

static void _ssl_debug(void *ctx, int level, const char *file, int line, const char *str)
{
    ((void) level);

    LOG_D("%s:%04d: %s", file, line, str);
}

static int mbedtls_ssl_certificate_verify(MbedTLSSession *session)
{
    int ret = 0;
    ret = mbedtls_ssl_get_verify_result(&session->ssl);
    if (ret != 0)
    {
        LOG_E("verify peer certificate fail....");
        memset(session->buffer, 0x00, session->buffer_len);
        mbedtls_x509_crt_verify_info((char *)session->buffer, session->buffer_len, "  ! ", ret);
        LOG_E("verification info: %s", session->buffer);
        return -RT_ERROR;
    }
    return RT_EOK;
}

int mbedtls_client_init(MbedTLSSession *session, void *entropy, size_t entropyLen)
{
    int ret = 0;

#if defined(MBEDTLS_DEBUG_C)
    LOG_D("Set debug level (%d)", (int) DEBUG_LEVEL);
    mbedtls_debug_set_threshold((int) DEBUG_LEVEL);
#endif

    mbedtls_net_init(&session->server_fd);
    mbedtls_ssl_init(&session->ssl);
    mbedtls_ssl_config_init(&session->conf);
    mbedtls_ctr_drbg_init(&session->ctr_drbg);
    mbedtls_entropy_init(&session->entropy);
    mbedtls_x509_crt_init(&session->cacert);
    
    ret = mbedtls_ctr_drbg_seed(&session->ctr_drbg, mbedtls_entropy_func, &session->entropy,
                                     (unsigned char *)entropy, entropyLen);
    if (ret != 0)
    {
        LOG_E("mbedtls_ctr_drbg_seed error, return -0x%x\n", -ret);
        return ret;
    }
    LOG_D("mbedtls client struct init success...");

    return RT_EOK;
}

int mbedtls_client_close(MbedTLSSession *session)
{
    if (session == RT_NULL)
    {
        return -RT_ERROR;
    }

    mbedtls_ssl_close_notify(&session->ssl);
    mbedtls_net_free(&session->server_fd);
    mbedtls_x509_crt_free(&session->cacert);
    mbedtls_entropy_free(&session->entropy);
    mbedtls_ctr_drbg_free(&session->ctr_drbg);
    mbedtls_ssl_config_free(&session->conf);
    mbedtls_ssl_free(&session->ssl);

    if (session->buffer)
    {
        tls_free(session->buffer);
    }

    if (session->host)
    {
        tls_free(session->host);
    }

    if(session->port)
    {
        tls_free(session->port);
    }

    if (session)
    {   
        tls_free(session);
        session = RT_NULL;
    }
    
    return RT_EOK;
}

int mbedtls_client_context(MbedTLSSession *session)
{
    int ret = 0;
    int authmode = MBEDTLS_SSL_VERIFY_REQUIRED;
 
    ret = mbedtls_x509_crt_parse(&session->cacert, (const unsigned char *)mbedtls_root_certificate,
                                 mbedtls_root_certificate_len);
    if (ret < 0)
    {
LOG_W("CA certificate parse failed, return -0x%x; fallback to MBEDTLS_SSL_VERIFY_NONE", -ret);
        authmode = MBEDTLS_SSL_VERIFY_NONE;
    }
    else
    {
        LOG_D("Loading the CA root certificate success...");
    }

    /* Hostname set here should match CN in server certificate */
    if (session->host)
    {
        ret = mbedtls_ssl_set_hostname(&session->ssl, session->host);
        if (ret != 0)
        {
            LOG_E("mbedtls_ssl_set_hostname error, return -0x%x", -ret);
            return ret;
        }
    }

    ret = mbedtls_ssl_config_defaults(&session->conf,
                                          MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
    {
        LOG_E("mbedtls_ssl_config_defaults error, return -0x%x", -ret);
        return ret;
    }

    mbedtls_ssl_conf_authmode(&session->conf, authmode);
    if (authmode != MBEDTLS_SSL_VERIFY_NONE)
    {
        mbedtls_ssl_conf_ca_chain(&session->conf, &session->cacert, NULL);
    }
    mbedtls_ssl_conf_rng(&session->conf, mbedtls_ctr_drbg_random, &session->ctr_drbg);

    mbedtls_ssl_conf_dbg(&session->conf, _ssl_debug, NULL);

    ret = mbedtls_ssl_setup(&session->ssl, &session->conf);
    if (ret != 0)
    {
        LOG_E("mbedtls_ssl_setup error, return -0x%x\n", -ret);
        return ret;
    }
    LOG_D("mbedtls client context init success...");

    return RT_EOK;
}

int mbedtls_client_connect(MbedTLSSession *session)
{
    int ret = 0;
    int authmode = session->conf.authmode;
    rt_tick_t handshake_start_tick;

    ret = mbedtls_net_connect(&session->server_fd, session->host, 
                                session->port, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0)
    {
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
        {
            LOG_E("mbedtls_net_connect timeout after %dms", HIVETON_TLS_IO_TIMEOUT_MS);
        }
        else
        {
            LOG_E("mbedtls_net_connect error, return -0x%x", -ret);
        }
        return ret;
    }

    LOG_D("Connected %s:%s success...", session->host, session->port);

    hiveton_tls_apply_io_timeout(session);
    mbedtls_ssl_conf_read_timeout(&session->conf, HIVETON_TLS_IO_TIMEOUT_MS);
    mbedtls_ssl_set_bio(&session->ssl, &session->server_fd,
                        mbedtls_net_send, mbedtls_net_recv,
                        mbedtls_net_recv_timeout);

    handshake_start_tick = rt_tick_get();
    while ((ret = mbedtls_ssl_handshake(&session->ssl)) != 0)
    {
        if (hiveton_tls_timeout_expired(handshake_start_tick))
        {
            LOG_E("mbedtls_ssl_handshake timeout after %dms", HIVETON_TLS_IO_TIMEOUT_MS);
            return MBEDTLS_ERR_SSL_TIMEOUT;
        }

        if (authmode != MBEDTLS_SSL_VERIFY_NONE &&
            RT_EOK != mbedtls_ssl_certificate_verify(session))
        {
            return -RT_ERROR;
        }
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
        {
            LOG_E("mbedtls_ssl_handshake timeout after %dms", HIVETON_TLS_IO_TIMEOUT_MS);
            return ret;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            if (ret == MBEDTLS_ERR_NET_RECV_FAILED || ret == MBEDTLS_ERR_NET_SEND_FAILED ||
                ret == MBEDTLS_ERR_NET_CONN_RESET)
            {
                LOG_E("mbedtls_ssl_handshake I/O failure, return -0x%x", -ret);
            }
            else
            {
                LOG_E("mbedtls_ssl_handshake error, return -0x%x", -ret);
            }
            return ret;
        }
        rt_thread_mdelay(10);
    }

    if (authmode != MBEDTLS_SSL_VERIFY_NONE &&
        RT_EOK != mbedtls_ssl_certificate_verify(session))
    {
        return -RT_ERROR;
    }

    if (authmode == MBEDTLS_SSL_VERIFY_NONE)
    {
        LOG_W("Certificate verification skipped");
    }
    else
    {
        LOG_D("Certificate verified success...");
    }

    return RT_EOK;
}

int mbedtls_client_read(MbedTLSSession *session, unsigned char *buf , size_t len)
{
    int ret = 0;

    if (session == RT_NULL || buf == RT_NULL)
    {
        return -RT_ERROR;
    } 

    ret = mbedtls_ssl_read(&session->ssl, (unsigned char *)buf, len);
    if (ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
        {
            LOG_E("mbedtls_client_read timeout after %dms", HIVETON_TLS_IO_TIMEOUT_MS);
        }
        else if (ret == MBEDTLS_ERR_NET_RECV_FAILED || ret == MBEDTLS_ERR_NET_CONN_RESET)
        {
            LOG_E("mbedtls_client_read recv failure, return -0x%x", -ret);
        }
        else
        {
            LOG_E("mbedtls_client_read data error, return -0x%x", -ret);
        }
    }

    return ret;
}

int mbedtls_client_write(MbedTLSSession *session, const unsigned char *buf , size_t len)
{
    int ret = 0;

    if (session == RT_NULL || buf == RT_NULL)
    {
        return -RT_ERROR;
    }

    ret = mbedtls_ssl_write(&session->ssl, (unsigned char *)buf, len);
    if (ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
        {
            LOG_E("mbedtls_client_write timeout after %dms", HIVETON_TLS_IO_TIMEOUT_MS);
        }
        else if (ret == MBEDTLS_ERR_NET_SEND_FAILED || ret == MBEDTLS_ERR_NET_CONN_RESET)
        {
            LOG_E("mbedtls_client_write send failure, return -0x%x", -ret);
        }
        else
        {
            LOG_E("mbedtls_client_write data error, return -0x%x", -ret);
        }
    }

    return ret;
}
