/*
 * HEVC video Decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Gildas Cocherel
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

#include "hevc.h"
#include "internal.h"

int ff_hevc_find_ref_idx(HEVCContext *s, int poc)
{
    int i;
    int LtMask = (1 << s->sps->log2_max_poc_lsb) - 1;
    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        HEVCFrame *ref = &s->DPB[i];
        if (ref->frame->buf[0] && (ref->sequence == s->seq_decode)) {
            if ((ref->flags & HEVC_FRAME_FLAG_SHORT_REF) != 0 && ref->poc == poc)
                return i;
            if ((ref->flags & HEVC_FRAME_FLAG_SHORT_REF) != 0 && (ref->poc & LtMask) == poc)
	            return i;
	    }
    }
    av_log(s->avctx, AV_LOG_ERROR,
           "Could not find ref with POC %d\n", poc);
    return 0;
}

void ff_hevc_free_refPicListTab(HEVCContext *s, HEVCFrame *ref)
{
    int j;
    int ctb_count;

    if (!ref->refPicListTab)
        return;

    ctb_count = s->sps->pic_width_in_ctbs * s->sps->pic_height_in_ctbs;
    for (j = ctb_count-1; j > 0; j--) {
        if (ref->refPicListTab[j] != ref->refPicListTab[j-1])
            av_free(ref->refPicListTab[j]);
        ref->refPicListTab[j] = NULL;
    }
    if (ref->refPicListTab[0] != NULL) {
        av_free(ref->refPicListTab[0]);
        ref->refPicListTab[0] = NULL;
    }
    ref->refPicList = NULL;
}

static void update_refs(HEVCContext *s)
{
    int i, j;
    int used[FF_ARRAY_ELEMS(s->DPB)] = { 0 };
    for (i = 0; i < 5; i++) {
        RefPicList *rpl = &s->sh.refPocList[i];
        for (j = 0; j < rpl->numPic; j++)
            used[rpl->idx[j]] = 1;
    }
    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        HEVCFrame *ref = &s->DPB[i];
        if (ref->frame->buf[0] && !used[i])
            ref->flags &= ~HEVC_FRAME_FLAG_SHORT_REF;
        if (ref->frame->buf[0] && !ref->flags) {
            av_frame_unref(ref->frame);
            ff_hevc_free_refPicListTab(s, ref);
        }
    }
}

static int find_next_ref(HEVCContext *s, int poc)
{
    int i;

    if (!s->sh.first_slice_in_pic_flag)
        return ff_hevc_find_ref_idx(s, poc);

    update_refs(s);

    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        HEVCFrame *ref = &s->DPB[i];
        if (!ref->frame->buf[0]) {
            return i;
        }
    }
    av_log(s->avctx, AV_LOG_ERROR,
           "could not free room for POC %d\n", poc);
    return -1;
}

static void malloc_refPicListTab(HEVCContext *s)
{
    int i;
    HEVCFrame *ref  = &s->DPB[find_next_ref(s, s->poc)];
    int ctb_count   = s->sps->pic_width_in_ctbs * s->sps->pic_height_in_ctbs;
    int ctb_addr_ts = s->pps->ctb_addr_rs_to_ts[s->sh.slice_address];

    ref->refPicListTab[ctb_addr_ts] = av_mallocz(sizeof(RefPicListTab));
    for (i = ctb_addr_ts; i < ctb_count-1; i++)
        ref->refPicListTab[i+1] = ref->refPicListTab[i];
    ref->refPicList = (RefPicList*) ref->refPicListTab[ctb_addr_ts];
}

RefPicList* ff_hevc_get_ref_list(HEVCContext *s, int short_ref_idx, int x0, int y0)
{
    if (x0 < 0 || y0 < 0) {
        return s->ref->refPicList;
    } else {
        HEVCFrame *ref   = &s->DPB[short_ref_idx];
        int x_cb         = x0 >> s->sps->log2_ctb_size;
        int y_cb         = y0 >> s->sps->log2_ctb_size;
        int pic_width_cb = (s->sps->pic_width_in_luma_samples + (1<<s->sps->log2_ctb_size)-1 ) >> s->sps->log2_ctb_size;
        int ctb_addr_ts  = s->pps->ctb_addr_rs_to_ts[y_cb * pic_width_cb + x_cb];
        return (RefPicList*) ref->refPicListTab[ctb_addr_ts];
    }
}

void ff_hevc_clear_refs(HEVCContext *s)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        HEVCFrame *ref = &s->DPB[i];
        if (!(ref->flags & HEVC_FRAME_FLAG_OUTPUT)) {
            av_frame_unref(ref->frame);
            ref->flags = 0;
            ff_hevc_free_refPicListTab(s, ref);
        }
    }
}

void ff_hevc_flush_dpb(HEVCContext *s)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        HEVCFrame *ref = &s->DPB[i];
        av_frame_unref(ref->frame);
        ref->flags = 0;
    }
}

int ff_hevc_set_new_ref(HEVCContext *s, AVFrame **frame, int poc)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        HEVCFrame *ref = &s->DPB[i];
        if (!ref->frame->buf[0]) {
            *frame         = ref->frame;
            s->ref = ref;
            ref->poc       = poc;

            ref->flags    = HEVC_FRAME_FLAG_OUTPUT | HEVC_FRAME_FLAG_SHORT_REF;
            ref->sequence = s->seq_decode;
            return ff_reget_buffer(s->avctx, *frame);
        }
    }
    av_log(s->avctx, AV_LOG_ERROR,
           "DPB is full, could not add ref with POC %d\n", poc);
    return -1;
}

int ff_hevc_output_frame(HEVCContext *s, AVFrame *out, int flush)
{
    int nb_output = 0;
    int min_poc   = 0xFFFF;
    int i, min_idx, ret;

    do {
        for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
            HEVCFrame *frame = &s->DPB[i];
            if ((frame->flags & HEVC_FRAME_FLAG_OUTPUT) &&
                frame->sequence == s->seq_output) {
                nb_output++;
                if (frame->poc < min_poc) {
                    min_poc = frame->poc;
                    min_idx = i;
                }
            }
        }

        /* wait for more frames before output */
        if (!flush && s->seq_output == s->seq_decode &&
            nb_output <= s->sps->temporal_layer[0].num_reorder_pics + 1)
            return 0;

        if (nb_output) {
            HEVCFrame *frame = &s->DPB[min_idx];

            frame->flags &= ~HEVC_FRAME_FLAG_OUTPUT;
            ret = av_frame_ref(out, frame->frame);
            if (ret < 0)
                return ret;
            return 1;
        }

        if (s->seq_output != s->seq_decode)
            s->seq_output = (s->seq_output + 1) & 0xff;
    } while (s->seq_output != s->seq_decode);

    return 0;
}

void ff_hevc_compute_poc(HEVCContext *s, int poc_lsb)
{
    int iMaxPOClsb  = 1 << s->sps->log2_max_poc_lsb;
    int iPrevPOClsb = s->pocTid0 % iMaxPOClsb;
    int iPrevPOCmsb = s->pocTid0 - iPrevPOClsb;
    int iPOCmsb;
    if ((poc_lsb < iPrevPOClsb) && ((iPrevPOClsb - poc_lsb) >= (iMaxPOClsb / 2))) {
        iPOCmsb = iPrevPOCmsb + iMaxPOClsb;
    } else if ((poc_lsb > iPrevPOClsb) && ((poc_lsb - iPrevPOClsb) > (iMaxPOClsb / 2))) {
        iPOCmsb = iPrevPOCmsb - iMaxPOClsb;
    } else {
        iPOCmsb = iPrevPOCmsb;
    }
    s->poc = iPOCmsb + poc_lsb;
}

static void set_ref_pic_list(HEVCContext *s)
{
    SliceHeader *sh = &s->sh;
    RefPicList  *refPocList = s->sh.refPocList;
    RefPicList  *refPicList;
    RefPicList  refPicListTmp[2]= {{{0}}};

    uint8_t num_ref_idx_lx_act[2];
    uint8_t cIdx;
    uint8_t num_poc_total_curr;
    uint8_t num_rps_curr_lx;
    uint8_t first_list;
    uint8_t sec_list;
    uint8_t i, list_idx;
	uint8_t nb_list = s->sh.slice_type == B_SLICE ? 2 : 1;

    malloc_refPicListTab(s);
    refPicList = s->DPB[find_next_ref(s, s->poc)].refPicList;

    num_ref_idx_lx_act[0] = sh->num_ref_idx_l0_active;
    num_ref_idx_lx_act[1] = sh->num_ref_idx_l1_active;
    refPicList[1].numPic = 0;
    for ( list_idx = 0; list_idx < nb_list; list_idx++) {
        /* The order of the elements is
         * ST_CURR_BEF - ST_CURR_AFT - LT_CURR for the RefList0 and
         * ST_CURR_AFT - ST_CURR_BEF - LT_CURR for the RefList1
         */
        first_list = list_idx == 0 ? ST_CURR_BEF : ST_CURR_AFT;
        sec_list   = list_idx == 0 ? ST_CURR_AFT : ST_CURR_BEF;

        /* even if num_ref_idx_lx_act is inferior to num_poc_total_curr we fill in
         * all the element from the Rps because we might reorder the list. If
         * we reorder the list might need a reference picture located after
         * num_ref_idx_lx_act.
         */
        num_poc_total_curr = refPocList[ST_CURR_BEF].numPic + refPocList[ST_CURR_AFT].numPic + refPocList[LT_CURR].numPic;
        num_rps_curr_lx    = num_poc_total_curr<num_ref_idx_lx_act[list_idx] ? num_poc_total_curr : num_ref_idx_lx_act[list_idx];
        cIdx = 0;
        for(i = 0; i < refPocList[first_list].numPic; i++) {
            refPicListTmp[list_idx].list[cIdx] = refPocList[first_list].list[i];
            refPicListTmp[list_idx].idx[cIdx]  = refPocList[first_list].idx[i];
            refPicListTmp[list_idx].isLongTerm[cIdx]  = 0;
            cIdx++;
        }
        for(i = 0; i < refPocList[sec_list].numPic; i++) {
            refPicListTmp[list_idx].list[cIdx] = refPocList[sec_list].list[i];
            refPicListTmp[list_idx].idx[cIdx]  = refPocList[sec_list].idx[i];
            refPicListTmp[list_idx].isLongTerm[cIdx]  = 0;
            cIdx++;
        }
        for(i = 0; i < refPocList[LT_CURR].numPic; i++) {
            refPicListTmp[list_idx].list[cIdx] = refPocList[LT_CURR].list[i];
            refPicListTmp[list_idx].idx[cIdx]  = refPocList[LT_CURR].idx[i];
            refPicListTmp[list_idx].isLongTerm[cIdx]  = 1;
            cIdx++;
        }
        refPicList[list_idx].numPic = num_rps_curr_lx;
        if (s->sh.ref_pic_list_modification_flag_lx[list_idx] == 1) {
            for(i = 0; i < num_rps_curr_lx; i++) {
                refPicList[list_idx].list[i] = refPicListTmp[list_idx].list[sh->list_entry_lx[list_idx][ i ]];
                refPicList[list_idx].idx[i]  = refPicListTmp[list_idx].idx[sh->list_entry_lx[list_idx][ i ]];
                refPicList[list_idx].isLongTerm[i]  = refPicListTmp[list_idx].isLongTerm[sh->list_entry_lx[list_idx][ i ]];
            }
        } else {
            for(i = 0; i < num_rps_curr_lx; i++) {
                refPicList[list_idx].list[i] = refPicListTmp[list_idx].list[i];
                refPicList[list_idx].idx[i]  = refPicListTmp[list_idx].idx[i];
                refPicList[list_idx].isLongTerm[i]  = refPicListTmp[list_idx].isLongTerm[i];
            }
        }
    }
}

void ff_hevc_set_ref_poc_list(HEVCContext *s)
{
    int i;
    int j = 0;
    int k = 0;
    ShortTermRPS *rps        = s->sh.short_term_rps;
    LongTermRPS *long_rps    = &s->sh.long_term_rps;
    RefPicList   *refPocList = s->sh.refPocList;
    int MaxPicOrderCntLsb = 1 << s->sps->log2_max_poc_lsb;
    if (rps != NULL) {
        for (i = 0; i < rps->num_negative_pics; i ++) {
            if ( rps->used[i] == 1 ) {
                refPocList[ST_CURR_BEF].list[j] = s->poc + rps->delta_poc[i];
                refPocList[ST_CURR_BEF].idx[j]  = ff_hevc_find_ref_idx(s, refPocList[ST_CURR_BEF].list[j]);
                j++;
            } else {
                refPocList[ST_FOLL].list[k] = s->poc + rps->delta_poc[i];
                refPocList[ST_FOLL].idx[k]  = ff_hevc_find_ref_idx(s, refPocList[ST_FOLL].list[k]);
                k++;
            }
        }
        refPocList[ST_CURR_BEF].numPic = j;
        j = 0;
        for (i = rps->num_negative_pics; i < rps->num_delta_pocs; i ++) {
            if (rps->used[i] == 1) {
                refPocList[ST_CURR_AFT].list[j] = s->poc + rps->delta_poc[i];
                refPocList[ST_CURR_AFT].idx[j]  = ff_hevc_find_ref_idx(s, refPocList[ST_CURR_AFT].list[j]);
                j++;
            } else {
                refPocList[ST_FOLL].list[k] = s->poc + rps->delta_poc[i];
                refPocList[ST_FOLL].idx[k]  = ff_hevc_find_ref_idx(s, refPocList[ST_FOLL].list[k]);
                k++;
            }
        }
        refPocList[ST_CURR_AFT].numPic = j;
        refPocList[ST_FOLL].numPic = k;
        for( i = 0, j= 0, k = 0; i < long_rps->num_long_term_sps + long_rps->num_long_term_pics; i++) {
            int pocLt = long_rps->PocLsbLt[i];
            if (long_rps->delta_poc_msb_present_flag[i])
                pocLt += s->poc - long_rps->DeltaPocMsbCycleLt[i] * MaxPicOrderCntLsb - s->sh.pic_order_cnt_lsb;
            if (long_rps->UsedByCurrPicLt[i]) {
                refPocList[LT_CURR].idx[j]  = ff_hevc_find_ref_idx(s, pocLt);
                refPocList[LT_CURR].list[j] = s->DPB[refPocList[LT_CURR].idx[j]].poc;
                j++;
            } else {
                refPocList[LT_FOLL].idx[k]  = ff_hevc_find_ref_idx(s, pocLt);
                refPocList[LT_FOLL].list[k] = s->DPB[refPocList[LT_FOLL].idx[k]].poc;
                k++;
            }
        }
        refPocList[LT_CURR].numPic = j;
        refPocList[LT_FOLL].numPic = k;
        set_ref_pic_list(s);
    } else {
        malloc_refPicListTab(s);
    }
}

int ff_hevc_get_num_poc(HEVCContext *s)
{
    int ret = 0;
    int i;
    ShortTermRPS *rps     = s->sh.short_term_rps;
    LongTermRPS *long_rps = &s->sh.long_term_rps;

    if (rps) {
        for (i = 0; i < rps->num_negative_pics; i++)
            ret += !!rps->used[i];
        for (; i < rps->num_delta_pocs; i++)
            ret += !!rps->used[i];
    }

    if (long_rps) {
        for (i = 0; i < long_rps->num_long_term_sps + long_rps->num_long_term_pics; i++)
            ret += !!long_rps->UsedByCurrPicLt[i];
    }
    return ret;
}
