/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _OSAL_H_
#define _OSAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#define gettimeofday                osal_gettimeofday
#define usleep                      osal_usleep
#define open_memstream              osal_open_memstream

#ifdef __linux__
#define OSAL_STRM_POS(A) (A).__pos
#else
#define OSAL_STRM_POS(A) (A)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _OSAL_H_ */
