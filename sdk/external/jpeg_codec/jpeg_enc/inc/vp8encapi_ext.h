/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __VP8ENCAPI_EXT_H__
#define __VP8ENCAPI_EXT_H__

#include "basetype.h"
#include "vp8encapi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    VP8EncRet VP8EncTestInputLineBuf(VP8EncInst inst);

    VP8EncRet VP8EncTestCropping(VP8EncInst inst);

#ifdef __cplusplus
}
#endif

#endif /*__VP8ENCAPI_EXT_H__*/
