/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __JPEGDECAPI_EX_H__
#define __JPEGDECAPI_EX_H__

#ifdef __cplusplus
extern "C"
{
#endif
#include "jpegdecapi.h"

JpegDecRet JpegDecInitReg(JpegDecInst decInst);

JpegDecRet JpegDecUpdateSliceMbSet(JpegDecInst decInst, JpegDecInput *pDecIn,
                                JpegDecImageInfo *pImageInfo, u32 amountOfMCUs,
                                u32 mcuInRow, u32 mcuSizeDivider);

const void *JpegDecGetDwlInst(JpegDecInst decInst);

#ifdef __cplusplus
}
#endif

#endif
