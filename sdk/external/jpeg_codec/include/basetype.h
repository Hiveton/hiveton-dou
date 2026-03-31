/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*!\file
 * \brief Basic type definitions.
 *
 * Basic numeric data type definitions used in the decoder software.
 */


#ifndef BASETYPE_H_INCLUDED
#define BASETYPE_H_INCLUDED
#ifdef __cplusplus
extern "C"
{
#endif
/*! \addtogroup common Common definitions
 *  @{
 */

#include <stddef.h>

#include "osal_sifli.h"

#ifndef NULL
    #ifdef  __cplusplus
        #define NULL    0
    #else
        #define NULL    ((void *)0)
    #endif
#endif

/* Macro to signal unused parameter. */
#ifndef UNUSED
    #define UNUSED(x) (void)(x)
#endif /* UNUSED */

typedef unsigned char u8; /**< unsigned 8 bits integer value */
typedef signed char i8; /**< signed 8 bits integer value */
typedef unsigned short u16; /**< unsigned 16 bits integer value */
typedef signed short i16; /**< signed 16 bits integer value */
typedef unsigned int u32; /**< unsigned 32 bits integer value */
typedef signed int i32; /**< signed 8 bits integer value */

typedef long long  i64;
typedef unsigned long long u64;

typedef size_t ptr_t;

#if defined(_WIN64)
    typedef unsigned long long g1_addr_t;
#else
    typedef unsigned long g1_addr_t; /**< unsigned 64 bits integer value */
    //typedef size_t g1_addr_t;
#endif
typedef g1_addr_t addr_t;


/*!\cond SWDEC*/
/* SW decoder 16 bits types */
#if defined(VC1SWDEC_16BIT) || defined(MP4ENC_ARM11)
    typedef unsigned short u16x;
    typedef signed short i16x;
#else
    typedef unsigned int u16x;
    typedef signed int i16x;
#endif
/*!\endcond */

/*! @} - end group common */
#ifdef __cplusplus
}
#endif
#endif /* BASETYPE_H_INCLUDED */
