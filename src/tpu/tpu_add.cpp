#include "tpu/tpu_add.hpp"
#include <string.h>

int IveTPUAdd::init(bmctx_t *ctx, bmk1880v2_context_t *bk_ctx) {
  m_cmdbuf_subfix = "add";
  m_slice_info.ping_pong_size = 2;
  m_slice_info.ping_pong_share_tl = 1;
  m_slice_info.nums_of_tl = 3;

  return BM_SUCCESS;
}

int IveTPUAdd::runSetup(bmctx_t *ctx, bmk1880v2_context_t *bk_ctx,
                        const std::vector<bmk1880v2_tensor_tgmem_shape_t> &tg_in_slices,
                        const std::vector<bmk1880v2_tensor_tgmem_shape_t> &tg_out_slices,
                        std::vector<u32> *tl_in_idx, std::vector<u32> *tl_out_idx,
                        const bool enable_cext) {
  m_input1.clear();
  m_input2.clear();
  bmk1880v2_tensor_lmem_shape_t tl_shape;
  tl_shape.n = tg_in_slices[0].n;
  tl_shape.c = tg_in_slices[0].c;
  tl_shape.h = tg_in_slices[0].h;
  tl_shape.w = tg_in_slices[0].w;
  for (size_t i = 0; i < m_slice_info.ping_pong_size; i++) {
    m_input1.emplace_back(allocTLMem(bk_ctx, tl_shape, FMT_U8, 1));
    m_input2.emplace_back(allocTLMem(bk_ctx, tl_shape, FMT_U8, 1));
  }
  auto *high_bit_zeros = allocTLMem(bk_ctx, tl_shape, FMT_U8, 1);
  constantFillTL(ctx, bk_ctx, 0, high_bit_zeros);

  m_p_add.res_high = NULL;
  m_p_add.a_high = high_bit_zeros;
  m_p_add.b_is_const = 0;
  m_p_add.b_high = high_bit_zeros;
  m_p_add.rshift_bits = 0;
  m_p_add.relu_enable = 0;

  for (size_t pp = 0; pp < m_slice_info.ping_pong_size; pp++) {
    tl_in_idx->push_back(0 + pp * 2);
    tl_in_idx->push_back(1 + pp * 2);
    tl_out_idx->push_back(0 + pp * 2);
  }
  return BM_SUCCESS;
}

void IveTPUAdd::operation(bmctx_t *ctx, bmk1880v2_context_t *bk_ctx, u32 ping_idx) {
  m_p_add.res_low = m_input1[ping_idx];
  m_p_add.a_low = m_input1[ping_idx];
  m_p_add.b_low = m_input2[ping_idx];
  bmk1880v2_tiu_element_wise_add(bk_ctx, &m_p_add);
}