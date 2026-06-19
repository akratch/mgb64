#include <ultra64.h>
#include <fr.h>
#include "dyn.h"
#include "title.h"
#include "blood_decrypt.h"
#include "player.h"
#include <PR/os.h>
#ifdef NATIVE_PORT
#include <string.h>
#endif

#define BLOOD_IMAGE_BYTES 0x1E00

#ifdef NATIVE_PORT
/*
 * The original game stores the evolving gunbarrel blood mask in dyn memory.
 * On the native renderer, that transient arena can be recycled aggressively
 * enough that long-running frontend captures occasionally hand Metal a stale
 * texture address. Keep the authored image generation path intact, but copy
 * the current 96x80 I4 payload into a stable upload buffer before SETTIMG.
 */
static u8 g_BloodOverlayUploadBuffer[BLOOD_IMAGE_BYTES];

static u8 *bloodGetOverlayTexturePtr(void)
{
   u8 *src = g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx];

   if (src == NULL) {
      return NULL;
   }

   memcpy(g_BloodOverlayUploadBuffer, src, BLOOD_IMAGE_BYTES);
   return g_BloodOverlayUploadBuffer;
}
#else
static u8 *bloodGetOverlayTexturePtr(void)
{
   return g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx];
}
#endif

u8 die_blood_image_1[2524] = {0}; /* MGB64: ROM-derived texture data removed; supply from your own ROM */

u8 die_blood_image_end = 0;

static u8 *bloodAnimationEndPtr(void)
{
   return die_blood_image_1 + sizeof(die_blood_image_1);
}

Gfx *insert_imageDL(Gfx *gdl) {
   gDPSetCycleType(gdl++, G_CYC_FILL);
   gDPSetColorImage(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, viGetX(), osVirtualToPhysical(viGetFrameBuf2()));
   gDPSetFillColor(gdl++, ((GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1)));
   gDPFillRectangle(gdl++, 0, 0, (viGetX() - 1), (viGetY() - 1));

   return gdl;
}

Gfx *sub_GAME_7F01C1A4(Gfx *gdl) {
   gSPMatrix(gdl++, osVirtualToPhysical(matrixBufferGunbarrel0), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
   gSPMatrix(gdl++, osVirtualToPhysical(&matrixBufferRareLogo2[D_8002A7D0]), (G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
   gDPPipeSync(gdl++);
   gDPSetCycleType(gdl++, G_CYC_1CYCLE);
   gDPSetRenderMode(gdl++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
   gSPSetGeometryMode(gdl++, (G_SHADE | G_SHADING_SMOOTH));

   return gdl;
}

s32 die_blood_image_routine(s32 arg0) {
   u8 sp37;
   u8* temp_v0_2;
   u8 *end = bloodAnimationEndPtr();

   if (arg0 == 0) {
      g_CurrentPlayer->bloodImgCur = die_blood_image_1;
   } else if (arg0 == 1) {
      if (g_CurrentPlayer->bloodImgNxt < end) {
         g_CurrentPlayer->bloodImgCur = g_CurrentPlayer->bloodImgNxt;
      }
   }

   g_CurrentPlayer->bloodImgIdx = (1 - g_CurrentPlayer->bloodImgIdx);
   g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx] = dynAllocate(BLOOD_IMAGE_BYTES);
   temp_v0_2 = dynAllocate(BLOOD_IMAGE_BYTES);
   g_CurrentPlayer->bloodImgNxt = decrypt_bleeding_animation_data(g_CurrentPlayer->bloodImgCur, 0x50, 0x60, temp_v0_2, &sp37);
   sub_GAME_7F01D16C(temp_v0_2, 0x50, 0x60, g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx]);
   sub_GAME_7F01D02C(g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx], 0x50, g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx]);
   sub_GAME_7F01CEEC(g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx], 0x50, g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx]);
   sub_GAME_7F01CC94(g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx], BLOOD_IMAGE_BYTES, g_CurrentPlayer->bloodImgBufPtrArray[g_CurrentPlayer->bloodImgIdx]);

   return (g_CurrentPlayer->bloodImgNxt >= end);
}

Gfx *gunbarrelBloodOverlayDL(Gfx *gdl) {
   u8 *blood_texture = bloodGetOverlayTexturePtr();

   if (blood_texture == NULL) {
      return gdl;
   }

   gDPSetTextureLUT(gdl++, G_TT_NONE);
   gDPSetTextureFilter(gdl++, G_TF_BILERP); 

   gdl = sub_GAME_7F01C1A4(gdl);

   gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
   gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
   gDPSetCombineMode(gdl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM)
   gDPSetColorDither(gdl++, G_CD_MAGICSQ);
   gDPSetPrimColor(gdl++, 0, 0, 0x96, 0x00, 0x00, 0xB4);
   gDPSetTexturePersp(gdl++, G_TP_NONE);
   gDPLoadTextureBlock_4b(gdl++, OS_K0_TO_PHYSICAL(blood_texture), G_IM_FMT_I, 96, 80, 0, (G_TX_NOMIRROR | G_TX_CLAMP), (G_TX_NOMIRROR | G_TX_CLAMP), G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
   gSPTextureRectangle(gdl++, 0, 0, ((viGetX() * 4) - 1), ((viGetY() * 4) - 1), G_TX_RENDERTILE, 0, 0, 0x18000 / viGetX(), 0x14000 / viGetY());
   
   return gdl;
}

Gfx *gameplayBloodOverlayDL(Gfx *gdl) {
   u8 *blood_texture = bloodGetOverlayTexturePtr();

   if (blood_texture == NULL) {
      return gdl;
   }

   gDPSetTextureLUT(gdl++, G_TT_NONE);
   gDPSetTextureFilter(gdl++, G_TF_BILERP);
   gDPSetCycleType(gdl++, G_CYC_1CYCLE);
   gSPSetGeometryMode(gdl++, (G_SHADE | G_SHADING_SMOOTH));
   gSPTexture(gdl++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
   gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
   gDPSetCombineMode(gdl++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
   gDPSetColorDither(gdl++, G_CD_MAGICSQ);
   gDPSetPrimColor(gdl++, 0, 0, 0x96, 0x00, 0x00, 0xB4);
   gDPSetTexturePersp(gdl++, G_TP_NONE);
   gDPLoadTextureBlock_4b(gdl++, OS_K0_TO_PHYSICAL(blood_texture), G_IM_FMT_I, 96, 80, 0, (G_TX_NOMIRROR | G_TX_CLAMP), (G_TX_NOMIRROR | G_TX_CLAMP), G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
   gSPTextureRectangle(gdl++, (viGetViewLeft() * 4), (viGetViewTop() * 4), (((viGetViewLeft() + viGetViewWidth()) * 4) - 1), (((viGetViewTop() + viGetViewHeight()) * 4) - 1), G_TX_RENDERTILE, 0, 0, (0x18000 / viGetViewWidth()), (0x14000 / viGetViewHeight()));
   gDPPipeSync(gdl++);
   gDPSetColorDither(gdl++, G_CD_BAYER);
   gDPSetTexturePersp(gdl++, G_TP_PERSP);

   return gdl;
}

Gfx *sub_GAME_7F01CA18(Gfx *gdl) {
	gdl = sub_GAME_7F01C1A4(gdl);

	gDPSetRenderMode(gdl++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
	gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
	gDPSetPrimColor(gdl++, 0, 0, 150, 0, 0, 180);
	gDPSetColorDither(gdl++, G_CD_MAGICSQ);
	gDPFillRectangle(gdl++, 0, 0, viGetX(), viGetY());

   return gdl;
}
