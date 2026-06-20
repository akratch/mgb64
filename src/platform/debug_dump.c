/**
 * debug_dump.c — Guard state frame dump tool.
 *
 * Press ` (backtick) during gameplay to write a comprehensive snapshot
 * of all guard/chr state to /tmp/ge007_dump_NNNN.txt.
 * An on-screen overlay confirms the dump was captured.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../bondtypes.h"
#include "../bondconstants.h"

extern union ModelRwData *modelGetNodeRwData(Model *model, ModelNode *node);

/* Externals from the game */
extern int g_frame_count_diag;
extern int g_MaxNumRooms;
extern int g_BgCurrentRoom;
extern f32 level_scale, inv_level_scale;

/* Camera */
extern Mtxf *camGetWorldToScreenMtxf(void);

/* Fog state */
struct CurrentEnvironmentRecord;
extern struct CurrentEnvironmentRecord *fogGetCurrentEnvironmentp(void);

/* Player access helpers — struct player is incomplete here, use bondview.c helpers */
extern coord3d *bondviewGetCurrentPlayersPosition(void);
extern s32 bondviewGetCurrentPlayersRoom(void);

/* Dump state */
static int s_dumpRequested = 0;
static int s_dumpCount = 0;
static char s_dumpPath[256] = "";
static int s_overlayTimer = 0;

void debugDumpRequest(void) {
    s_dumpRequested = 1;
}

int debugDumpOverlayActive(void) {
    return s_overlayTimer > 0;
}

const char *debugDumpOverlayText(void) {
    return s_dumpPath;
}

/* debugDumpOverlayTick is defined at the bottom of this file */

static const char *actionName(int act) {
    static const char *names[] = {
        "INIT","STAND","KNEEL","ANIM","DIE","DEAD","ARGH","PREARGH",
        "ATTACK","ATTACKWALK","ATTACKROLL","SIDESTEP","JUMPOUT","RUNPOS",
        "PATROL","GOPOS","SURRENDER","STARTLARM","SURPRISED","TEST"
    };
    if (act >= 0 && act < 20) return names[act];
    return "???";
}

static const char *propTypeName(int t) {
    switch (t) {
        case 1: return "DOOR";
        case 2: return "OBJ";
        case 3: return "CHR";
        case 4: return "WEAPON";
        case 5: return "VIEWER";
        default: return "???";
    }
}

void debugDumpExecute(void) {
    if (!s_dumpRequested) return;
    s_dumpRequested = 0;

    snprintf(s_dumpPath, sizeof(s_dumpPath), "/tmp/ge007_dump_%04d.txt", s_dumpCount++);
    FILE *fp = fopen(s_dumpPath, "w");
    if (!fp) {
        snprintf(s_dumpPath, sizeof(s_dumpPath), "DUMP FAILED (fopen)");
        s_overlayTimer = 180;
        return;
    }

    fprintf(fp, "=== GE007 Debug Dump — frame %d ===\n\n", g_frame_count_diag);

    /* --- Camera --- */
    fprintf(fp, "== CAMERA ==\n");
    {
        coord3d *ppos = bondviewGetCurrentPlayersPosition();
        s32 proom = bondviewGetCurrentPlayersRoom();
        if (ppos) {
            fprintf(fp, "  player_pos=(%.1f, %.1f, %.1f)\n", ppos->x, ppos->y, ppos->z);
        }
        fprintf(fp, "  player_room=%d g_BgCurrentRoom=%d\n", proom, g_BgCurrentRoom);
    }
    fprintf(fp, "  level_scale=%.6f inv_level_scale=%.6f\n", level_scale, inv_level_scale);

    Mtxf *cam = camGetWorldToScreenMtxf();
    if (cam) {
        fprintf(fp, "  cam_w2s:\n");
        for (int r = 0; r < 4; r++) {
            fprintf(fp, "    [%.4f, %.4f, %.4f, %.4f]\n",
                    cam->m[r][0], cam->m[r][1], cam->m[r][2], cam->m[r][3]);
        }
    }

    /* --- Fog --- */
    fprintf(fp, "\n== FOG ==\n");
    {
        /* CurrentEnvironmentRecord layout: s32 DiffFromFar, s32 FarIntensity,
         * u8 Red, u8 Green, u8 Blue, u8 Clouds, ... */
        u8 *ep = (u8 *)fogGetCurrentEnvironmentp();
        if (ep) {
            s32 diffFromFar, farIntensity;
            memcpy(&diffFromFar, ep + 0, 4);
            memcpy(&farIntensity, ep + 4, 4);
            fprintf(fp, "  DiffFromFar=%d FarIntensity=%d\n", diffFromFar, farIntensity);
            fprintf(fp, "  RGB=(%d,%d,%d) Clouds=%d\n", ep[8], ep[9], ep[10], ep[11]);
        }
    }

    /* --- All CHR props --- */
    fprintf(fp, "\n== GUARDS/CHRS ==\n");
    {
        /* Iterate chr records via the setup propDefs.
         * On NATIVE_PORT, chrTickBeams iterates props; we can access
         * chr data through the prop->chr pointer for chr-type props. */
        extern s32 g_NumChrSlots;
        extern ChrRecord *g_ChrSlots;

        if (g_ChrSlots) {
            for (int i = 0; i < g_NumChrSlots && i < 50; i++) {
                ChrRecord *chr = &g_ChrSlots[i];
                if (chr->prop == NULL) continue;
                PropRecord *prop = chr->prop;
                Model *model = chr->model;

                fprintf(fp, "\n  --- chr[%d] chrnum=%d ---\n", i, chr->chrnum);
                fprintf(fp, "  action=%d(%s) hidden=0x%04X chrflags=0x%X sleep=%d\n",
                        chr->actiontype, actionName(chr->actiontype),
                        chr->hidden, chr->chrflags, chr->sleep);
                fprintf(fp, "  pos=(%.1f, %.1f, %.1f) ground=%.1f manground=%.1f\n",
                        prop->pos.x, prop->pos.y, prop->pos.z,
                        chr->ground, chr->manground);
                fprintf(fp, "  prop_type=%d(%s) flags=0x%X rooms=[%d,%d,%d,%d] stan=%p\n",
                        prop->type, propTypeName(prop->type),
                        prop->flags,
                        prop->rooms[0], prop->rooms[1], prop->rooms[2], prop->rooms[3],
                        (void*)prop->stan);

                if (model) {
                    fprintf(fp, "  model=%p anim=%p animlooping=%d speed=%.2f playspeed=%.2f\n",
                            (void*)model, (void*)model->anim,
                            model->animlooping, model->speed, model->playspeed);
                    fprintf(fp, "  scale=%.4f render_pos=%p field_20=%p\n",
                            model->scale, (void*)model->render_pos, (void*)chr->field_20);

                    /* Dump ALL bone matrices */
                    if (model->render_pos) {
                        s32 nmat = (model->obj && model->obj->Skeleton) ? model->obj->Skeleton->numjoints : 0;
                        if (nmat > 20) nmat = 20; /* safety cap */
                        fprintf(fp, "  render_pos=%p numjoints=%d\n", (void*)model->render_pos, nmat);
                        for (int bi = 0; bi < nmat; bi++) {
                            float (*rp)[4] = (float (*)[4])&model->render_pos[bi];
                            float diag_sum = rp[0][0]*rp[0][0] + rp[1][1]*rp[1][1] + rp[2][2]*rp[2][2];
                            int valid = (diag_sum > 0.001f);
                            fprintf(fp, "  bone[%d] trans=(%8.1f,%8.1f,%8.1f) diag=(%6.3f,%6.3f,%6.3f) %s\n",
                                    bi, rp[3][0], rp[3][1], rp[3][2],
                                    rp[0][0], rp[1][1], rp[2][2],
                                    valid ? "OK" : "*** ZERO ***");
                        }
                    } else {
                        fprintf(fp, "  *** render_pos = NULL ***\n");
                    }

                    /* Check rwdata unk00/unk01 (the struct mismatch field) */
                    if (model->obj && model->obj->RootNode) {
                        u8 *rwbytes = (u8 *)modelGetNodeRwData(model, model->obj->RootNode);
                        if (rwbytes) {
                            fprintf(fp, "  rwdata byte0=%d byte1=%d byte2=%d (unk00/unk01/unk02)\n",
                                    rwbytes[0], rwbytes[1], rwbytes[2]);
                        }
                    }
                } else {
                    fprintf(fp, "  model=NULL\n");
                }

                /* Fog culling info */
                {
                    coord3d *ppos = bondviewGetCurrentPlayersPosition();
                    if (ppos) {
                        f32 dx = prop->pos.x - ppos->x;
                        f32 dz = prop->pos.z - ppos->z;
                        f32 dist = sqrtf(dx*dx + dz*dz);
                        fprintf(fp, "  dist_to_player=%.1f\n", dist);
                    }
                }
            }
        } else {
            fprintf(fp, "  g_ChrSlots=NULL (no chr data)\n");
        }
    }

    fprintf(fp, "\n=== END DUMP ===\n");
    fclose(fp);

    fprintf(stderr, "[DUMP] Wrote %s at frame %d\n", s_dumpPath, g_frame_count_diag);
    s_overlayTimer = 180; /* Show overlay for ~3 seconds at 60fps */

    /* Update window title to confirm dump */
    {
        extern void platformSetWindowTitle(const char *title);
        char title[300];
        snprintf(title, sizeof(title), "GE007 - DUMP SAVED: %s (frame %d)", s_dumpPath, g_frame_count_diag);
        platformSetWindowTitle(title);
    }
}

/* Called from lvl.c each frame to restore title after overlay timeout */
void debugDumpOverlayTick(void) {
    if (s_overlayTimer > 0) {
        s_overlayTimer--;
        if (s_overlayTimer == 0) {
            extern void platformSetWindowTitle(const char *title);
            platformSetWindowTitle("MGB64");
        }
    }
}
