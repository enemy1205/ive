#pragma once
#include "debug.hpp"
#include "tpu_data.hpp"

#include <bmkernel/bm1880v2/1880v2_fp_convert.h>
#include <bmkernel/bm1880v2/bmkernel_1880v2.h>
#include <libbmruntime/bmruntime_bmkernel.h>

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <iostream>
#include <vector>

#define MULTIPLIER_ONLY_PACKED_DATA_SIZE 5

inline void createHandle(bmctx_t *ctx, bmk1880v2_context_t **bmk) {
  int ret = bm_init(0, ctx);
  if (ret != BM_SUCCESS) {
    fprintf(stderr, "bm_init failed, err %d\n", ret);
    exit(-1);
  }

  bmruntime_bmkernel_create(*ctx, (void **)bmk);
}

inline void destroyHandle(bmctx_t *ctx) {
  bmruntime_bmkernel_destroy(*ctx);
  bm_exit(*ctx);
}

inline void genTableBF16(u16 *table_pos_neg, bmk1880v2_tensor_lmem_shape_t *table_shape,
                         float min_value, float max_value) {
  u32 half = table_shape->h * table_shape->w / 2;
  int table_hw = table_shape->h * table_shape->w;

  // data >= 0
  for (u32 i = 0; i < half; i++) {
    table_pos_neg[i] = convert_fp32_bf16(max_value);
  }

  // data < 0
  for (u32 i = half; i < half * 2; i++) {
    table_pos_neg[i] = convert_fp32_bf16(min_value);
  }

  // duplicate channel #1 to #31
  // TODO: tensor copy
  for (u64 i = 1; i < table_shape->c; i++) {
    memcpy(&table_pos_neg[table_hw * i], &table_pos_neg[0], sizeof(u16) * table_hw);
  }
}

inline bool tgTLShapeCompare(bmk1880v2_tensor_lmem_shape_t &tl_shape,
                             bmk1880v2_tensor_tgmem_shape_t &tg_shape) {
  if (tg_shape.n == tl_shape.n && tg_shape.c == tl_shape.c && tg_shape.h == tl_shape.h &&
      tg_shape.w == tl_shape.w) {
    return true;
  }
  return false;
}

inline void extendValue2TL(bmctx_t *ctx, bmk1880v2_context_t *bk_ctx, const int value, const int c,
                           const int h, const int w, const fmt_t fmt,
                           bmk1880v2_tensor_lmem_t *lmem) {
  CviImg thresh_img(ctx, c, h, w, fmt);
  memset(thresh_img.GetVAddr(), value, c * h * w);
  bmk1880v2_tdma_tg2l_tensor_copy_param_t p;
  p.src = &thresh_img.m_tg;
  p.dst = lmem;
  bmk1880v2_tdma_g2l_tensor_copy(bk_ctx, &p);
  bmruntime_bmkernel_submit(*ctx);
  thresh_img.Free(ctx);
}

inline void constantFillTL(bmctx_t *ctx, bmk1880v2_context_t *bk_ctx, const u16 value,
                           bmk1880v2_tensor_lmem_t *lmem) {
  bmk1880v2_tdma_tg2l_tensor_fill_constant_param_t p_fill;
  p_fill.constant = value;
  p_fill.dst = lmem;

  bmk1880v2_tdma_tg2l_bf16_tensor_fill_constant(bk_ctx, &p_fill);
  bmruntime_bmkernel_submit(*ctx);
}

inline void QuantizeMultiplierSmallerThanOne(float real_multiplier, u32 *quantized_multiplier,
                                             int *right_shift) {
  float original_real_multiplier = real_multiplier;
  if (real_multiplier <= 0.f || real_multiplier > 1.f) {
    std::cerr << "Multiplier should be bigger than 0, smaller or euqal to 1." << std::endl;
    *quantized_multiplier = 0;
    *right_shift = 0;
    return;
  } else if (real_multiplier == 1.f) {
    *quantized_multiplier = (u32)(1ll << 31) - 1;
    *right_shift = 0;
  } else {
    int s = 0;
    // We want to bring the real multiplier into the interval [1/2, 1).
    // We can do so by multiplying it by two, and recording how many times
    // we multiplied by two so that we can compensate that by a right
    // shift by the same amount.
    while (real_multiplier < 0.5f) {
      real_multiplier *= 2.0f;
      s++;
    }
    // Now that the real multiplier is in [1/2, 1), we convert it
    // into a fixed-point number.
    s64 q = static_cast<s64>(round(real_multiplier * (1ll << 31)));
    assert(q <= (1ll << 31));
    // Handle the special case when the real multiplier was so close to 1
    // that its fixed-point approximation was undistinguishable from 1.
    // We handle this by dividing it by two, and remembering to decrement
    // the right shift amount.
    if (q == (1ll << 31)) {
      q /= 2;
      s--;
    }
    assert(s >= 0);
    assert(q <= (s64)LONG_MAX);
    *quantized_multiplier = (u32)q;
    *right_shift = s;
  }
  IVE_DEBUG("    QuantizeMultiplierSmallerThanOne: %f -> multiplier %d, rshift %d\n",
            original_real_multiplier, *quantized_multiplier, *right_shift);
}

inline void pack_per_chan_cal_data(u32 channels, bool has_bias, s32 *bias, u32 *multiplier,
                                   s8 *shift, u8 *packed_data) {
  u8 *ptr = packed_data;

  for (u32 i = 0; i < channels; i++) {
    if (has_bias) {
      u32 val = (u32)bias[i];
      *ptr = val & 0xff;
      ptr++;
      *ptr = (val >> 8) & 0xff;
      ptr++;
      *ptr = (val >> 16) & 0xff;
      ptr++;
      *ptr = (val >> 24) & 0xff;
      ptr++;
    }

    {
      u32 val = multiplier[i];
      *ptr = val & 0xff;
      ptr++;
      *ptr = (val >> 8) & 0xff;
      ptr++;
      *ptr = (val >> 16) & 0xff;
      ptr++;
      *ptr = (val >> 24) & 0xff;
      ptr++;
    }

    {
      u8 val = shift[i];
      *ptr = val;
      ptr++;
    }
  }
}

inline void getPackedMultiplierArrayBuffer(const u32 c, const u32 &quantized_multiplier,
                                           const int &right_shift, u8 *cal_data) {
  // Create tl_multiplier
  u32 *multiplier_data = new u32[c];
  s8 *shift_data = new s8[c];
  for (unsigned int i = 0; i < c; ++i) {
    // multipliers typically range in [2^30 ; 2^31 - 1].
    // Values in [0, 2^30 - 1] are normally unused, but harmless.
    // Thus a good way to randomize multipliers is to subtract from them
    // a random value smaller than 2^30 but still significant compared to it.
    multiplier_data[i] = quantized_multiplier;

    // Our H/W only supports right shift
    shift_data[i] = right_shift > 0 ? right_shift : 0;

#ifdef ENABLE_DEBUG_MSG
    printf("      [oc=%d] multiplier_data %d, shift_data %d\n", i, p_param->multiplier_data[i],
           p_param->shift_data[i]);
#endif
  }

  pack_per_chan_cal_data(c, 0, NULL, multiplier_data, shift_data, cal_data);
  delete[] multiplier_data;
  delete[] shift_data;
}

inline u8 *getPackedMultiplierArray(const u32 c, const u32 &quantized_multiplier,
                                    const int &right_shift) {
  const int per_chan_cal_data_size = MULTIPLIER_ONLY_PACKED_DATA_SIZE;  // p_param->has_bias ? 9 :
                                                                        // 5;  // bias(4) +
                                                                        // multiplier(4) + shift(1)
  const int cal_data_size = c * per_chan_cal_data_size;
  u8 *cal_data = (u8 *)malloc(cal_data_size);
  getPackedMultiplierArrayBuffer(c, quantized_multiplier, right_shift, cal_data);
  return cal_data;
}
