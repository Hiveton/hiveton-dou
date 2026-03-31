/*
 * SPDX-FileCopyrightText: 2025 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _JPEG_DEC_H_
#define _JPEG_DEC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Decode JPEG image from byte stream
 *
 *
 * @param[in]  bytesStrmStart byte stream start address 
 * @param[in]  out_cf         output color format string, supported formats are "RGB565" and "ARGB8888"
 * @param[in]  out_buf        output buffer address
 * @param[in]  out_buf_len    output buffer length
 *
 * @return    0: success, other: fail
 */
int jpeg_decode2(uint8_t *byteStrmStart,
                 uint32_t streamTotalLen,
                 const char *out_cf,
                 uint8_t *out_buf,
                 uint32_t out_buf_len);

/**
 * @brief  Init decoder
 *
 *
 * @return None
 */
int jpeg_decode_init(void);


/**
 * @brief Get JPEG image dimension
 *
 * @param[in]  bytesStrmStart byte stream start address
 * @param[in]  streamTotalLen total length of byte stream
 * @param[out] width          output image width
 * @param[out] height         output image height
 *
 *  @return None
 */
int jpeg_decode_get_dimension(uint8_t *byteStrmStart, uint32_t streamTotalLen, uint32_t *width, uint32_t *height);

/**
 * @brief  De-Init decoder
 *
 *
 * @return None
 */
int jpeg_decode_deinit(void);



#ifdef __cplusplus
}
#endif

#endif /* _JPEG_DEC_H_ */
