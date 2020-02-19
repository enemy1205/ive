#include "ive.h"
#include <glog/logging.h>
#include "kernel_generator.hpp"
#include "tpu_data.hpp"

#include "tpu/tpu_add.hpp"
#include "tpu/tpu_and.hpp"
#include "tpu/tpu_block.hpp"
#include "tpu/tpu_copy.hpp"
#include "tpu/tpu_filter.hpp"
#include "tpu/tpu_magandang.hpp"
#include "tpu/tpu_morph.hpp"
#include "tpu/tpu_or.hpp"
#include "tpu/tpu_sad.hpp"
#include "tpu/tpu_sobel.hpp"
#include "tpu/tpu_sub.hpp"
#include "tpu/tpu_threshold.hpp"
#include "tpu/tpu_xor.hpp"

#include <cmath>

struct TPU_HANDLE {
  IveTPUAdd t_add;
  IveTPUAnd t_and;
  IveTPUBlock t_block;
  IveTPUBlockBF16 t_block_bf16;
  IveTPUCopyInterval t_copy_int;
  IveTPUErode t_erode;
  IveTPUFilter t_filter;
  IveTPUFilterBF16 t_filter_bf16;
  IveTPUMagAndAng t_magandang;
  IveTPUOr t_or;
  IveTPUSAD t_sad;
  IveTPUSobelGradOnly t_sobel_gradonly;
  IveTPUSobel t_sobel;
  IveTPUSubAbs t_sub_abs;
  IveTPUSub t_sub;
  IveTPUThreshold t_thresh;
  IveTPUThresholdHighLow t_thresh_hl;
  IveTPUThresholdSlope t_thresh_s;
  IveTPUXOr t_xor;
};

struct IVE_HANDLE_CTX {
  bmctx_t ctx;
  bmk1880v2_context_t *bk_ctx;
  TPU_HANDLE t_h;
  // VIP
};

void CVI_SYS_LOGGING(char *argv0) { google::InitGoogleLogging(argv0); }

IVE_HANDLE CVI_IVE_CreateHandle() {
  IVE_HANDLE_CTX *handle_ctx = new IVE_HANDLE_CTX;
  createHandle(&handle_ctx->ctx, &handle_ctx->bk_ctx);
  return (void *)handle_ctx;
}

CVI_S32 CVI_IVE_DestroyHandle(IVE_HANDLE pIveHandle) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  destroyHandle(&handle_ctx->ctx);
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_Flush(IVE_HANDLE pIveHandle) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  bmruntime_bmkernel_submit(handle_ctx->ctx);
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_CreateMemInfo(IVE_HANDLE pIveHandle, IVE_MEM_INFO_S *pstMemInfo, CVI_U32 u32Size) {
  pstMemInfo->u32PhyAddr = 0;
  pstMemInfo->pu8VirAddr = new CVI_U8[u32Size];
  pstMemInfo->u32Size = u32Size;
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_CreateImage(IVE_HANDLE pIveHandle, IVE_IMAGE_S *pstImg, IVE_IMAGE_TYPE_E enType,
                            u16 u16Width, u16 u16Height) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  int c = 1;
  int fmt_size = 1;
  fmt_t fmt = FMT_U8;
  switch (enType) {
    case IVE_IMAGE_TYPE_S8C1:
      fmt = FMT_I8;
      break;
    case IVE_IMAGE_TYPE_U8C1:
      break;
    case IVE_IMAGE_TYPE_U8C3_PACKAGE:
    case IVE_IMAGE_TYPE_U8C3_PLANAR:
      c = 3;
      break;
    case IVE_IMAGE_TYPE_BF16C1:
      fmt_size = 2;
      fmt = FMT_BF16;
      break;
    case IVE_IMAGE_TYPE_U16C1:
      fmt_size = 2;
      fmt = FMT_U16;
      break;
    case IVE_IMAGE_TYPE_S16C1:
      fmt_size = 2;
      fmt = FMT_I16;
      break;
    case IVE_IMAGE_TYPE_U32C1:
      fmt_size = 4;
      fmt = FMT_INVALID;
      break;
    case IVE_IMAGE_TYPE_FP32C1:
      fmt_size = 4;
      fmt = FMT_F32;
      break;
    default:
      std::cerr << "Not supported enType " << enType << std::endl;
      return CVI_FAILURE;
      break;
  }
  int total_size = c * u16Width * u16Height * fmt_size;
  // Special case for unsupported I32/U32 images
  // FIXME: Put thosinto bmkernel, bmruntime
  if (fmt == FMT_INVALID) {
    pstImg->tpu_block = NULL;
    pstImg->enType = enType;
    pstImg->u16Width = u16Width;
    pstImg->u16Height = u16Height;
    pstImg->u16Reserved = fmt_size;
    int img_sz = pstImg->u16Width * pstImg->u16Height * fmt_size * c;
    uint8_t *arr = new uint8_t[img_sz];
    for (size_t i = 0; i < (size_t)c; i++) {
      pstImg->pu8VirAddr[i] = arr + i * img_sz;
      pstImg->u64PhyAddr[i] = i * img_sz;
      pstImg->u16Stride[i] = pstImg->u16Width;
    }

    for (size_t i = c; i < 3; i++) {
      pstImg->pu8VirAddr[i] = NULL;
      pstImg->u64PhyAddr[i] = -1;
      pstImg->u16Stride[i] = 0;
    }
    return CVI_SUCCESS;
  }

  auto *cpp_img = new CviImg(&handle_ctx->ctx, c, u16Height, u16Width, fmt);
  pstImg->tpu_block = reinterpret_cast<CVI_IMG *>(cpp_img);

  pstImg->enType = enType;
  pstImg->u16Width = cpp_img->m_tg.shape.w;
  pstImg->u16Height = cpp_img->m_tg.shape.h;
  pstImg->u16Reserved = fmt_size;

  int img_sz = cpp_img->m_tg.stride.h * pstImg->u16Height * fmt_size;
  for (size_t i = 0; i < cpp_img->m_tg.shape.c; i++) {
    pstImg->pu8VirAddr[i] = cpp_img->GetVAddr() + i * img_sz;
    pstImg->u64PhyAddr[i] = cpp_img->GetPAddr() + i * img_sz;
    pstImg->u16Stride[i] = cpp_img->m_tg.stride.h;
  }

  for (size_t i = cpp_img->m_tg.shape.c; i < 3; i++) {
    pstImg->pu8VirAddr[i] = NULL;
    pstImg->u64PhyAddr[i] = -1;
    pstImg->u16Stride[i] = 0;
  }
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_SubImage(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDst,
                         CVI_U16 u16X1, CVI_U16 u16Y1, CVI_U16 u16X2, CVI_U16 u16Y2) {
  if (pstSrc->tpu_block == NULL) {
    std::cerr << "Currently not support I32/ U32 sub image." << std::endl;
    return CVI_FAILURE;
  }
  if (u16X1 >= u16X2 || u16Y1 >= u16Y2) {
    std::cerr << "(X1, Y1) must smaller than (X2, Y2)." << std::endl;
    return CVI_FAILURE;
  }
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  auto *src_img = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
  auto *cpp_img = new CviImg(&handle_ctx->ctx, *src_img, u16X1, u16Y1, u16X2, u16Y2);
  if (cpp_img == nullptr) {
    return CVI_FAILURE;
  }
  pstDst->tpu_block = reinterpret_cast<CVI_IMG *>(cpp_img);

  pstDst->enType = pstSrc->enType;
  pstDst->u16Width = cpp_img->m_tg.shape.w;
  pstDst->u16Height = cpp_img->m_tg.shape.h;
  pstDst->u16Reserved = pstSrc->u16Reserved;

  int img_sz = cpp_img->m_tg.stride.h * pstDst->u16Height * pstSrc->u16Reserved;
  for (size_t i = 0; i < cpp_img->m_tg.shape.c; i++) {
    pstDst->pu8VirAddr[i] = cpp_img->GetVAddr() + i * img_sz;
    pstDst->u64PhyAddr[i] = cpp_img->GetPAddr() + i * img_sz;
    pstDst->u16Stride[i] = cpp_img->m_tg.stride.h;
  }

  for (size_t i = cpp_img->m_tg.shape.c; i < 3; i++) {
    pstDst->pu8VirAddr[i] = NULL;
    pstDst->u64PhyAddr[i] = -1;
    pstDst->u16Stride[i] = 0;
  }
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_Free(IVE_HANDLE pIveHandle, IVE_MEM_INFO_S *pstMemInfo) {
  delete[] pstMemInfo->pu8VirAddr;
  pstMemInfo->u32Size = 0;
  return CVI_SUCCESS;
}

CVI_S32 CVI_SYS_Free(IVE_HANDLE pIveHandle, IVE_IMAGE_S *pstImg) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  if (pstImg->tpu_block == NULL) {
    delete[] pstImg->pu8VirAddr[0];
  } else {
    auto *cpp_img = reinterpret_cast<CviImg *>(pstImg->tpu_block);
    cpp_img->Free(&handle_ctx->ctx);
    delete cpp_img;
  }
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_DMA(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDst,
                    IVE_DMA_CTRL_S *pstDmaCtrl, bool bInstant) {
  int ret = CVI_NOT_SUPPORTED;
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  if (pstDmaCtrl->enMode == IVE_DMA_MODE_DIRECT_COPY) {
    ret = CVI_SUCCESS;
#ifdef USE_CPU_COPY
    uint size = pstSrc->u16Stride[0] * pstSrc->u16Height;
    memcpy(pstDst->pu8VirAddr[0], pstSrc->pu8VirAddr[0], size);
#else
    CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
    CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
    std::vector<CviImg> inputs = {*cpp_src};
    std::vector<CviImg> outputs = {*cpp_dst};

    IveTPUCopyDirect::run(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs, &outputs);
#endif
  } else if (pstDmaCtrl->enMode == IVE_DMA_MODE_INTERVAL_COPY) {
    handle_ctx->t_h.t_copy_int.setInvertal(pstDmaCtrl->u8HorSegSize, pstDmaCtrl->u8VerSegRows);
    handle_ctx->t_h.t_copy_int.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
    CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
    std::vector<CviImg> inputs = {*cpp_src};
    std::vector<CviImg> outputs = {*cpp_dst};

    handle_ctx->t_h.t_copy_int.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                   &outputs);
  }
  return ret;
}

CVI_S32 CVI_IVE_ImageTypeConvert(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc,
                                 IVE_DST_IMAGE_S *pstDst, IVE_ITC_CRTL_S *pstItcCtrl,
                                 bool bInstant) {
  CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  if (pstItcCtrl->enType == IVE_ITC_NORMALIZE) {
    if (cpp_src->m_tg.fmt == FMT_BF16 && cpp_dst->m_tg.fmt == FMT_F32) {
      u16 *src_ptr = (u16 *)cpp_src->GetVAddr();
      float *dst_ptr = (float *)cpp_dst->GetVAddr();

      for (u64 i = 0; i < cpp_src->GetImgSize() / 2; i++) {
        dst_ptr[i] = convert_bf16_fp32(src_ptr[i]);
      }
    } else if (cpp_src->m_tg.fmt == FMT_BF16 && cpp_dst->m_tg.fmt == FMT_U16) {
      u16 *src_ptr = (u16 *)cpp_src->GetVAddr();
      u16 *dst_ptr = (u16 *)cpp_dst->GetVAddr();
      u16 *tmp_arr = new u16[cpp_src->GetImgSize()];
      u16 min = 65535, max = 0;
      u64 img_size = cpp_src->m_tg.shape.c * cpp_src->m_tg.shape.h * cpp_src->m_tg.shape.w;
      for (u64 i = 0; i < img_size; i++) {
        tmp_arr[i] = src_ptr[i];
        if (tmp_arr[i] < min) {
          min = tmp_arr[i];
        }
        if (tmp_arr[i] > max) {
          max = tmp_arr[i];
        }
      }
      float multiplier = 65535.f / (float)(max - min);
      int s8_offset = cpp_dst->m_tg.fmt == FMT_U16 ? 0 : 32768;
      for (u64 i = 0; i < img_size; i++) {
        dst_ptr[i] = std::round((int)(multiplier * (tmp_arr[i] - min)) - s8_offset);
      }
      delete[] tmp_arr;
    } else if (cpp_src->m_tg.fmt == FMT_U16 &&
               (cpp_dst->m_tg.fmt == FMT_U8 || cpp_dst->m_tg.fmt == FMT_I8)) {
      u16 *src_ptr = (u16 *)cpp_src->GetVAddr();
      u8 *dst_ptr = (u8 *)cpp_dst->GetVAddr();
      u16 *tmp_arr = new u16[cpp_src->GetImgSize()];
      u16 min = 65535, max = 0;
      u64 img_size = cpp_src->m_tg.shape.c * cpp_src->m_tg.shape.h * cpp_src->m_tg.shape.w;
      for (u64 i = 0; i < img_size; i++) {
        tmp_arr[i] = src_ptr[i];
        if (tmp_arr[i] < min) {
          min = tmp_arr[i];
        }
        if (tmp_arr[i] > max) {
          max = tmp_arr[i];
        }
      }
      float multiplier = 255.f / (float)(max - min);
      short s8_offset = cpp_dst->m_tg.fmt == FMT_U8 ? 0 : 128;
      for (u64 i = 0; i < img_size; i++) {
        dst_ptr[i] = std::round((int)(multiplier * (tmp_arr[i] - min)) - s8_offset);
      }
      delete[] tmp_arr;
    } else if (cpp_src->m_tg.fmt == FMT_BF16 &&
               (cpp_dst->m_tg.fmt == FMT_U8 || cpp_dst->m_tg.fmt == FMT_I8)) {
      u16 *src_ptr = (u16 *)cpp_src->GetVAddr();
      u8 *dst_ptr = (u8 *)cpp_dst->GetVAddr();
      float *tmp_arr = new float[cpp_src->GetImgSize()];
      float min = 10000000.f, max = -100000000.f;
      u64 img_size = cpp_src->m_tg.shape.c * cpp_src->m_tg.shape.h * cpp_src->m_tg.shape.w;
      for (u64 i = 0; i < img_size; i++) {
        tmp_arr[i] = convert_bf16_fp32(src_ptr[i]);
        if (tmp_arr[i] < min) {
          min = tmp_arr[i];
        }
        if (tmp_arr[i] > max) {
          max = tmp_arr[i];
        }
      }
      float multiplier = 255.f / (max - min);
      short s8_offset = cpp_dst->m_tg.fmt == FMT_U8 ? 0 : 128;
      for (u64 i = 0; i < img_size; i++) {
        dst_ptr[i] = std::round((u8)(multiplier * (tmp_arr[i] - min)) - s8_offset);
      }
      delete[] tmp_arr;
    }
  } else if (pstItcCtrl->enType == IVE_ITC_SATURATE) {
    if (cpp_src->m_tg.fmt == FMT_BF16 && cpp_dst->m_tg.fmt == FMT_F32) {
      u16 *src_ptr = (u16 *)cpp_src->GetVAddr();
      float *dst_ptr = (float *)cpp_dst->GetVAddr();

      for (u64 i = 0; i < cpp_src->GetImgSize() / 2; i++) {
        dst_ptr[i] = convert_bf16_fp32(src_ptr[i]);
      }
    } else if (cpp_src->m_tg.fmt == FMT_BF16 && cpp_dst->m_tg.fmt == FMT_U16) {
      u16 *src_ptr = (u16 *)cpp_src->GetVAddr();
      u16 *dst_ptr = (u16 *)cpp_dst->GetVAddr();

      for (u64 i = 0; i < cpp_src->GetImgSize() / 2; i++) {
        dst_ptr[i] = std::round(convert_bf16_fp32(src_ptr[i]));
      }
    } else if ((cpp_src->m_tg.fmt == FMT_BF16 || cpp_src->m_tg.fmt == FMT_U8 ||
                cpp_src->m_tg.fmt == FMT_I8) &&
               (cpp_dst->m_tg.fmt == FMT_BF16 || cpp_dst->m_tg.fmt == FMT_U8 ||
                cpp_dst->m_tg.fmt == FMT_I8)) {
      IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
      CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
      CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
      std::vector<CviImg> inputs = {*cpp_src};
      std::vector<CviImg> outputs = {*cpp_dst};

      IveTPUCopyDirect::run(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs, &outputs);
    } else {
      std::cerr << "Unsupported input output image type ( " << cpp_src->m_tg.fmt << ", "
                << cpp_dst->m_tg.fmt << ")." << std::endl;
      return CVI_NOT_SUPPORTED;
    }
  } else {
    std::cerr << "Unsupported enType " << pstItcCtrl->enType << std::endl;
    return CVI_NOT_SUPPORTED;
  }
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_Add(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc1, IVE_SRC_IMAGE_S *pstSrc2,
                    IVE_DST_IMAGE_S *pstDst, IVE_ADD_CTRL_S *ctrl, bool bInstant) {
  int ret = CVI_NOT_SUPPORTED;
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);

  float *x = (float *)&ctrl->u0q16X;
  float *y = (float *)&ctrl->u0q16Y;
  if ((*x == 1 && *y == 1) || (*x == 0 && *y == 0)) {
    ret = CVI_SUCCESS;
    handle_ctx->t_h.t_add.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrc1->tpu_block);
    CviImg *cpp_src2 = reinterpret_cast<CviImg *>(pstSrc2->tpu_block);
    CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
    std::vector<CviImg> inputs = {*cpp_src1, *cpp_src2};
    std::vector<CviImg> outputs = {*cpp_dst};

    handle_ctx->t_h.t_add.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                              &outputs);
  }
  return ret;
}

CVI_S32 CVI_IVE_And(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc1, IVE_SRC_IMAGE_S *pstSrc2,
                    IVE_DST_IMAGE_S *pstDst, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  handle_ctx->t_h.t_and.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
  CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrc1->tpu_block);
  CviImg *cpp_src2 = reinterpret_cast<CviImg *>(pstSrc2->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src1, *cpp_src2};
  std::vector<CviImg> outputs = {*cpp_dst};

  handle_ctx->t_h.t_and.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs, &outputs);
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_BLOCK(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDst,
                      IVE_BLOCK_CTRL_S *pstBlkCtrl, bool bInstant) {
  CVI_U32 cell_size = pstBlkCtrl->cell_size;
  if (pstDst->u16Width != (pstSrc->u16Width / cell_size)) {
    std::cerr << "Dst block width not match! Src: " << pstSrc->u16Width
              << ", dst: " << pstDst->u16Width << ", cell size: " << cell_size << std::endl;
    return CVI_FAILURE;
  }
  if (pstDst->u16Height != (pstSrc->u16Height / cell_size)) {
    std::cerr << "Dst block height not match! Src: " << pstSrc->u16Height
              << ", dst: " << pstDst->u16Height << ", cell size: " << cell_size << std::endl;
    return CVI_FAILURE;
  }

  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src};
  std::vector<CviImg> outputs = {*cpp_dst};
  if ((cpp_src->m_tg.fmt != FMT_U8 && cpp_src->m_tg.fmt != FMT_BF16) &&
      (cpp_dst->m_tg.fmt != FMT_U8 && cpp_dst->m_tg.fmt != FMT_BF16)) {
    std::cerr << "CVI Block only supports U8/ BF16." << std::endl;
    return CVI_NOT_SUPPORTED;
  }
  if (cpp_src->m_tg.fmt == FMT_U8 && cpp_dst->m_tg.fmt == FMT_U8) {
    handle_ctx->t_h.t_block.setBinNum(pstBlkCtrl->bin_num);
    handle_ctx->t_h.t_block.setCellSize(cell_size, cpp_src->m_tg.shape.c);
    handle_ctx->t_h.t_block.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_block.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                &outputs);
  } else {
    handle_ctx->t_h.t_block_bf16.setBinNum(pstBlkCtrl->bin_num);
    handle_ctx->t_h.t_block_bf16.setCellSize(cell_size, cpp_src->m_tg.shape.c);
    handle_ctx->t_h.t_block_bf16.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_block_bf16.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                     &outputs);
  }
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_Dilate(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDst,
                       IVE_DILATE_CTRL_S *pstDilateCtrl, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  handle_ctx->t_h.t_filter.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
  CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src};
  std::vector<CviImg> outputs = {*cpp_dst};

  CviImg cimg(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 5, 5, FMT_U8);
  IveKernel kernel;
  kernel.img = cimg;
  kernel.img.GetVAddr();
  for (size_t i = 0; i < cpp_src->m_tg.shape.c; i++) {
    memcpy(kernel.img.GetVAddr() + i * 25, pstDilateCtrl->au8Mask, 25);
  }
  kernel.multiplier.f = 1.f;
  QuantizeMultiplierSmallerThanOne(kernel.multiplier.f, &kernel.multiplier.base,
                                   &kernel.multiplier.shift);
  handle_ctx->t_h.t_filter.setKernel(kernel);
  handle_ctx->t_h.t_filter.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                               &outputs);
  kernel.img.Free(&handle_ctx->ctx);
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_Erode(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDst,
                      IVE_ERODE_CTRL_S *pstErodeCtrl, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  handle_ctx->t_h.t_erode.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
  CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src};
  std::vector<CviImg> outputs = {*cpp_dst};

  CviImg cimg(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 5, 5, FMT_U8);
  IveKernel kernel;
  kernel.img = cimg;
  kernel.img.GetVAddr();
  for (size_t i = 0; i < cpp_src->m_tg.shape.c; i++) {
    memcpy(kernel.img.GetVAddr() + i * 25, pstErodeCtrl->au8Mask, 25);
  }
  kernel.multiplier.f = 1.f;
  QuantizeMultiplierSmallerThanOne(kernel.multiplier.f, &kernel.multiplier.base,
                                   &kernel.multiplier.shift);
  handle_ctx->t_h.t_erode.setKernel(kernel);
  handle_ctx->t_h.t_erode.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                              &outputs);
  kernel.img.Free(&handle_ctx->ctx);
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_Filter(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDst,
                       IVE_FILTER_CTRL_S *pstFltCtrl, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  handle_ctx->t_h.t_filter.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
  CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src};
  std::vector<CviImg> outputs = {*cpp_dst};

  CviImg cimg(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, FMT_I8);
  IveKernel kernel;
  kernel.img = cimg;
  kernel.img.GetVAddr();
  for (size_t i = 0; i < cpp_src->m_tg.shape.c; i++) {
    memcpy(kernel.img.GetVAddr() + i * 9, pstFltCtrl->as8Mask, 9);
  }
  kernel.multiplier.f = 1.f / pstFltCtrl->u8Norm;
  QuantizeMultiplierSmallerThanOne(kernel.multiplier.f, &kernel.multiplier.base,
                                   &kernel.multiplier.shift);
  handle_ctx->t_h.t_filter.setKernel(kernel);
  handle_ctx->t_h.t_filter.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                               &outputs);
  kernel.img.Free(&handle_ctx->ctx);
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_HOG(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDstH,
                    IVE_DST_IMAGE_S *pstDstV, IVE_DST_IMAGE_S *pstDstMag,
                    IVE_DST_IMAGE_S *pstDstAng, IVE_DST_IMAGE_S *pstDstBlk,
                    IVE_DST_IMAGE_S *pstDstHist, IVE_HOG_CTRL_S *pstHogCtrl, bool bInstant) {
  if (pstDstHist->enType != IVE_IMAGE_TYPE_U32C1 && pstDstHist->tpu_block != NULL) {
    std::cerr << "Histogram enType only supports IVE_IMAGE_TYPE_U32C1." << std::endl;
    return CVI_FAILURE;
  }
  if (pstDstHist->u16Height != 1 || pstDstHist->u16Width != pstHogCtrl->bin_num) {
    std::cerr << "Histogram shape size not match the bin num! " << std::endl;
    return CVI_FAILURE;
  }
  IVE_SOBEL_CTRL_S iveSblCtrl;
  iveSblCtrl.enOutCtrl = IVE_SOBEL_OUT_CTRL_BOTH;
  if (CVI_IVE_Sobel(pIveHandle, pstSrc, pstDstH, pstDstV, &iveSblCtrl, 0) != CVI_SUCCESS) {
    return CVI_FAILURE;
  }
  IVE_MAG_AND_ANG_CTRL iveMaaCtrl;
  iveMaaCtrl.enOutCtrl = IVE_MAG_AND_ANG_OUT_CTRL_ANG;
  iveMaaCtrl.no_negative = true;
  if (CVI_IVE_MagAndAng(pIveHandle, pstDstH, pstDstV, pstDstMag, pstDstAng, &iveMaaCtrl, 0) !=
      CVI_SUCCESS) {
    return CVI_FAILURE;
  }

  // Create Block
  IVE_BLOCK_CTRL_S iveBlkCtrl;
  iveBlkCtrl.bin_num = 1;
  iveBlkCtrl.cell_size = pstHogCtrl->cell_size;
  if (CVI_IVE_BLOCK(pIveHandle, pstDstAng, pstDstBlk, &iveBlkCtrl, 0) != CVI_SUCCESS) {
    return CVI_FAILURE;
  }
  // Get Histogram
  u16 *blk_ptr = (u16 *)pstDstBlk->pu8VirAddr[0];
  u32 *hog_ptr = (u32 *)pstDstHist->pu8VirAddr[0];
  memset(hog_ptr, 0, pstDstHist->u16Width * pstDstHist->u16Height * sizeof(u32));
  u16 div = 360 / pstHogCtrl->bin_num;
  if (iveMaaCtrl.no_negative) {
    for (u64 i = 0; i < pstDstBlk->u16Width * pstDstBlk->u16Height; i++) {
      float degree = convert_bf16_fp32(blk_ptr[i]);
      u32 index = (u32)(degree / div);
      if (index > pstHogCtrl->bin_num) {
        std::cout << "Histogram index out of range. Original degree " << degree << std::endl;
        return CVI_FAILURE;
      }
      hog_ptr[index]++;
    }
  } else {
    for (u64 i = 0; i < pstDstBlk->u16Width * pstDstBlk->u16Height; i++) {
      float degree = convert_bf16_fp32(blk_ptr[i]);
      u32 index = degree < 0 ? (u32)((360 + degree) / div) : (u32)(degree / div);
      if (index > pstHogCtrl->bin_num) {
        std::cout << "Histogram index out of range. Original degree " << degree << std::endl;
        return CVI_FAILURE;
      }
      hog_ptr[index]++;
    }
  }
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_MagAndAng(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrcH, IVE_SRC_IMAGE_S *pstSrcV,
                          IVE_DST_IMAGE_S *pstDstMag, IVE_DST_IMAGE_S *pstDstAng,
                          IVE_MAG_AND_ANG_CTRL_S *pstMaaCtrl, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrcH->tpu_block);
  CviImg *cpp_src2 = reinterpret_cast<CviImg *>(pstSrcV->tpu_block);
  CviImg *cpp_dst =
      (pstDstMag != NULL) ? reinterpret_cast<CviImg *>(pstDstMag->tpu_block) : nullptr;
  CviImg *cpp_dst2 =
      (pstDstAng != NULL) ? reinterpret_cast<CviImg *>(pstDstAng->tpu_block) : nullptr;
  std::vector<CviImg> inputs = {*cpp_src1, *cpp_src2};
  std::vector<CviImg> outputs;
  switch (pstMaaCtrl->enOutCtrl) {
    case IVE_MAG_AND_ANG_OUT_CTRL_MAG: {
      if (pstDstMag == NULL) {
        std::cerr << "Under mode IVE_MAG_AND_ANG_OUT_CTRL_MAG Magnitude image cannot be NULL."
                  << std::endl;
        return CVI_FAILURE;
      }
      handle_ctx->t_h.t_magandang.exportOption(true, false);
      outputs.emplace_back(*cpp_dst);
    } break;
    case IVE_MAG_AND_ANG_OUT_CTRL_ANG: {
      if (pstDstAng == NULL) {
        std::cerr << "Under mode IVE_MAG_AND_ANG_OUT_CTRL_ANG angle image cannot be NULL."
                  << std::endl;
        return CVI_FAILURE;
      }
      handle_ctx->t_h.t_magandang.exportOption(false, true);
      outputs.emplace_back(*cpp_dst2);
    } break;
    case IVE_MAG_AND_ANG_OUT_CTRL_MAG_AND_ANG: {
      if (pstDstMag == NULL || pstDstAng == NULL) {
        std::cerr << "Under mode IVE_MAG_AND_ANG_OUT_CTRL_MAG_AND_ANG both outputs cannot be NULL."
                  << std::endl;
        return CVI_FAILURE;
      }
      handle_ctx->t_h.t_magandang.exportOption(true, true);
      outputs.emplace_back(*cpp_dst);
      outputs.emplace_back(*cpp_dst2);
    } break;
    default:
      std::cerr << "Not supported Mag and Angle type." << std::endl;
      return CVI_FAILURE;
      break;
  }
  handle_ctx->t_h.t_magandang.noNegative(pstMaaCtrl->no_negative);
  handle_ctx->t_h.t_magandang.init(&handle_ctx->ctx, handle_ctx->bk_ctx);

  handle_ctx->t_h.t_magandang.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                  &outputs);
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_NormGrad(IVE_HANDLE *pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDstH,
                         IVE_DST_IMAGE_S *pstDstV, IVE_DST_IMAGE_S *pstDstHV,
                         IVE_NORM_GRAD_CTRL_S *pstNormGradCtrl, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src};
  std::vector<CviImg> outputs;
  if (pstNormGradCtrl->enOutCtrl == IVE_NORM_GRAD_OUT_CTRL_HOR_AND_VER) {
    IVE_IMAGE_S dstH_BF16, dstV_BF16;
    CVI_IVE_CreateImage(pIveHandle, &dstH_BF16, IVE_IMAGE_TYPE_BF16C1, pstSrc->u16Width,
                        pstSrc->u16Height);
    CVI_IVE_CreateImage(pIveHandle, &dstV_BF16, IVE_IMAGE_TYPE_BF16C1, pstSrc->u16Width,
                        pstSrc->u16Height);
    CviImg *cpp_dstv = reinterpret_cast<CviImg *>(dstV_BF16.tpu_block);
    CviImg *cpp_dsth = reinterpret_cast<CviImg *>(dstH_BF16.tpu_block);
    outputs.emplace_back(*cpp_dstv);
    outputs.emplace_back(*cpp_dsth);
    IveKernel kernel_w =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_X);
    IveKernel kernel_h =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_Y);
    handle_ctx->t_h.t_sobel_gradonly.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_sobel_gradonly.setKernel(kernel_w, kernel_h);
    handle_ctx->t_h.t_sobel_gradonly.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx,
                                                         inputs, &outputs);
    kernel_w.img.Free(&handle_ctx->ctx);
    kernel_h.img.Free(&handle_ctx->ctx);
    IVE_ITC_CRTL_S iveItcCtrl;
    iveItcCtrl.enType = IVE_ITC_NORMALIZE;
    CVI_IVE_ImageTypeConvert(pIveHandle, &dstV_BF16, pstDstV, &iveItcCtrl, 0);
    CVI_IVE_ImageTypeConvert(pIveHandle, &dstH_BF16, pstDstH, &iveItcCtrl, 0);
    CVI_SYS_Free(pIveHandle, &dstV_BF16);
    CVI_SYS_Free(pIveHandle, &dstH_BF16);
  } else if (pstNormGradCtrl->enOutCtrl == IVE_NORM_GRAD_OUT_CTRL_HOR) {
    IVE_IMAGE_S dst_BF16;
    CVI_IVE_CreateImage(pIveHandle, &dst_BF16, IVE_IMAGE_TYPE_BF16C1, pstSrc->u16Width,
                        pstSrc->u16Height);
    CviImg *cpp_dsth = reinterpret_cast<CviImg *>(dst_BF16.tpu_block);
    outputs.emplace_back(*cpp_dsth);
    IveKernel kernel_h =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_Y);
    handle_ctx->t_h.t_filter_bf16.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_filter_bf16.setKernel(kernel_h);
    handle_ctx->t_h.t_filter_bf16.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                      &outputs);
    kernel_h.img.Free(&handle_ctx->ctx);
    IVE_ITC_CRTL_S iveItcCtrl;
    iveItcCtrl.enType = IVE_ITC_NORMALIZE;
    CVI_IVE_ImageTypeConvert(pIveHandle, &dst_BF16, pstDstH, &iveItcCtrl, 0);
    CVI_SYS_Free(pIveHandle, &dst_BF16);
  } else if (pstNormGradCtrl->enOutCtrl == IVE_NORM_GRAD_OUT_CTRL_VER) {
    IVE_IMAGE_S dst_BF16;
    CVI_IVE_CreateImage(pIveHandle, &dst_BF16, IVE_IMAGE_TYPE_BF16C1, pstSrc->u16Width,
                        pstSrc->u16Height);
    CviImg *cpp_dstv = reinterpret_cast<CviImg *>(dst_BF16.tpu_block);
    outputs.emplace_back(*cpp_dstv);
    IveKernel kernel_w =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_X);
    handle_ctx->t_h.t_filter_bf16.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_filter_bf16.setKernel(kernel_w);
    handle_ctx->t_h.t_filter_bf16.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                      &outputs);
    kernel_w.img.Free(&handle_ctx->ctx);
    IVE_ITC_CRTL_S iveItcCtrl;
    iveItcCtrl.enType = IVE_ITC_NORMALIZE;
    CVI_IVE_ImageTypeConvert(pIveHandle, &dst_BF16, pstDstV, &iveItcCtrl, 0);
    CVI_SYS_Free(pIveHandle, &dst_BF16);
  } else if (pstNormGradCtrl->enOutCtrl == IVE_NORM_GRAD_OUT_CTRL_COMBINE) {
    IVE_IMAGE_S dst_BF16;
    CVI_IVE_CreateImage(pIveHandle, &dst_BF16, IVE_IMAGE_TYPE_BF16C1, pstSrc->u16Width,
                        pstSrc->u16Height);
    CviImg *cpp_dsthv = reinterpret_cast<CviImg *>(dst_BF16.tpu_block);
    outputs.emplace_back(*cpp_dsthv);
    IveKernel kernel_w =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_X);
    IveKernel kernel_h =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_Y);
    handle_ctx->t_h.t_sobel.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_sobel.setKernel(kernel_w, kernel_h);
    handle_ctx->t_h.t_sobel.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                &outputs);
    kernel_w.img.Free(&handle_ctx->ctx);
    kernel_h.img.Free(&handle_ctx->ctx);
    IVE_ITC_CRTL_S iveItcCtrl;
    iveItcCtrl.enType = IVE_ITC_NORMALIZE;
    CVI_IVE_ImageTypeConvert(pIveHandle, &dst_BF16, pstDstHV, &iveItcCtrl, 0);
    CVI_SYS_Free(pIveHandle, &dst_BF16);
  } else {
    return CVI_NOT_SUPPORTED;
  }
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_Or(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc1, IVE_SRC_IMAGE_S *pstSrc2,
                   IVE_DST_IMAGE_S *pstDst, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  handle_ctx->t_h.t_or.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
  CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrc1->tpu_block);
  CviImg *cpp_src2 = reinterpret_cast<CviImg *>(pstSrc2->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src1, *cpp_src2};
  std::vector<CviImg> outputs = {*cpp_dst};

  handle_ctx->t_h.t_or.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs, &outputs);
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_SAD(IVE_HANDLE *pIveHandle, IVE_SRC_IMAGE_S *pstSrc1, IVE_SRC_IMAGE_S *pstSrc2,
                    IVE_DST_IMAGE_S *pstSad, IVE_DST_IMAGE_S *pstThr, IVE_SAD_CTRL_S *pstSadCtrl,
                    bool bInstant) {
  if (pstSrc1->u16Width != pstSrc2->u16Width || pstSrc1->u16Height != pstSrc2->u16Height) {
    std::cerr << "Two input size must be the same!" << std::endl;
    return CVI_FAILURE;
  }
  CVI_U32 window_size = 1;
  switch (pstSadCtrl->enMode) {
    case IVE_SAD_MODE_MB_4X4:
      window_size = 4;
      break;
    case IVE_SAD_MODE_MB_8X8:
      window_size = 8;
      break;
    case IVE_SAD_MODE_MB_16X16:
      window_size = 16;
      break;
    default:
      std::cerr << "Unsupported SAD mode " << pstSadCtrl->enMode << std::endl;
      return CVI_FAILURE;
      break;
  }
  if (pstSad->u16Width != pstSrc1->u16Width) {
    std::cerr << "Dst width not match with src! Src: " << pstSrc1->u16Width
              << ", dst: " << pstSad->u16Width << std::endl;
    return CVI_FAILURE;
  }
  if (pstSad->u16Height != pstSrc1->u16Height) {
    std::cerr << "Dst height not match with src! Src: " << pstSrc1->u16Height
              << ", dst: " << pstSad->u16Height << std::endl;
    return CVI_FAILURE;
  }
  int ret = CVI_SUCCESS;
  bool do_threshold = (pstSadCtrl->enOutCtrl == IVE_SAD_OUT_CTRL_16BIT_BOTH ||
                       pstSadCtrl->enOutCtrl == IVE_SAD_OUT_CTRL_8BIT_BOTH ||
                       pstSadCtrl->enOutCtrl == IVE_SAD_OUT_CTRL_THRESH)
                          ? true
                          : false;
  bool is_output_u8 = (pstSadCtrl->enOutCtrl == IVE_SAD_OUT_CTRL_8BIT_SAD ||
                       pstSadCtrl->enOutCtrl == IVE_SAD_OUT_CTRL_8BIT_BOTH ||
                       pstSadCtrl->enOutCtrl == IVE_SAD_OUT_CTRL_THRESH)
                          ? true
                          : false;
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrc1->tpu_block);
  CviImg *cpp_src2 = reinterpret_cast<CviImg *>(pstSrc2->tpu_block);
  CviImg *cpp_dst = nullptr;
  std::vector<CviImg> inputs = {*cpp_src1, *cpp_src2};
  std::vector<CviImg> outputs;
  IVE_IMAGE_S dst_BF16;
  if (!is_output_u8) {
    ret = CVI_IVE_CreateImage(pIveHandle, &dst_BF16, IVE_IMAGE_TYPE_BF16C1, pstSad->u16Width,
                              pstSad->u16Height);
    cpp_dst = reinterpret_cast<CviImg *>(dst_BF16.tpu_block);
  } else {
    cpp_dst = reinterpret_cast<CviImg *>(pstSad->tpu_block);
  }
  if (do_threshold) {
    if (pstSad->u16Width != pstThr->u16Width || pstSad->u16Height != pstThr->u16Height) {
      std::cerr << "Threshold output size must be the same as SAD output!" << std::endl;
      return CVI_FAILURE;
    }
    CviImg *thresh_dst = reinterpret_cast<CviImg *>(pstThr->tpu_block);
    if (pstSadCtrl->enOutCtrl == IVE_SAD_OUT_CTRL_THRESH) {
      outputs.emplace_back(*thresh_dst);
      handle_ctx->t_h.t_sad.outputThresholdOnly(true);
    } else {
      outputs.emplace_back(*cpp_dst);
      outputs.emplace_back(*thresh_dst);
    }
  } else {
    outputs.emplace_back(*cpp_dst);
  }
  handle_ctx->t_h.t_sad.doThreshold(do_threshold);
  handle_ctx->t_h.t_sad.setThreshold(pstSadCtrl->u16Thr, pstSadCtrl->u8MinVal,
                                     pstSadCtrl->u8MaxVal);
  handle_ctx->t_h.t_sad.setWindowSize(window_size, cpp_src1->m_tg.shape.c);
  handle_ctx->t_h.t_sad.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
  handle_ctx->t_h.t_sad.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs, &outputs);
  if (!is_output_u8) {
    IVE_ITC_CRTL_S iveItcCtrl;
    iveItcCtrl.enType = IVE_ITC_SATURATE;
    ret = CVI_IVE_ImageTypeConvert(pIveHandle, &dst_BF16, pstSad, &iveItcCtrl, 0);
    CVI_SYS_Free(pIveHandle, &dst_BF16);
  }
  return ret;
}

CVI_S32 CVI_IVE_Sobel(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc, IVE_DST_IMAGE_S *pstDstH,
                      IVE_DST_IMAGE_S *pstDstV, IVE_SOBEL_CTRL_S *pstSobelCtrl, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  CviImg *cpp_src = reinterpret_cast<CviImg *>(pstSrc->tpu_block);
  CviImg *cpp_dsth = reinterpret_cast<CviImg *>(pstDstH->tpu_block);
  CviImg *cpp_dstv = reinterpret_cast<CviImg *>(pstDstV->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src};
  std::vector<CviImg> outputs;
  if (pstSobelCtrl->enOutCtrl == IVE_SOBEL_OUT_CTRL_BOTH) {
    outputs.emplace_back(*cpp_dstv);
    outputs.emplace_back(*cpp_dsth);
    IveKernel kernel_w =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_X);
    IveKernel kernel_h =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_Y);
    handle_ctx->t_h.t_sobel_gradonly.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_sobel_gradonly.setKernel(kernel_w, kernel_h);
    handle_ctx->t_h.t_sobel_gradonly.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx,
                                                         inputs, &outputs);
    kernel_w.img.Free(&handle_ctx->ctx);
    kernel_h.img.Free(&handle_ctx->ctx);
  } else if (pstSobelCtrl->enOutCtrl == IVE_SOBEL_OUT_CTRL_HOR) {
    outputs.emplace_back(*cpp_dsth);
    IveKernel kernel_h =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_Y);
    handle_ctx->t_h.t_filter_bf16.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_filter_bf16.setKernel(kernel_h);
    handle_ctx->t_h.t_filter_bf16.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                      &outputs);
    kernel_h.img.Free(&handle_ctx->ctx);
  } else if (pstSobelCtrl->enOutCtrl == IVE_SOBEL_OUT_CTRL_VER) {
    outputs.emplace_back(*cpp_dstv);
    IveKernel kernel_w =
        createKernel(&handle_ctx->ctx, cpp_src->m_tg.shape.c, 3, 3, IVE_KERNEL::SOBEL_X);
    handle_ctx->t_h.t_filter_bf16.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    handle_ctx->t_h.t_filter_bf16.setKernel(kernel_w);
    handle_ctx->t_h.t_filter_bf16.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                      &outputs);
    kernel_w.img.Free(&handle_ctx->ctx);
  } else {
    return CVI_NOT_SUPPORTED;
  }
  return CVI_SUCCESS;
}

CVI_S32 CVI_IVE_Sub(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc1, IVE_SRC_IMAGE_S *pstSrc2,
                    IVE_DST_IMAGE_S *pstDst, IVE_SUB_CTRL_S *ctrl, bool bInstant) {
  int ret = CVI_NOT_SUPPORTED;
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  if (ctrl->enMode == IVE_SUB_MODE_NORMAL) {
    ret = CVI_SUCCESS;
    handle_ctx->t_h.t_sub.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrc1->tpu_block);
    CviImg *cpp_src2 = reinterpret_cast<CviImg *>(pstSrc2->tpu_block);
    CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
    std::vector<CviImg> inputs = {*cpp_src1, *cpp_src2};
    std::vector<CviImg> outputs = {*cpp_dst};

    handle_ctx->t_h.t_sub.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                              &outputs);
  } else if (ctrl->enMode == IVE_SUB_MODE_ABS) {
    ret = CVI_SUCCESS;
    handle_ctx->t_h.t_sub_abs.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
    CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrc1->tpu_block);
    CviImg *cpp_src2 = reinterpret_cast<CviImg *>(pstSrc2->tpu_block);
    CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
    std::vector<CviImg> inputs = {*cpp_src1, *cpp_src2};
    std::vector<CviImg> outputs = {*cpp_dst};

    handle_ctx->t_h.t_sub_abs.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                  &outputs);
  }
  return ret;
}

CVI_S32 CVI_IVE_Thresh(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc1, IVE_DST_IMAGE_S *pstDst,
                       IVE_THRESH_CTRL_S *ctrl, bool bInstant) {
  int ret = CVI_NOT_SUPPORTED;
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrc1->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src1};
  std::vector<CviImg> outputs = {*cpp_dst};

  if (ctrl->enMode == IVE_THRESH_MODE_BINARY) {
    ret = CVI_SUCCESS;
    if (ctrl->u8MinVal == 0 && ctrl->u8MaxVal == 255) {
      handle_ctx->t_h.t_thresh.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
      handle_ctx->t_h.t_thresh.setThreshold(ctrl->u8LowThr);
      handle_ctx->t_h.t_thresh.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                   &outputs);
    } else {
      handle_ctx->t_h.t_thresh_hl.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
      handle_ctx->t_h.t_thresh_hl.setThreshold(ctrl->u8LowThr, ctrl->u8MinVal, ctrl->u8MaxVal);
      handle_ctx->t_h.t_thresh_hl.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs,
                                                      &outputs);
    }
  }
  return ret;
}

CVI_S32 CVI_IVE_Xor(IVE_HANDLE pIveHandle, IVE_SRC_IMAGE_S *pstSrc1, IVE_SRC_IMAGE_S *pstSrc2,
                    IVE_DST_IMAGE_S *pstDst, bool bInstant) {
  IVE_HANDLE_CTX *handle_ctx = reinterpret_cast<IVE_HANDLE_CTX *>(pIveHandle);
  handle_ctx->t_h.t_xor.init(&handle_ctx->ctx, handle_ctx->bk_ctx);
  CviImg *cpp_src1 = reinterpret_cast<CviImg *>(pstSrc1->tpu_block);
  CviImg *cpp_src2 = reinterpret_cast<CviImg *>(pstSrc2->tpu_block);
  CviImg *cpp_dst = reinterpret_cast<CviImg *>(pstDst->tpu_block);
  std::vector<CviImg> inputs = {*cpp_src1, *cpp_src2};
  std::vector<CviImg> outputs = {*cpp_dst};

  handle_ctx->t_h.t_xor.runSingleSizeKernel(&handle_ctx->ctx, handle_ctx->bk_ctx, inputs, &outputs);
  return CVI_SUCCESS;
}
