#include "bmkernel/bm_kernel.h"
#include "ive.h"

#include <math.h>
#include <stdio.h>
#include "bmkernel/bm1880v2/1880v2_fp_convert.h"

#define CELL_SIZE 5
#define BIN_NUM 9

int cpu_ref(const int channels, IVE_SRC_IMAGE_S *src, IVE_DST_IMAGE_S *dstH, IVE_DST_IMAGE_S *dstV,
            IVE_DST_IMAGE_S *dstAng);

int main(int argc, char **argv) {
  CVI_SYS_LOGGING(argv[0]);
  // Create instance
  IVE_HANDLE handle = CVI_IVE_CreateHandle();
  printf("BM Kernel init.\n");

  // Fetch image information
  IVE_IMAGE_S src = CVI_IVE_ReadImage(handle, "cat.png", IVE_IMAGE_TYPE_U8C1);
  int nChannels = 1;
  int width = src.u16Width;
  int height = src.u16Height;

  IVE_DST_IMAGE_S dstH, dstV;
  CVI_IVE_CreateImage(handle, &dstV, IVE_IMAGE_TYPE_BF16C1, width, height);
  CVI_IVE_CreateImage(handle, &dstH, IVE_IMAGE_TYPE_BF16C1, width, height);

  IVE_DST_IMAGE_S dstH_u8, dstV_u8;
  CVI_IVE_CreateImage(handle, &dstH_u8, IVE_IMAGE_TYPE_U8C1, width, height);
  CVI_IVE_CreateImage(handle, &dstV_u8, IVE_IMAGE_TYPE_U8C1, width, height);

  IVE_DST_IMAGE_S dstAng;
  CVI_IVE_CreateImage(handle, &dstAng, IVE_IMAGE_TYPE_BF16C1, width, height);

  IVE_DST_IMAGE_S dstAng_u8;
  CVI_IVE_CreateImage(handle, &dstAng_u8, IVE_IMAGE_TYPE_U8C1, width, height);

  IVE_DST_IMAGE_S dstBlk;
  CVI_IVE_CreateImage(handle, &dstBlk, IVE_IMAGE_TYPE_BF16C1, width / CELL_SIZE,
                      height / CELL_SIZE);

  IVE_DST_MEM_INFO_S dstHist;
  CVI_IVE_CreateMemInfo(handle, &dstHist, BIN_NUM * sizeof(int));

  printf("Run TPU HOG.\n");
  IVE_HOG_CTRL_S pstHogCtrl;
  pstHogCtrl.bin_num = BIN_NUM;
  pstHogCtrl.cell_size = CELL_SIZE;
  CVI_IVE_HOG(handle, &src, &dstH, &dstV, NULL, &dstAng, &dstBlk, &dstHist, &pstHogCtrl, 0);

  printf("Normalize result to 0-255.\n");
  IVE_ITC_CRTL_S iveItcCtrl;
  iveItcCtrl.enType = IVE_ITC_NORMALIZE;
  CVI_IVE_ImageTypeConvert(handle, &dstV, &dstV_u8, &iveItcCtrl, 0);
  CVI_IVE_ImageTypeConvert(handle, &dstH, &dstH_u8, &iveItcCtrl, 0);
  CVI_IVE_ImageTypeConvert(handle, &dstAng, &dstAng_u8, &iveItcCtrl, 0);

  int ret = cpu_ref(nChannels, &src, &dstH, &dstV, &dstAng);

  // write result to disk
  printf("Save to image.\n");
  CVI_IVE_WriteImage("test_sobelV_c.png", &dstV_u8);
  CVI_IVE_WriteImage("test_sobelH_c.png", &dstH_u8);
  CVI_IVE_WriteImage("test_ang_c.png", &dstAng_u8);

  // Free memory, instance
  CVI_SYS_FreeI(handle, &src);
  CVI_SYS_FreeI(handle, &dstH);
  CVI_SYS_FreeI(handle, &dstH_u8);
  CVI_SYS_FreeI(handle, &dstV);
  CVI_SYS_FreeI(handle, &dstV_u8);
  CVI_SYS_FreeI(handle, &dstAng);
  CVI_SYS_FreeI(handle, &dstAng_u8);
  CVI_SYS_FreeI(handle, &dstBlk);
  CVI_SYS_FreeM(handle, &dstHist);
  CVI_IVE_DestroyHandle(handle);

  return ret;
}

int cpu_ref(const int channels, IVE_SRC_IMAGE_S *src, IVE_DST_IMAGE_S *dstH, IVE_DST_IMAGE_S *dstV,
            IVE_DST_IMAGE_S *dstAng) {
  int ret = CVI_SUCCESS;
  u16 *dstH_ptr = (u16 *)dstH->pu8VirAddr[0];
  u16 *dstV_ptr = (u16 *)dstV->pu8VirAddr[0];
  u16 *dstAng_ptr = (u16 *)dstAng->pu8VirAddr[0];
  float mul_val = 180.f / M_PI;
  float ang_epsilon = 0.02;  // atan2 0.02, mul => 1.3

  printf("Check Ang:\n");
  for (size_t i = 0; i < channels * src->u16Width * src->u16Height; i++) {
    float dstH_f = convert_bf16_fp32(dstH_ptr[i]);
    float dstV_f = convert_bf16_fp32(dstV_ptr[i]);
    float dstAng_f = convert_bf16_fp32(dstAng_ptr[i]);
    float atan2_res = (float)atan2(dstV_f, dstH_f) * mul_val;
    if (atan2_res < 0) {
      atan2_res += 360.f;
    }
    float error = fabs(atan2_res - dstAng_f) / atan2_res;
    if (error > ang_epsilon) {
      printf("[%lu] atan2( %f, %f) = TPU %f, CPU %f (%f). eplison = %f\n", i, dstV_f, dstH_f,
             dstAng_f, atan2_res, atan2_res - 360.f, error);
      ret = CVI_FAILURE;
    }
  }
  return ret;
}