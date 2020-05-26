#pragma once
#include "tpu_data.hpp"
#include "utils.hpp"

#include <string.h>
#include <iostream>
#include <vector>

enum IVETLType { DATA, KERNEL, TABLE };

class IveCore {
 public:
  IveCore();
  const unsigned int getNpuNum(cvk_context_t *cvk_ctx) const { return cvk_ctx->info.npu_num; }
  virtual int init(bmctx_t *ctx, cvk_context_t *cvk_ctx) = 0;
  int run(bmctx_t *ctx, cvk_context_t *cvk_ctx, std::vector<CviImg> &input,
          std::vector<CviImg> *output, bool legacy_mode = false);

 protected:
  cvk_tl_t *allocTLMem(cvk_context_t *cvk_ctx, cvk_tl_shape_t tl_shape, cvk_fmt_t fmt, int eu_align,
                       IVETLType type = IVETLType::DATA);
  virtual int sliceSetup(SliceRes &slice_res, SliceRes *tg_in_res, SliceRes *tg_out_res);
  virtual int runSetup(bmctx_t *ctx, cvk_context_t *cvk_ctx,
                       const std::vector<cvk_tg_shape_t> &tg_in_slices,
                       const std::vector<cvk_tg_shape_t> &tg_out_slices,
                       std::vector<uint32_t> *tl_in_idx, std::vector<uint32_t> *tl_out_idx,
                       const bool enable_cext) = 0;
  virtual void operation(bmctx_t *ctx, cvk_context_t *cvk_ctx, uint32_t ping_idx) = 0;
  virtual void beforeSubmit(bmctx_t *ctx, cvk_context_t *cvk_ctx, std::vector<CviImg> &input,
                            std::vector<CviImg> *output);
  virtual int postProcess(bmctx_t *ctx);

  uint32_t m_nums_of_input = 1;
  uint32_t m_nums_of_output = 1;
  SliceInfo m_slice_info;
  kernelInfo m_kernel_info;
  std::vector<IVETLType> m_tl_type;
  std::vector<cvk_tl_t *> m_tl_vec;
  std::string m_cmdbuf_subfix;
  bool m_force_use_ext = false;

 private:
  int getSlice(const uint32_t nums_of_lmem, const uint32_t nums_of_table,
               const uint32_t fixed_lmem_size, const uint32_t n, const uint32_t c, const uint32_t h,
               const uint32_t w, const uint32_t table_size, const kernelInfo kernel_info,
               const int npu_num, sliceUnit *unit_h, sliceUnit *unit_w, const bool enable_cext);
  int freeTLMems(cvk_context_t *cvk_ctx);
  int runSingleSizeKernel(bmctx_t *ctx, cvk_context_t *cvk_ctx, std::vector<CviImg> &input,
                          std::vector<CviImg> *output, bool enable_min_max = false);
  int runSingleSizeExtKernel(bmctx_t *ctx, cvk_context_t *cvk_ctx, std::vector<CviImg> &input,
                             std::vector<CviImg> *output, bool enable_min_max = false);
  int runNoKernel(bmctx_t *ctx, cvk_context_t *cvk_ctx, std::vector<CviImg> &input,
                  std::vector<CviImg> *output, bool enable_min_max = false);

  bool m_write_cmdbuf = false;
  cvk_chip_info_t m_chip_info;
  uint32_t m_table_per_channel_size = 0;
};