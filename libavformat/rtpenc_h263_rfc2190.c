/*
 * RTP packetization for H.263 video
 * Copyright (c) 2012 Martin Storsjo
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "rtpenc.h"
#include "libavutil/bitstream.h"

struct H263Info {
    int src;
    int i;
    int u;
    int s;
    int a;
    int pb;
    int tr;
};

struct H263State {
    int gobn;
    int mba;
    int hmv1, vmv1, hmv2, vmv2;
    int quant;
};

static void send_mode_a(AVFormatContext *s1, const struct H263Info *info,
                        const uint8_t *buf, int len, int ebits, int m)
{
    RTPMuxContext *s = s1->priv_data;
    AVPutBitContext pb;

    init_av_bitstream_put(&pb, s->buf, 32);
    av_bitstream_put(&pb, 1, 0); /* F - 0, mode A */
    av_bitstream_put(&pb, 1, 0); /* P - 0, normal I/P */
    av_bitstream_put(&pb, 3, 0); /* SBIT - 0 bits */
    av_bitstream_put(&pb, 3, ebits); /* EBIT */
    av_bitstream_put(&pb, 3, info->src); /* SRC - source format */
    av_bitstream_put(&pb, 1, info->i); /* I - inter/intra */
    av_bitstream_put(&pb, 1, info->u); /* U - unrestricted motion vector */
    av_bitstream_put(&pb, 1, info->s); /* S - syntax-baesd arithmetic coding */
    av_bitstream_put(&pb, 1, info->a); /* A - advanced prediction */
    av_bitstream_put(&pb, 4, 0); /* R - reserved */
    av_bitstream_put(&pb, 2, 0); /* DBQ - 0 */
    av_bitstream_put(&pb, 3, 0); /* TRB - 0 */
    av_bitstream_put(&pb, 8, info->tr); /* TR */
    flush_av_bitstream_put(&pb);
    memcpy(s->buf + 4, buf, len);

    ff_rtp_send_data(s1, s->buf, len + 4, m);
}

static void send_mode_b(AVFormatContext *s1, const struct H263Info *info,
                        const struct H263State *state, const uint8_t *buf,
                        int len, int sbits, int ebits, int m)
{
    RTPMuxContext *s = s1->priv_data;
    AVPutBitContext pb;

    init_av_bitstream_put(&pb, s->buf, 64);
    av_bitstream_put(&pb, 1, 1); /* F - 1, mode B */
    av_bitstream_put(&pb, 1, 0); /* P - 0, mode B */
    av_bitstream_put(&pb, 3, sbits); /* SBIT - 0 bits */
    av_bitstream_put(&pb, 3, ebits); /* EBIT - 0 bits */
    av_bitstream_put(&pb, 3, info->src); /* SRC - source format */
    av_bitstream_put(&pb, 5, state->quant); /* QUANT - quantizer for the first MB */
    av_bitstream_put(&pb, 5, state->gobn); /* GOBN - GOB number */
    av_bitstream_put(&pb, 9, state->mba); /* MBA - MB address */
    av_bitstream_put(&pb, 2, 0); /* R - reserved */
    av_bitstream_put(&pb, 1, info->i); /* I - inter/intra */
    av_bitstream_put(&pb, 1, info->u); /* U - unrestricted motion vector */
    av_bitstream_put(&pb, 1, info->s); /* S - syntax-baesd arithmetic coding */
    av_bitstream_put(&pb, 1, info->a); /* A - advanced prediction */
    av_bitstream_put(&pb, 7, state->hmv1); /* HVM1 - horizontal motion vector 1 */
    av_bitstream_put(&pb, 7, state->vmv1); /* VMV1 - vertical motion vector 1 */
    av_bitstream_put(&pb, 7, state->hmv2); /* HVM2 - horizontal motion vector 2 */
    av_bitstream_put(&pb, 7, state->vmv2); /* VMV2 - vertical motion vector 2 */
    flush_av_bitstream_put(&pb);
    memcpy(s->buf + 8, buf, len);

    ff_rtp_send_data(s1, s->buf, len + 8, m);
}

void ff_rtp_send_h263_rfc2190(AVFormatContext *s1, const uint8_t *buf, int size,
                              const uint8_t *mb_info, int mb_info_size)
{
    RTPMuxContext *s = s1->priv_data;
    int len, sbits = 0, ebits = 0;
    AVGetBitContext gb;
    struct H263Info info = { 0 };
    struct H263State state = { 0 };
    int mb_info_pos = 0, mb_info_count = mb_info_size / 12;
    const uint8_t *buf_base = buf;

    s->timestamp = s->cur_timestamp;

    av_bitstream_get_init(&gb, buf, size*8);
    if (av_bitstream_get(&gb, 22) == 0x20) { /* Picture Start Code */
        info.tr  = av_bitstream_get(&gb, 8);
        av_bitstream_skip(&gb, 2); /* PTYPE start, H261 disambiguation */
        av_bitstream_skip(&gb, 3); /* Split screen, document camera, freeze picture release */
        info.src = av_bitstream_get(&gb, 3);
        info.i   = av_bitstream_get(&gb, 1);
        info.u   = av_bitstream_get(&gb, 1);
        info.s   = av_bitstream_get(&gb, 1);
        info.a   = av_bitstream_get(&gb, 1);
        info.pb  = av_bitstream_get(&gb, 1);
    }

    while (size > 0) {
        struct H263State packet_start_state = state;
        len = FFMIN(s->max_payload_size - 8, size);

        /* Look for a better place to split the frame into packets. */
        if (len < size) {
            const uint8_t *end = ff_h263_find_resync_marker_reverse(buf,
                                                                    buf + len);
            len = end - buf;
            if (len == s->max_payload_size - 8) {
                /* Skip mb info prior to the start of the current ptr */
                while (mb_info_pos < mb_info_count) {
                    uint32_t pos = AV_RL32(&mb_info[12*mb_info_pos])/8;
                    if (pos >= buf - buf_base)
                        break;
                    mb_info_pos++;
                }
                /* Find the first mb info past the end pointer */
                while (mb_info_pos + 1 < mb_info_count) {
                    uint32_t pos = AV_RL32(&mb_info[12*(mb_info_pos + 1)])/8;
                    if (pos >= end - buf_base)
                        break;
                    mb_info_pos++;
                }
                if (mb_info_pos < mb_info_count) {
                    const uint8_t *ptr = &mb_info[12*mb_info_pos];
                    uint32_t bit_pos = AV_RL32(ptr);
                    uint32_t pos = (bit_pos + 7)/8;
                    if (pos <= end - buf_base) {
                        state.quant = ptr[4];
                        state.gobn  = ptr[5];
                        state.mba   = AV_RL16(&ptr[6]);
                        state.hmv1  = (int8_t) ptr[8];
                        state.vmv1  = (int8_t) ptr[9];
                        state.hmv2  = (int8_t) ptr[10];
                        state.vmv2  = (int8_t) ptr[11];
                        ebits = 8 * pos - bit_pos;
                        len   = pos - (buf - buf_base);
                        mb_info_pos++;
                    } else {
                        av_log(s1, AV_LOG_ERROR,
                               "Unable to split H263 packet, use -mb_info %d "
                               "or lower.\n", s->max_payload_size - 8);
                    }
                } else {
                    av_log(s1, AV_LOG_ERROR, "Unable to split H263 packet, "
                           "use -mb_info %d or -ps 1.\n",
                           s->max_payload_size - 8);
                }
            }
        }

        if (size > 2 && !buf[0] && !buf[1])
            send_mode_a(s1, &info, buf, len, ebits, len == size);
        else
            send_mode_b(s1, &info, &packet_start_state, buf, len, sbits,
                        ebits, len == size);

        if (ebits) {
            sbits = 8 - ebits;
            len--;
        } else {
            sbits = 0;
        }
        buf  += len;
        size -= len;
        ebits = 0;
    }
}
