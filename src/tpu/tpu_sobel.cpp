#include "tpu/tpu_sobel.hpp"

#include <string.h>

void IveTPUSobel::setTblMgr(TblMgr *tblmgr) { mp_tblmgr = tblmgr; }

void IveTPUSobel::setKernel(IveKernel &kernel_x, IveKernel &kernel_y) {
  m_kernel_x = &kernel_x;
  m_kernel_y = &kernel_y;
  m_kernel_info.size = m_kernel_x->img.m_tg.shape.h;
  int pad = (m_kernel_info.size - 1) / 2;
  m_kernel_info.pad[0] = pad;
  m_kernel_info.pad[1] = pad;
  m_kernel_info.pad[2] = pad;
  m_kernel_info.pad[3] = pad;
}

void IveTPUSobel::magDistMethod(int method) { m_dist_method = method; }

int IveTPUSobel::init(bmctx_t *ctx, bmk1880v2_context_t *bk_ctx) {
  m_cmdbuf_subfix = "sobel";
  m_slice_info.io_fmt = FMT_BF16;
  // 1 input tl
  // 2 conv result
  // 0 a^2 + b^2 result (reuse input tl)
  // 1 buf & 1 final sqrt result
  u32 total_tables = m_dist_method == 0 ? 0 : 2;
  m_slice_info.nums_of_tl = 4 * 2;                // in bf16
  m_slice_info.nums_of_table = total_tables * 2;  // sqrt 2 table 256 * 2 in bf16
  m_kernel_info.nums_of_kernel = 4;               // 2 BF16 kernels
  return CVI_SUCCESS;
}

int IveTPUSobel::runSetup(bmctx_t *ctx, bmk1880v2_context_t *bk_ctx,
                          const std::vector<bmk1880v2_tensor_tgmem_shape_t> &tg_in_slices,
                          const std::vector<bmk1880v2_tensor_tgmem_shape_t> &tg_out_slices,
                          std::vector<u32> *tl_in_idx, std::vector<u32> *tl_out_idx,
                          const bool enable_cext) {
  bmk1880v2_tensor_lmem_shape_t tl_shape, tl_shape_out;
  tl_shape.n = tg_in_slices[0].n;
  tl_shape.c = tg_in_slices[0].c;
  tl_shape.h = tg_in_slices[0].h;
  tl_shape.w = tg_in_slices[0].w;
  tl_shape_out.n = tg_out_slices[0].n;
  tl_shape_out.c = tg_out_slices[0].c;
  tl_shape_out.h = tg_out_slices[0].h;
  tl_shape_out.w = tg_out_slices[0].w;
  auto *tl_input = allocTLMem(bk_ctx, tl_shape, FMT_BF16, 1);
  auto *tl_gx = allocTLMem(bk_ctx, tl_shape_out, FMT_BF16, 1);
  auto *tl_gy = allocTLMem(bk_ctx, tl_shape_out, FMT_BF16, 1);
  auto *tl_buf = allocTLMem(bk_ctx, tl_shape_out, FMT_BF16, 1);

  bmk1880v2_tensor_lmem_shape_t tl_kernel_s = {1, m_kernel_x->img.m_tg.shape.c, m_kernel_info.size,
                                               m_kernel_info.size};
  auto *tl_kernel_gx = allocTLMem(bk_ctx, tl_kernel_s, FMT_BF16, 1, IVETLType::KERNEL);
  auto *tl_kernel_gy = allocTLMem(bk_ctx, tl_kernel_s, FMT_BF16, 1, IVETLType::KERNEL);
  cviImgFlush2TL(ctx, bk_ctx, m_kernel_x->img, tl_kernel_gx);
  cviImgFlush2TL(ctx, bk_ctx, m_kernel_y->img, tl_kernel_gy);

  bmk1880v2_tensor_lmem_t *tl_table_data = nullptr, *tl_table_data_mantissa = nullptr;
  if (m_dist_method == 1) {
    const bmk1880v2_tensor_lmem_shape_t tl_table_s = mp_tblmgr->getTblTLShape(FMT_BF16);
    tl_table_data = allocTLMem(bk_ctx, tl_table_s, FMT_BF16, 1, IVETLType::TABLE);
    tl_table_data_mantissa = allocTLMem(bk_ctx, tl_table_s, FMT_BF16, 1, IVETLType::TABLE);
    {
      const CviImg *table_data = mp_tblmgr->sqrt(TBLSQRT::TBLSQRT_DATA);
      const CviImg *table_data_mantissa = mp_tblmgr->sqrt(TBLSQRT::TBLSQRT_MANTISSA);
      cviImg2TL(ctx, bk_ctx, *table_data, tl_table_data);
      cviImg2TL(ctx, bk_ctx, *table_data_mantissa, tl_table_data_mantissa);
    }
  }

  if (enable_cext) {
    m_p_conv_x.pad_top = 0;
    m_p_conv_x.pad_bottom = 0;
  } else {
    m_p_conv_x.pad_top = m_kernel_info.pad[2];
    m_p_conv_x.pad_bottom = m_kernel_info.pad[3];
  }
  m_p_conv_x.pad_left = m_kernel_info.pad[0];
  m_p_conv_x.pad_right = m_kernel_info.pad[1];
  m_p_conv_x.stride_w = 1;
  m_p_conv_x.stride_h = 1;
  m_p_conv_x.relu_enable = 0;
  m_p_conv_x.ins_h = 0;
  m_p_conv_x.ins_w = 0;
  m_p_conv_x.ins_last_h = 0;
  m_p_conv_x.ins_last_w = 0;
  m_p_conv_x.dilation_h = 1;
  m_p_conv_x.dilation_w = 1;
  m_p_conv_x.bias = NULL;
  m_p_conv_x.rshift_bits = 0;
  m_p_conv_x.bf16_enable = 1;
  m_p_conv_x.ifmap = tl_input;
  m_p_conv_x.ofmap = tl_gx;
  m_p_conv_x.weight = tl_kernel_gx;
  m_p_conv_y = m_p_conv_x;
  m_p_conv_y.ofmap = tl_gy;
  m_p_conv_y.weight = tl_kernel_gy;

  if (m_dist_method == 0) {
    m_p_mul_a.rshift_bits = 0;
    m_p_mul_a.relu_enable = 0;
    m_p_mul_a.bf16_enable = 1;
    m_p_mul_a.res_high = NULL;
    m_p_mul_a.res_low = tl_gy;
    m_p_mul_a.a = tl_gx;
    m_p_mul_a.b_is_const = 1;
    m_p_mul_a.b_val = convert_fp32_bf16(-1.f);

    m_p_max_a.bf16_enable = 1;
    m_p_max_a.b_is_const = 0;
    m_p_max_a.a = tl_gx;
    m_p_max_a.b = tl_gy;
    m_p_max_a.max = tl_gx;

    m_p_mul_b.rshift_bits = 0;
    m_p_mul_b.relu_enable = 0;
    m_p_mul_b.bf16_enable = 1;
    m_p_mul_b.res_high = NULL;
    m_p_mul_b.res_low = tl_buf;
    m_p_mul_b.a = tl_gy;
    m_p_mul_b.b_is_const = 1;
    m_p_mul_b.b_val = convert_fp32_bf16(-1.f);

    m_p_max_b.bf16_enable = 1;
    m_p_max_b.b_is_const = 0;
    m_p_max_b.a = tl_gy;
    m_p_max_b.b = tl_buf;
    m_p_max_b.max = tl_gy;

    m_p_add.bf16_enable = 1;
    m_p_add.relu_enable = 0;
    m_p_add.b_is_const = 0;
    m_p_add.rshift_bits = 0;
    m_p_add.a_low = tl_gx;
    m_p_add.a_high = NULL;
    m_p_add.b_low = tl_gy;
    m_p_add.b_high = NULL;
    m_p_add.res_low = tl_gx;
    m_p_add.res_high = NULL;
  } else if (m_dist_method == 1) {
    m_p_mul_a.rshift_bits = 0;
    m_p_mul_a.relu_enable = 1;
    m_p_mul_a.bf16_enable = 1;
    m_p_mul_a.res_high = NULL;
    m_p_mul_a.res_low = tl_buf;
    m_p_mul_a.a = tl_gx;
    m_p_mul_a.b_is_const = 0;
    m_p_mul_a.b = tl_gx;

    m_p_mac.res_is_int8 = 0;
    m_p_mac.lshift_bits = 0;
    m_p_mac.rshift_bits = 0;
    m_p_mac.relu_enable = 1;
    m_p_mac.bf16_enable = 1;
    m_p_mac.res_high = NULL;
    m_p_mac.res_low = tl_buf;
    m_p_mac.a = tl_gy;
    m_p_mac.b_is_const = 0;
    m_p_mac.b = tl_gy;

    m_p_sqrt.a = tl_buf;
    m_p_sqrt.res = tl_gx;
    m_p_sqrt.buf = tl_gy;
    m_p_sqrt.sqrt_table_answer = tl_table_data;
    m_p_sqrt.sqrt_table_answer_mantissa = tl_table_data_mantissa;
  }
  tl_in_idx->push_back(0);
  tl_out_idx->push_back(1);

  return CVI_SUCCESS;
}

void IveTPUSobel::operation(bmctx_t *ctx, bmk1880v2_context_t *bk_ctx, u32 ping_idx) {
  if (m_dist_method == 0) {
    bmk1880v2_tiu_bf16_depthwise_convolution(bk_ctx, &m_p_conv_x);
    bmk1880v2_tiu_bf16_element_wise_mul(bk_ctx, &m_p_mul_a);
    bmk1880v2_tiu_bf16_element_wise_max(bk_ctx, &m_p_max_a);
    bmk1880v2_tiu_bf16_depthwise_convolution(bk_ctx, &m_p_conv_y);
    bmk1880v2_tiu_bf16_element_wise_mul(bk_ctx, &m_p_mul_b);
    bmk1880v2_tiu_bf16_element_wise_max(bk_ctx, &m_p_max_b);
    bmk1880v2_tiu_bf16_element_wise_add(bk_ctx, &m_p_add);
  } else if (m_dist_method == 1) {
    bmk1880v2_tiu_bf16_depthwise_convolution(bk_ctx, &m_p_conv_x);
    bmk1880v2_tiu_bf16_depthwise_convolution(bk_ctx, &m_p_conv_y);
    bmk1880v2_tiu_bf16_element_wise_mul(bk_ctx, &m_p_mul_a);
    bmk1880v2_tiu_bf16_element_wise_mac(bk_ctx, &m_p_mac);
    bf16_emit_sqrt(bk_ctx, m_p_sqrt.a, m_p_sqrt.buf, m_p_sqrt.sqrt_table_answer,
                   m_p_sqrt.sqrt_table_answer_mantissa, m_p_sqrt.res);
  }
}