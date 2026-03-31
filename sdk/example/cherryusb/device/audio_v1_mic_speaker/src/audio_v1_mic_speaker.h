/*
 * SPDX-FileCopyrightText: 2019-2025 SiFli Technologies(Nanjing) Co, Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _AUDIO_V1_MIC_SPEAKER_H
#define _AUDIO_V1_MIC_SPEAKER_H

void audio_v1_init(uint8_t busid, uint32_t reg_base);
void audio_v1_task(uint8_t busid);

#endif