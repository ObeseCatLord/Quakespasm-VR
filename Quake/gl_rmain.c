/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_main.c

#include "quakedef.h"
#include "debug_log.h"
#include "vr.h"

extern cvar_t r_alias_batching;

vec3_t modelorg, r_entorigin;
entity_t *currententity;

int r_visframecount; // bumped when going to a new PVS
int r_framecount;    // used for dlight push checking

mplane_t frustum[4];

// johnfitz -- rendering statistics
int rs_brushpolys, rs_aliaspolys, rs_skypolys;
int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;

//
// view origin
//
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

static qboolean r_vr_stereo_frame = false;
static qboolean r_vr_sort_origin_valid = false;
static int r_vr_eye_index = 0;
static int r_vr_eye_count = 1;
static vec3_t r_vr_sort_origin;

float r_fovx, r_fovy; // johnfitz -- rendering fov may be different becuase of
                      // r_waterwarp and r_stereo

//
// screen size info
//
refdef_t r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;

int d_lightstylevalue[256]; // 8.8 fraction of base light value

cvar_t r_norefresh = {"r_norefresh", "0", CVAR_NONE};
cvar_t r_drawentities = {"r_drawentities", "1", CVAR_NONE};
cvar_t r_drawviewmodel = {"r_drawviewmodel", "1", CVAR_NONE};
cvar_t r_speeds = {"r_speeds", "0", CVAR_NONE};
cvar_t r_perfdebug = {"r_perfdebug", "0", CVAR_NONE};
cvar_t r_perfdebug_min_ms = {"r_perfdebug_min_ms", "8", CVAR_NONE};
cvar_t r_pos = {"r_pos", "0", CVAR_NONE};
cvar_t r_fullbright = {"r_fullbright", "0", CVAR_NONE};
cvar_t r_lightmap = {"r_lightmap", "0", CVAR_NONE};
cvar_t r_shadows = {"r_shadows", "0", CVAR_ARCHIVE};
cvar_t r_wateralpha = {"r_wateralpha", "1", CVAR_ARCHIVE};
cvar_t r_useportalculling = {"r_useportalculling", "0", CVAR_ARCHIVE};
cvar_t r_litwater = {"r_litwater", "1", CVAR_NONE};
cvar_t r_dynamic = {"r_dynamic", "1", CVAR_ARCHIVE};
cvar_t r_novis = {"r_novis", "0", CVAR_ARCHIVE};

cvar_t gl_finish = {"gl_finish", "0", CVAR_NONE};
cvar_t gl_clear = {"gl_clear", "1", CVAR_NONE};
cvar_t gl_cull = {"gl_cull", "1", CVAR_NONE};
cvar_t gl_smoothmodels = {"gl_smoothmodels", "1", CVAR_NONE};
cvar_t gl_affinemodels = {"gl_affinemodels", "0", CVAR_NONE};
cvar_t gl_polyblend = {"gl_polyblend", "1", CVAR_NONE};
cvar_t gl_flashblend = {"gl_flashblend", "0", CVAR_ARCHIVE};
cvar_t gl_playermip = {"gl_playermip", "0", CVAR_NONE};
cvar_t gl_nocolors = {"gl_nocolors", "0", CVAR_NONE};
cvar_t cl_coop_nametags = {"cl_coop_nametags", "1", CVAR_ARCHIVE};

// johnfitz -- new cvars
cvar_t r_stereo = {"r_stereo", "0", CVAR_NONE};
cvar_t r_stereodepth = {"r_stereodepth", "128", CVAR_NONE};
cvar_t r_clearcolor = {"r_clearcolor", "2", CVAR_ARCHIVE};
cvar_t r_drawflat = {"r_drawflat", "0", CVAR_NONE};
cvar_t r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t r_lerplightstyles = {"r_lerplightstyles", "1", CVAR_ARCHIVE}; // 0=off; 1=skip abrupt transitions; 2=always lerp
cvar_t gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE};
cvar_t gl_farclip = {"gl_farclip", "65536", CVAR_ARCHIVE};
cvar_t gl_overbright = {"gl_overbright", "1", CVAR_ARCHIVE};
cvar_t gl_overbright_models = {"gl_overbright_models", "1", CVAR_ARCHIVE};
cvar_t r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t r_lerpmodels = {"r_lerpmodels", "1", CVAR_NONE};
cvar_t r_lerpmove = {"r_lerpmove", "1", CVAR_NONE};
cvar_t r_nolerp_list = {
    "r_nolerp_list",
    "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/"
    "brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/"
    "v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl",
    CVAR_NONE};
cvar_t r_noshadow_list = {
    "r_noshadow_list",
    "progs/flame2.mdl,progs/flame.mdl,progs/bolt1.mdl,progs/bolt2.mdl,progs/"
    "bolt3.mdl,progs/laser.mdl",
    CVAR_NONE};

extern cvar_t r_vfog;
// johnfitz

cvar_t gl_zfix = {"gl_zfix", "0", CVAR_NONE}; // QuakeSpasm z-fighting fix

cvar_t r_alphasort = {"r_alphasort", "1", CVAR_ARCHIVE};
cvar_t r_lavaalpha = {"r_lavaalpha", "0", CVAR_NONE};
cvar_t r_telealpha = {"r_telealpha", "0", CVAR_NONE};
cvar_t r_slimealpha = {"r_slimealpha", "0", CVAR_NONE};
cvar_t r_part_density = {"r_part_density", "1", CVAR_ARCHIVE};

int r_perf_pvs_leaf;
int r_perf_pvs_fat;
int r_perf_pvs_novis;

void R_BeginVRFrame(void) {
  r_vr_stereo_frame = true;
  r_vr_sort_origin_valid = false;
  r_vr_eye_index = 0;
  r_vr_eye_count = 2;
}

void R_SetVREye(int eye_index, int eye_count) {
  r_vr_eye_index = eye_index;
  r_vr_eye_count = eye_count > 0 ? eye_count : 1;
}

void R_EndVRFrame(void) {
  r_vr_stereo_frame = false;
  r_vr_sort_origin_valid = false;
  r_vr_eye_index = 0;
  r_vr_eye_count = 1;
}

qboolean R_IsVRStereoFrame(void) { return r_vr_stereo_frame; }

qboolean R_IsVRFirstEye(void) {
  return !r_vr_stereo_frame || r_vr_eye_index == 0;
}

qboolean R_IsVRLastEye(void) {
  return !r_vr_stereo_frame || r_vr_eye_index >= r_vr_eye_count - 1;
}

const vec_t *R_VRStereoSortOrigin(void) {
  if (r_vr_sort_origin_valid)
    return r_vr_sort_origin;
  return r_refdef.vieworg;
}
int r_perf_leaves_scanned;
int r_perf_leaves_visible;
int r_perf_leaves_culled;
int r_perf_marksurfaces_scanned;
int r_perf_surfaces_unique;
int r_perf_surfaces_culled;
int r_perf_surfaces_chained;
int r_perf_efrag_leaves;
int r_perf_alias_draws;
int r_perf_alias_culled;
int r_perf_alias_glsl_draws;
int r_perf_alias_batch_flushes;

static int r_perf_setup_calls;
static int r_perf_scene_calls;
static int r_perf_entities_opaque;
static int r_perf_entities_alpha;
static int r_perf_entities_alias;
static int r_perf_entities_brush;
static int r_perf_entities_sprite;
static double r_perf_skyroom_ms;
static double r_perf_setup_ms;
static double r_perf_mark_ms;
static double r_perf_warp_ms;
static double r_perf_scene_ms;
static double r_perf_sky_ms;
static double r_perf_world_ms;
static double r_perf_shadows_ms;
static double r_perf_entities_opaque_ms;
static double r_perf_water_ms;
static double r_perf_entities_alpha_ms;
static double r_perf_dlights_ms;
static double r_perf_particles_ms;
static double r_perf_outlines_ms;
static double r_perf_viewmodel_ms;
static double r_perf_debugdraw_ms;
static double r_perf_scale_ms;

static qboolean R_PerfActive(void) { return r_perfdebug.value != 0; }

static double R_PerfStart(void) {
  return R_PerfActive() ? Sys_DoubleTime() : 0.0;
}

static void R_PerfAdd(double *accum, double start) {
  if (R_PerfActive())
    *accum += (Sys_DoubleTime() - start) * 1000.0;
}

static void R_PerfResetFrame(void) {
  r_perf_pvs_leaf = r_perf_pvs_fat = r_perf_pvs_novis = 0;
  r_perf_leaves_scanned = r_perf_leaves_visible = r_perf_leaves_culled = 0;
  r_perf_marksurfaces_scanned = r_perf_surfaces_unique = 0;
  r_perf_surfaces_culled = r_perf_surfaces_chained = r_perf_efrag_leaves = 0;
  r_perf_alias_draws = r_perf_alias_culled = r_perf_alias_glsl_draws = 0;
  r_perf_alias_batch_flushes = 0;
  r_perf_setup_calls = r_perf_scene_calls = 0;
  r_perf_entities_opaque = r_perf_entities_alpha = 0;
  r_perf_entities_alias = r_perf_entities_brush = r_perf_entities_sprite = 0;
  r_perf_skyroom_ms = r_perf_setup_ms = r_perf_mark_ms = r_perf_warp_ms = 0.0;
  r_perf_scene_ms = r_perf_sky_ms = r_perf_world_ms = r_perf_shadows_ms = 0.0;
  r_perf_entities_opaque_ms = r_perf_water_ms = r_perf_entities_alpha_ms = 0.0;
  r_perf_dlights_ms = r_perf_particles_ms = r_perf_outlines_ms = 0.0;
  r_perf_viewmodel_ms = r_perf_debugdraw_ms = r_perf_scale_ms = 0.0;
}

static void R_PerfCountEntity(entity_t *ent, qboolean alphapass) {
  if (!R_PerfActive() || !ent || !ent->model)
    return;

  if (alphapass)
    r_perf_entities_alpha++;
  else
    r_perf_entities_opaque++;

  switch (ent->model->type) {
  case mod_alias:
    r_perf_entities_alias++;
    break;
  case mod_brush:
    r_perf_entities_brush++;
    break;
  case mod_sprite:
    r_perf_entities_sprite++;
    break;
  }
}

static void R_PerfLogFrame(double total_ms) {
  if (!R_PerfActive())
    return;

  if (r_perfdebug.value < 2 &&
      total_ms < q_max(0.0f, r_perfdebug_min_ms.value))
    return;

  DebugLog("r_perfdebug: map=%s vr=%d total=%.3f skyroom=%.3f setup=%.3f "
           "mark=%.3f warp=%.3f scene=%.3f scale=%.3f calls(setup=%d "
           "scene=%d) pvs(leaf=%d fat=%d novis=%d) leaves(scan=%d vis=%d "
           "cull=%d) marks=%d surf(unique=%d cull=%d chain=%d) efragleaf=%d "
           "ents(opaque=%d alpha=%d alias=%d brush=%d sprite=%d) "
           "draw(world=%.3f water=%.3f entopq=%.3f entalpha=%.3f aliasdraw=%d "
           "aliascull=%d aliasglsl=%d aliasflush=%d sky=%.3f shadows=%.3f "
           "dlights=%.3f particles=%.3f outlines=%.3f viewmodel=%.3f "
           "debugdraw=%.3f)\n",
           cl.worldmodel ? cl.worldmodel->name : "<none>",
           (int)vr_enabled.value, total_ms, r_perf_skyroom_ms, r_perf_setup_ms,
           r_perf_mark_ms, r_perf_warp_ms, r_perf_scene_ms, r_perf_scale_ms,
           r_perf_setup_calls, r_perf_scene_calls, r_perf_pvs_leaf,
           r_perf_pvs_fat, r_perf_pvs_novis, r_perf_leaves_scanned,
           r_perf_leaves_visible, r_perf_leaves_culled,
           r_perf_marksurfaces_scanned, r_perf_surfaces_unique,
           r_perf_surfaces_culled, r_perf_surfaces_chained,
           r_perf_efrag_leaves, r_perf_entities_opaque, r_perf_entities_alpha,
           r_perf_entities_alias, r_perf_entities_brush, r_perf_entities_sprite,
           r_perf_world_ms, r_perf_water_ms, r_perf_entities_opaque_ms,
           r_perf_entities_alpha_ms, r_perf_alias_draws, r_perf_alias_culled,
           r_perf_alias_glsl_draws, r_perf_alias_batch_flushes, r_perf_sky_ms,
           r_perf_shadows_ms, r_perf_dlights_ms, r_perf_particles_ms,
           r_perf_outlines_ms, r_perf_viewmodel_ms, r_perf_debugdraw_ms);
}

float map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha, map_fallbackalpha;

qboolean r_drawflat_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe,
    r_drawworld_cheatsafe; // johnfitz

cvar_t r_scale = {"r_scale", "1", CVAR_ARCHIVE};

//==============================================================================
//
// GLSL GAMMA CORRECTION
//
//==============================================================================

static GLuint r_gamma_texture;
static GLuint r_gamma_program;
static int r_gamma_texture_width, r_gamma_texture_height;

// uniforms used in gamma shader
static GLint gammaLoc;
static GLint contrastLoc;
static GLint textureLoc;

/*
=============
GLSLGamma_DeleteTexture
=============
*/
void GLSLGamma_DeleteTexture(void) {
  glDeleteTextures(1, &r_gamma_texture);
  r_gamma_texture = 0;
  r_gamma_program = 0; // deleted in R_DeleteShaders
}

/*
=============
GLSLGamma_CreateShaders
=============
*/
static void GLSLGamma_CreateShaders(void) {
  const GLchar *vertSource = "#version 110\n"
                             "\n"
                             "void main(void) {\n"
                             "	gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);\n"
                             "	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
                             "}\n";

  const GLchar *fragSource =
      "#version 110\n"
      "\n"
      "uniform sampler2D GammaTexture;\n"
      "uniform float GammaValue;\n"
      "uniform float ContrastValue;\n"
      "\n"
      "void main(void) {\n"
      "	  vec4 frag = texture2D(GammaTexture, gl_TexCoord[0].xy);\n"
      "	  frag.rgb = frag.rgb * ContrastValue;\n"
      "	  gl_FragColor = vec4(pow(frag.rgb, vec3(GammaValue)), 1.0);\n"
      "}\n";

  if (!gl_glsl_gamma_able)
    return;

  r_gamma_program = GL_CreateProgram(vertSource, fragSource, 0, NULL);

  // get uniform locations
  gammaLoc = GL_GetUniformLocation(&r_gamma_program, "GammaValue");
  contrastLoc = GL_GetUniformLocation(&r_gamma_program, "ContrastValue");
  textureLoc = GL_GetUniformLocation(&r_gamma_program, "GammaTexture");
}

/*
=============
GLSLGamma_GammaCorrect
=============
*/
void GLSLGamma_GammaCorrect(void) {
  float smax, tmax;

  if (!gl_glsl_gamma_able)
    return;

  if (vid_gamma.value == 1 && vid_contrast.value == 1)
    return;

  // create render-to-texture texture if needed
  if (!r_gamma_texture ||
      (r_gamma_texture_width < glwidth || r_gamma_texture_height < glheight)) {
    if (r_gamma_texture) {
      glDeleteTextures(1, &r_gamma_texture);
    }

    glGenTextures(1, &r_gamma_texture);
    glBindTexture(GL_TEXTURE_2D, r_gamma_texture);

    r_gamma_texture_width = glwidth;
    r_gamma_texture_height = glheight;

    if (!gl_texture_NPOT) {
      r_gamma_texture_width = TexMgr_Pad(r_gamma_texture_width);
      r_gamma_texture_height = TexMgr_Pad(r_gamma_texture_height);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, r_gamma_texture_width,
                 r_gamma_texture_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }

  // create shader if needed
  if (!r_gamma_program) {
    GLSLGamma_CreateShaders();
    if (!r_gamma_program) {
      Sys_Error("GLSLGamma_CreateShaders failed");
    }
  }

  // copy the framebuffer to the texture
  GL_DisableMultitexture();
  glBindTexture(GL_TEXTURE_2D, r_gamma_texture);

  if (vr_enabled.value)
    VR_HandleGammaCorrect();

  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, glx, gly, glwidth, glheight);

  // draw the texture back to the framebuffer with a fragment shader
  GL_UseProgram(r_gamma_program);
  GL_Uniform1fFunc(gammaLoc, vid_gamma.value);
  GL_Uniform1fFunc(contrastLoc, q_min(2.0f, q_max(1.0f, vid_contrast.value)));
  GL_Uniform1iFunc(textureLoc, 0); // use texture unit 0

  glDisable(GL_ALPHA_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  glViewport(glx, gly, glwidth, glheight);

  smax = glwidth / (float)r_gamma_texture_width;
  tmax = glheight / (float)r_gamma_texture_height;

  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2f(-1, -1);
  glTexCoord2f(smax, 0);
  glVertex2f(1, -1);
  glTexCoord2f(smax, tmax);
  glVertex2f(1, 1);
  glTexCoord2f(0, tmax);
  glVertex2f(-1, 1);
  glEnd();

  glEnable(GL_CULL_FACE);

  GL_UseProgram(0);

  // clear cached binding
  GL_ClearBindings();
}

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox(vec3_t emins, vec3_t emaxs) {
  int i;
  mplane_t *p;
  byte signbits;
  float vec[3];

  for (i = 0; i < 4; i++) {
    p = frustum + i;
    signbits = p->signbits;
    vec[0] = ((signbits & 1) ? emins : emaxs)[0];
    vec[1] = ((signbits & 2) ? emins : emaxs)[1];
    vec[2] = ((signbits & 4) ? emins : emaxs)[2];
    if (p->normal[0] * vec[0] + p->normal[1] * vec[1] + p->normal[2] * vec[2] <
        p->dist)
      return true;
  }
  return false;
}

/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity(entity_t *e) {
  vec3_t mins, maxs;
  vec_t scalefactor, *minbounds, *maxbounds;

  if (e->angles[0] || e->angles[2]) // pitch or roll
  {
    minbounds = e->model->rmins;
    maxbounds = e->model->rmaxs;
  } else if (e->angles[1]) // yaw
  {
    minbounds = e->model->ymins;
    maxbounds = e->model->ymaxs;
  } else // no rotation
  {
    minbounds = e->model->mins;
    maxbounds = e->model->maxs;
  }

  scalefactor = ENTSCALE_DECODE(e->scale);
  if (scalefactor != 1.0f) {
    VectorMA(e->origin, scalefactor, minbounds, mins);
    VectorMA(e->origin, scalefactor, maxbounds, maxs);
  } else {
    VectorAdd(e->origin, minbounds, mins);
    VectorAdd(e->origin, maxbounds, maxs);
  }

  return R_CullBox(mins, maxs);
}

/*
===============
R_RotateForEntity -- johnfitz -- modified to take origin and angles instead of
pointer to entity
===============
*/
void R_RotateForEntity(vec3_t origin, vec3_t angles, unsigned char scale) {
  float scalefactor = ENTSCALE_DECODE(scale);
  glTranslatef(origin[0], origin[1], origin[2]);
  glRotatef(angles[1], 0, 0, 1);
  glRotatef(-angles[0], 0, 1, 0);
  glRotatef(angles[2], 1, 0, 0);
  if (scalefactor != 1.0f)
    glScalef(scalefactor, scalefactor, scalefactor);
}

/*
=============
GL_PolygonOffset -- johnfitz

negative offset moves polygon closer to camera
=============
*/
void GL_PolygonOffset(int offset) {
  if (offset > 0) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(1, offset);
  } else if (offset < 0) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1, offset);
  } else {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
  }
}

//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

int SignbitsForPlane(mplane_t *out) {
  int bits, j;

  // for fast box on planeside test

  bits = 0;
  for (j = 0; j < 3; j++) {
    if (out->normal[j] < 0)
      bits |= 1 << j;
  }
  return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
void TurnVector(vec3_t out, const vec3_t forward, const vec3_t side,
                float angle) {
  float scale_forward, scale_side;

  scale_forward = cos(DEG2RAD(angle));
  scale_side = sin(DEG2RAD(angle));

  out[0] = scale_forward * forward[0] + scale_side * side[0];
  out[1] = scale_forward * forward[1] + scale_side * side[1];
  out[2] = scale_forward * forward[2] + scale_side * side[2];
}

/*
===============
R_SetFrustum -- johnfitz -- rewritten
===============
*/
void R_SetFrustum(float fovx, float fovy) {
  int i;

  if (r_stereo.value)
    fovx +=
        10; // silly hack so that polygons don't drop out becuase of stereo skew

  if (vr_enabled.value)
    fovx += 25; // meh

  TurnVector(frustum[0].normal, vpn, vright, fovx / 2 - 90); // left plane
  TurnVector(frustum[1].normal, vpn, vright, 90 - fovx / 2); // right plane
  TurnVector(frustum[2].normal, vpn, vup, 90 - fovy / 2);    // bottom plane
  TurnVector(frustum[3].normal, vpn, vup, fovy / 2 - 90);    // top plane

  for (i = 0; i < 4; i++) {
    frustum[i].type = PLANE_ANYZ;
    frustum[i].dist = DotProduct(
        r_origin, frustum[i].normal); // FIXME: shouldn't this always be zero?
    frustum[i].signbits = SignbitsForPlane(&frustum[i]);
  }
}

/*
=============
GL_SetFrustum -- johnfitz -- written to replace MYgluPerspective
=============
*/
#define NEARCLIP 4
float frustum_skew = 0.0; // used by r_stereo
void GL_SetFrustum(float fovx, float fovy) {
  float xmax, ymax;
  xmax = NEARCLIP * tan(fovx * M_PI / 360.0);
  ymax = NEARCLIP * tan(fovy * M_PI / 360.0);
  glFrustum(-xmax + frustum_skew, xmax + frustum_skew, -ymax, ymax, NEARCLIP,
            gl_farclip.value);
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL(void) {
  int scale;

  if (vr_enabled.value) {
    VR_SetMatrices();
  } else {
    // johnfitz -- rewrote this section
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    scale = CLAMP(1, (int)r_scale.value, 4); // ericw -- see R_ScaleView
    glViewport(glx + r_refdef.vrect.x,
               gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height,
               r_refdef.vrect.width / scale, r_refdef.vrect.height / scale);
    // johnfitz

    GL_SetFrustum(r_fovx, r_fovy); // johnfitz -- use r_fov* vars
  }

  //	glCullFace(GL_BACK); //johnfitz -- glquake used CCW with backwards
  // culling -- let's do it right

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glRotatef(-90, 1, 0, 0); // put Z going up
  glRotatef(90, 0, 0, 1);  // put Z going up
  glRotatef(-r_refdef.viewangles[2], 1, 0, 0);
  glRotatef(-r_refdef.viewangles[0], 0, 1, 0);
  glRotatef(-r_refdef.viewangles[1], 0, 0, 1);
  glTranslatef(-r_refdef.vieworg[0], -r_refdef.vieworg[1],
               -r_refdef.vieworg[2]);

  //
  // set drawing parms
  //
  if (gl_cull.value)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);

  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glEnable(GL_DEPTH_TEST);
}

/*
=============
R_Clear -- johnfitz -- rewritten and gutted
=============
*/
void R_Clear(void) {
  unsigned int clearbits;

  clearbits = GL_DEPTH_BUFFER_BIT;
  // from mh -- if we get a stencil buffer, we should clear it, even though we
  // don't use it
  if (gl_stencilbits)
    clearbits |= GL_STENCIL_BUFFER_BIT;
  if ((gl_clear.value || skyroom_drawing) && !skyroom_drawn)
    clearbits |= GL_COLOR_BUFFER_BIT;
  glClear(clearbits);
}

/*
===============
R_SetupScene -- johnfitz -- this is the stuff that needs to be done once per eye
in stereo mode
===============
*/
void R_SetupScene(void) { R_SetupGL(); }

static void R_SetupCheatSafeDrawModes(void) {
  // johnfitz -- cheat-protect some draw modes
  r_drawflat_cheatsafe = r_fullbright_cheatsafe = r_lightmap_cheatsafe = false;
  r_drawworld_cheatsafe = true;
  if (cl.maxclients == 1) {
    if (!r_drawworld.value)
      r_drawworld_cheatsafe = false;

    if (r_drawflat.value)
      r_drawflat_cheatsafe = true;
    else if (r_fullbright.value || !cl.worldmodel->lightdata)
      r_fullbright_cheatsafe = true;
    else if (r_lightmap.value)
      r_lightmap_cheatsafe = true;
  }
  // johnfitz
}

/*
===============
R_SetupView -- johnfitz -- this is the stuff that needs to be done once per
frame, even in stereo mode
===============
*/
static void R_SetupFrameState(void) {
  // Need to do those early because we now update dynamic light maps during
  // R_MarkSurfaces.
  R_PushDlights();
  R_AnimateLight();
  r_framecount++;

  Fog_SetupFrame(); // johnfitz
}

void R_SetupView(void) {
  double perf_start, perf_mark_start, perf_warp_start;

  perf_start = R_PerfStart();
  if (R_PerfActive())
    r_perf_setup_calls++;

  if (!R_IsVRStereoFrame() || R_IsVRFirstEye())
    R_SetupFrameState();

  // build the transformation matrix for the given view angles
  VectorCopy(r_refdef.vieworg, r_origin);
  AngleVectors(r_refdef.viewangles, vpn, vright, vup);

  if (R_IsVRStereoFrame() && !r_vr_sort_origin_valid) {
    VectorCopy(r_refdef.vieworg, r_vr_sort_origin);
    r_vr_sort_origin_valid = true;
  }

  // current viewleaf
  r_oldviewleaf = r_viewleaf;
  r_viewleaf = Mod_PointInLeaf(r_origin, cl.worldmodel);

  V_SetContentsColor(r_viewleaf->contents);
  V_CalcBlend();

  // johnfitz -- calculate r_fovx and r_fovy here
  r_fovx = r_refdef.fov_x;
  r_fovy = r_refdef.fov_y;
  if (r_waterwarp.value) {
    int contents = Mod_PointInLeaf(r_origin, cl.worldmodel)->contents;
    if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME ||
        contents == CONTENTS_LAVA) {
      // variance is a percentage of width, where width = 2 * tan(fov / 2)
      // otherwise the effect is too dramatic at high FOV and too subtle at low
      // FOV.  what a mess!
      r_fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) *
                    (0.97 + sin(cl.time * 1.5) * 0.03)) *
               2 / M_PI_DIV_180;
      r_fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) *
                    (1.03 - sin(cl.time * 1.5) * 0.03)) *
               2 / M_PI_DIV_180;
    }
  }
  // johnfitz

  R_SetFrustum(r_fovx, r_fovy); // johnfitz -- use r_fov* vars

  perf_mark_start = R_PerfStart();
  R_MarkSurfaces(); // johnfitz -- create texture chains from PVS
  R_PerfAdd(&r_perf_mark_ms, perf_mark_start);

  if (!skyroom_drawn) {
    perf_warp_start = R_PerfStart();
    R_UpdateWarpTextures(); // johnfitz -- do this before R_Clear
    R_PerfAdd(&r_perf_warp_ms, perf_warp_start);
  }

  R_Clear();

  R_SetupCheatSafeDrawModes();
  R_PerfAdd(&r_perf_setup_ms, perf_start);
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_DrawEntitiesOnList
=============
*/
static int R_AliasEntitySortCompare(const void *pa, const void *pb) {
  const entity_t *a = *(const entity_t *const *)pa;
  const entity_t *b = *(const entity_t *const *)pb;
  uintptr_t ak, bk;

  if (a->model != b->model) {
    ak = (uintptr_t)a->model;
    bk = (uintptr_t)b->model;
    return (ak > bk) - (ak < bk);
  }
  if (a->skinnum != b->skinnum)
    return (a->skinnum > b->skinnum) - (a->skinnum < b->skinnum);
  if (a->colormap != b->colormap) {
    ak = (uintptr_t)a->colormap;
    bk = (uintptr_t)b->colormap;
    return (ak > bk) - (ak < bk);
  }
  if (a->alpha != b->alpha)
    return (a->alpha > b->alpha) - (a->alpha < b->alpha);

  ak = (uintptr_t)a;
  bk = (uintptr_t)b;
  return (ak > bk) - (ak < bk);
}

typedef struct sorted_alpha_entity_s {
  entity_t *ent;
  float dist;
} sorted_alpha_entity_t;

static int R_AlphaEntitySortCompare(const void *pa, const void *pb) {
  const sorted_alpha_entity_t *a = (const sorted_alpha_entity_t *)pa;
  const sorted_alpha_entity_t *b = (const sorted_alpha_entity_t *)pb;

  if (a->dist < b->dist)
    return 1;
  if (a->dist > b->dist)
    return -1;
  return 0;
}

static float R_AlphaEntitySortDistance(entity_t *ent) {
  vec3_t delta;
  const vec_t *sort_origin;

  sort_origin = R_IsVRStereoFrame() ? R_VRStereoSortOrigin() : r_refdef.vieworg;
  VectorSubtract(ent->origin, sort_origin, delta);
  return DotProduct(delta, vpn);
}

static void R_DrawCurrentEntityOnList(void) {
  R_PerfCountEntity(currententity, ENTALPHA_DECODE(currententity->alpha) < 1);

  switch (currententity->model->type) {
  case mod_alias:
    R_DrawAliasModel(currententity);
    break;
  case mod_brush:
    R_DrawBrushModel(currententity);
    break;
  case mod_sprite:
    R_DrawSpriteModel(currententity);
    break;
  }
}

void R_DrawEntitiesOnList(qboolean alphapass) // johnfitz -- added parameter
{
  int i;
  entity_t *alias_ents[MAX_VISEDICTS];
  int alias_count = 0;

  if (!r_drawentities.value)
    return;

  if (alphapass && r_alphasort.value) {
    sorted_alpha_entity_t alpha_ents[MAX_VISEDICTS];
    int alpha_count = 0;

    for (i = 0; i < cl_numvisedicts; i++) {
      currententity = cl_visedicts[i];
      if (ENTALPHA_DECODE(currententity->alpha) == 1)
        continue;
      if (currententity == &cl.entities[cl.viewentity])
        currententity->angles[0] *= 0.3;
      alpha_ents[alpha_count].ent = currententity;
      alpha_ents[alpha_count].dist = R_AlphaEntitySortDistance(currententity);
      alpha_count++;
    }

    if (alpha_count > 1)
      qsort(alpha_ents, alpha_count, sizeof(alpha_ents[0]),
            R_AlphaEntitySortCompare);

    for (i = 0; i < alpha_count; i++) {
      currententity = alpha_ents[i].ent;
      R_DrawCurrentEntityOnList();
    }
    return;
  }

  if (!alphapass && r_alias_batching.value) {
    for (i = 0; i < cl_numvisedicts; i++) {
      currententity = cl_visedicts[i];

      if (ENTALPHA_DECODE(currententity->alpha) < 1)
        continue;

      // johnfitz -- chasecam
      if (currententity == &cl.entities[cl.viewentity])
        currententity->angles[0] *= 0.3;
      // johnfitz

      if (currententity->model->type == mod_alias) {
        if (alias_count < MAX_VISEDICTS)
          alias_ents[alias_count++] = currententity;
        continue;
      }

      R_DrawCurrentEntityOnList();
    }

    if (alias_count > 1)
      qsort(alias_ents, alias_count, sizeof(alias_ents[0]),
            R_AliasEntitySortCompare);

    R_BeginAliasBatchScope();
    for (i = 0; i < alias_count; i++) {
      currententity = alias_ents[i];
      R_DrawAliasModel(currententity);
    }
    R_EndAliasBatchScope();
    return;
  }

  R_BeginAliasBatchScope();

  // johnfitz -- sprites are not a special case
  for (i = 0; i < cl_numvisedicts; i++) {
    currententity = cl_visedicts[i];

    // johnfitz -- if alphapass is true, draw only alpha entites this time
    // if alphapass is false, draw only nonalpha entities this time
    if ((ENTALPHA_DECODE(currententity->alpha) < 1 && !alphapass) ||
        (ENTALPHA_DECODE(currententity->alpha) == 1 && alphapass))
      continue;

    // johnfitz -- chasecam
    if (currententity == &cl.entities[cl.viewentity])
      currententity->angles[0] *= 0.3;
    // johnfitz

    if (currententity->model->type != mod_alias)
      R_EndAliasBatchScope();
    R_DrawCurrentEntityOnList();
    if (currententity->model->type != mod_alias)
      R_BeginAliasBatchScope();
  }

  R_EndAliasBatchScope();
}

/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
void R_DrawViewModel(void) {
  if (skyroom_drawing)
    return;

  if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value)
    return;

  if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
    return;

  if (vr_enabled.value && vr_crosshair.value)
    VR_ShowCrosshair();

  currententity = &cl.viewent;
  if (!currententity->model)
    return;

  // johnfitz -- this fixes a crash
  if (currententity->model->type != mod_alias)
    return;
  // johnfitz

  // hack the depth range to prevent view model from poking into walls
  // only when not in VR
  if (!vr_enabled.value)
    glDepthRange(0, 0.3);

  VR_ApplyCurrentViewWeaponTransform();

  R_DrawAliasModel(currententity);

  if (vr_enabled.value)
    VR_DrawAdjustmentControllers();

  if (!vr_enabled.value)
    glDepthRange(0, 1);
}

/*
================
R_EmitWirePoint -- johnfitz -- draws a wireframe cross shape for point entities
================
*/
void R_EmitWirePoint(vec3_t origin) {
  const int size = 8;

  glBegin(GL_LINES);
  glVertex3f(origin[0] - size, origin[1], origin[2]);
  glVertex3f(origin[0] + size, origin[1], origin[2]);
  glVertex3f(origin[0], origin[1] - size, origin[2]);
  glVertex3f(origin[0], origin[1] + size, origin[2]);
  glVertex3f(origin[0], origin[1], origin[2] - size);
  glVertex3f(origin[0], origin[1], origin[2] + size);
  glEnd();
}

/*
================
R_EmitWireBox -- johnfitz -- draws one axis aligned bounding box
================
*/
void R_EmitWireBox(vec3_t mins, vec3_t maxs) {
  glBegin(GL_QUAD_STRIP);
  glVertex3f(mins[0], mins[1], mins[2]);
  glVertex3f(mins[0], mins[1], maxs[2]);
  glVertex3f(maxs[0], mins[1], mins[2]);
  glVertex3f(maxs[0], mins[1], maxs[2]);
  glVertex3f(maxs[0], maxs[1], mins[2]);
  glVertex3f(maxs[0], maxs[1], maxs[2]);
  glVertex3f(mins[0], maxs[1], mins[2]);
  glVertex3f(mins[0], maxs[1], maxs[2]);
  glVertex3f(mins[0], mins[1], mins[2]);
  glVertex3f(mins[0], mins[1], maxs[2]);
  glEnd();
}

/*
================
R_ShowBoundingBoxes -- johnfitz

draw bounding boxes -- the server-side boxes, not the renderer cullboxes
================
*/
void R_ShowBoundingBoxes(void) {
  extern edict_t *sv_player;
  vec3_t mins, maxs;
  edict_t *ed;
  int i;
  qcvm_t *oldvm;

  if (!r_showbboxes.value || cl.maxclients > 1 || !r_drawentities.value ||
      !sv.active)
    return;

  glDisable(GL_DEPTH_TEST);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  GL_PolygonOffset(OFFSET_SHOWTRIS);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);
  glColor3f(1, 1, 1);

  oldvm = qcvm;
  PR_SwitchQCVM(NULL);
  PR_SwitchQCVM(&sv.qcvm);

  for (i = 1, ed = NEXT_EDICT(qcvm->edicts); i < qcvm->num_edicts;
       i++, ed = NEXT_EDICT(ed)) {
    if (ed == sv_player || ed->free)
      continue; // don't draw player's own bbox or freed edicts

    //		if (r_showbboxes.value != 2)
    //			if (!SV_VisibleToClient (sv_player, ed, sv.worldmodel))
    //				continue; //don't draw if not in pvs

    if (ed->v.mins[0] == ed->v.maxs[0] && ed->v.mins[1] == ed->v.maxs[1] &&
        ed->v.mins[2] == ed->v.maxs[2]) {
      // point entity
      R_EmitWirePoint(ed->v.origin);
    } else {
      // box entity
      VectorAdd(ed->v.mins, ed->v.origin, mins);
      VectorAdd(ed->v.maxs, ed->v.origin, maxs);
      R_EmitWireBox(mins, maxs);
    }
  }

  glColor3f(1, 1, 1);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_CULL_FACE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  GL_PolygonOffset(OFFSET_NONE);
  glEnable(GL_DEPTH_TEST);

  PR_SwitchQCVM(NULL);
  PR_SwitchQCVM(oldvm);

  Sbar_Changed(); // so we don't get dots collecting on the statusbar
}

/*
================
R_ShowTris -- johnfitz
================
*/
void R_ShowTris(void) {
  extern cvar_t r_particles;
  int i;

  if (r_showtris.value < 1 || r_showtris.value > 2 || cl.maxclients > 1)
    return;

  if (r_showtris.value == 1)
    glDisable(GL_DEPTH_TEST);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  GL_PolygonOffset(OFFSET_SHOWTRIS);
  glDisable(GL_TEXTURE_2D);
  glColor3f(1, 1, 1);
  //	glEnable (GL_BLEND);
  //	glBlendFunc (GL_ONE, GL_ONE);

  if (r_drawworld.value) {
    R_DrawWorld_ShowTris();
  }

  if (r_drawentities.value) {
    for (i = 0; i < cl_numvisedicts; i++) {
      currententity = cl_visedicts[i];

      if (currententity == &cl.entities[cl.viewentity]) // chasecam
        currententity->angles[0] *= 0.3;

      switch (currententity->model->type) {
      case mod_brush:
        R_DrawBrushModel_ShowTris(currententity);
        break;
      case mod_alias:
        R_DrawAliasModel_ShowTris(currententity);
        break;
      case mod_sprite:
        R_DrawSpriteModel(currententity);
        break;
      default:
        break;
      }
    }

    // viewmodel
    currententity = &cl.viewent;
    if (r_drawviewmodel.value && !chase_active.value &&
        cl.stats[STAT_HEALTH] > 0 && !(cl.items & IT_INVISIBILITY) &&
        currententity->model && currententity->model->type == mod_alias) {
      glDepthRange(0, 0.3);
      R_DrawAliasModel_ShowTris(currententity);
      glDepthRange(0, 1);
    }
  }

  if (r_particles.value) {
    R_DrawParticles_ShowTris();
  }

  //	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  //	glDisable (GL_BLEND);
  glColor3f(1, 1, 1);
  glEnable(GL_TEXTURE_2D);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  GL_PolygonOffset(OFFSET_NONE);
  if (r_showtris.value == 1)
    glEnable(GL_DEPTH_TEST);

  Sbar_Changed(); // so we don't get dots collecting on the statusbar
}

/*
================
R_DrawShadows
================
*/
void R_DrawShadows(void) {
  int i;

  if (!r_shadows.value || !r_drawentities.value || r_drawflat_cheatsafe ||
      r_lightmap_cheatsafe)
    return;

  // Use stencil buffer to prevent self-intersecting shadows, from Baker (MarkV)
  if (gl_stencilbits) {
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_EQUAL, 0, ~0);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glEnable(GL_STENCIL_TEST);
  }

  for (i = 0; i < cl_numvisedicts; i++) {
    currententity = cl_visedicts[i];

    if (currententity->model->type != mod_alias)
      continue;

    if (currententity == &cl.viewent)
      return;

    GL_DrawAliasShadow(currententity);
  }

  if (gl_stencilbits) {
    glDisable(GL_STENCIL_TEST);
  }
}

/*
================
R_DrawPlayerOutlines -- Draw colored outlines around other players, visible
through walls, when holding the scoreboard button in co-op.
Uses a two-pass stencil technique per player:
  Pass 1: write stencil mask at normal model scale (no color output)
  Pass 2: draw inflated model only outside stencil mask, with shirt color
================
*/
static void R_DrawPlayerThroughWallSilhouette (entity_t *e, float r, float g, float b, float a)
{
	if (!e || !e->model || e->model->type != mod_alias)
		return;

	glDisable (GL_DEPTH_TEST);
	glDepthMask (GL_FALSE);
	glDisable (GL_CULL_FACE);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	currententity = e;
	R_DrawAliasModelOutline (e, r, g, b, a, 1.04f);

	glDisable (GL_BLEND);
	glDepthMask (GL_TRUE);
	glEnable (GL_DEPTH_TEST);
	if (gl_cull.value)
		glEnable (GL_CULL_FACE);
	else
		glDisable (GL_CULL_FACE);
	glColor4f (1, 1, 1, 1);
}

static void R_GetPlayerShirtColor (int playernum, float boost, float min_peak,
		float *r, float *g, float *b)
{
	int	topcolor;
	byte	*rgb;
	float	maxc, scale;

	topcolor = (cl.scores[playernum].colors >> 4) & 0xF;
	rgb = (byte *)&d_8to24table[topcolor * 16 + 8];
	*r = rgb[0] / 255.0f;
	*g = rgb[1] / 255.0f;
	*b = rgb[2] / 255.0f;

	maxc = q_max (*r, q_max (*g, *b));
	if (maxc <= 0)
	{
		*r = *g = *b = min_peak;
		return;
	}

	scale = boost;
	if (maxc * scale < min_peak)
		scale = min_peak / maxc;

	*r *= scale;
	*g *= scale;
	*b *= scale;
}

static void R_DrawWeaponMenuSelectedPlayerSilhouette (void)
{
	int		playernum, entnum;
	float		r, g, b;
	entity_t	*e;

	if (!cl.in_vr_weaponmenu)
		return;
	if (!vr_weaponmenu_player_teleport.value)
		return;
	if (vr_weaponmenu_selection_type != VR_WEAPONMENU_SELECTION_PLAYER)
		return;
	if (cl.gametype != GAME_COOP || !cl.scores)
		return;

	playernum = vr_weaponmenu_selection;
	if (playernum < 0 || playernum >= cl.maxclients || playernum >= MAX_SCOREBOARD)
		return;
	if (!cl.scores[playernum].name[0])
		return;

	entnum = playernum + 1;
	if (entnum == cl.viewentity)
		return;

	e = &cl.entities[entnum];
	if (!e->model || e->model->type != mod_alias)
		return;

	R_GetPlayerShirtColor (playernum, 1.8f, 0.5f, &r, &g, &b);
	R_DrawPlayerThroughWallSilhouette (e, r, g, b, 0.52f);
}

static void R_DrawPlayerOutlines (void)
{
	int		i, playernum;
	entity_t	*e;
	float		r, g, b;

	if (!Sbar_IsShowingScores ())
		return;
	if (cl.gametype != GAME_COOP)
		return;

	if (!vr_enabled.value || !gl_stencilbits)
	{
		for (i = 1; i <= cl.maxclients; i++)
		{
			e = &cl.entities[i];

			if (i == cl.viewentity)
				continue;

			playernum = i - 1;
			if (!cl.scores[playernum].name[0])
				continue;

			R_GetPlayerShirtColor (playernum, 1.5f, 0.45f, &r, &g, &b);
			R_DrawPlayerThroughWallSilhouette (e, r, g, b, 0.35f);
		}

		return;
	}

	for (i = 1; i <= cl.maxclients; i++)
	{
		e = &cl.entities[i];

		if (!e->model)
			continue;
		if (e->model->type != mod_alias)
			continue;
		if (i == cl.viewentity)
			continue;

		playernum = i - 1;
		if (!cl.scores[playernum].name[0])
			continue;
		R_GetPlayerShirtColor (playernum, 1.0f, 0.0f, &r, &g, &b);

		currententity = e;

		// Pass 1: stencil mask at normal scale -- mark the model's screen area
		glClear (GL_STENCIL_BUFFER_BIT);
		glEnable (GL_STENCIL_TEST);
		glStencilFunc (GL_ALWAYS, 1, 0xFF);
		glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE);
		glColorMask (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glDepthMask (GL_FALSE);
		glDisable (GL_DEPTH_TEST);

		R_DrawAliasModelOutline (e, 0, 0, 0, 1.0f, 1.0f);

		// Pass 2: draw inflated outline where stencil != 1 (the ring around the model)
		glStencilFunc (GL_NOTEQUAL, 1, 0xFF);
		glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
		glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glEnable (GL_BLEND);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		R_DrawAliasModelOutline (e, r, g, b, 0.7f, 1.05f);

		glDisable (GL_BLEND);
		glDisable (GL_STENCIL_TEST);
	}

	// Restore state
	glDepthMask (GL_TRUE);
	glEnable (GL_DEPTH_TEST);
	glColor3f (1, 1, 1);
}

#define NAMETAG_CHAR_WIDTH 3.0f
#define NAMETAG_CHAR_HEIGHT 3.0f
#define NAMETAG_HEAD_OFFSET 7.0f
#define NAMETAG_SHADOW_OFFSET 0.35f
static void R_EmitNametagChar(vec3_t pos, unsigned char ch,
                              float char_width, float char_height) {
  int row, col;
  float frow, fcol, size;
  vec3_t p0, p1, p2, p3;

  row = ch >> 4;
  col = ch & 15;
  frow = row * 0.0625f;
  fcol = col * 0.0625f;
  size = 0.0625f;

  VectorCopy(pos, p0);
  VectorMA(pos, char_width, vright, p1);
  VectorMA(p1, char_height, vup, p2);
  VectorMA(pos, char_height, vup, p3);

  glTexCoord2f(fcol, frow + size);
  glVertex3fv(p0);
  glTexCoord2f(fcol + size, frow + size);
  glVertex3fv(p1);
  glTexCoord2f(fcol + size, frow);
  glVertex3fv(p2);
  glTexCoord2f(fcol, frow);
  glVertex3fv(p3);
}

static void R_DrawNametagString(vec3_t origin, const char *str, float r,
                                float g, float b, float alpha) {
  int i, len;
  vec3_t pos;

  len = (int)strlen(str);
  if (!len)
    return;

  VectorMA(origin, -0.5f * len * NAMETAG_CHAR_WIDTH, vright, pos);

  glColor4f(r, g, b, alpha);
  glBegin(GL_QUADS);
  for (i = 0; i < len; i++) {
    if ((unsigned char)str[i] != 32)
      R_EmitNametagChar(pos, (unsigned char)str[i], NAMETAG_CHAR_WIDTH,
                        NAMETAG_CHAR_HEIGHT);
    VectorMA(pos, NAMETAG_CHAR_WIDTH, vright, pos);
  }
  glEnd();
}

static void R_DrawCoopNametags(void) {
  int i, playernum;
  entity_t *e;
  vec3_t tagorg, shadoworg;
  float scale, r, g, b;
  extern gltexture_t *char_texture;

  if (!cl_coop_nametags.value)
    return;
  if (cl.gametype != GAME_COOP)
    return;
  if (!r_drawentities.value)
    return;
  if (!char_texture)
    return;

  GL_DisableMultitexture();
  GL_Bind(char_texture);
  glEnable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_CULL_FACE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glDepthMask(GL_FALSE);

  for (i = 1; i <= cl.maxclients; i++) {
    if (i == cl.viewentity)
      continue;

    e = &cl.entities[i];
    if (!e->model || e->model->type != mod_alias)
      continue;

    playernum = i - 1;
    if (!cl.scores[playernum].name[0])
      continue;

    R_GetPlayerShirtColor(playernum, 1.65f, 0.55f, &r, &g, &b);

    scale = ENTSCALE_DECODE(e->scale);
    VectorCopy(e->origin, tagorg);
    tagorg[2] += e->model->maxs[2] * scale + NAMETAG_HEAD_OFFSET;

    VectorMA(tagorg, NAMETAG_SHADOW_OFFSET, vright, shadoworg);
    VectorMA(shadoworg, -NAMETAG_SHADOW_OFFSET, vup, shadoworg);
    R_DrawNametagString(shadoworg, cl.scores[playernum].name, 0, 0, 0, 0.65f);
    R_DrawNametagString(tagorg, cl.scores[playernum].name, r, g, b, 1.0f);
  }

  glDepthMask(GL_TRUE);
  if (gl_cull.value)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glDisable(GL_ALPHA_TEST);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glColor4f(1, 1, 1, 1);
}

/*
================
R_RenderScene
================
*/
void R_RenderScene(void) {
  double perf_scene_start, perf_start;

  perf_scene_start = R_PerfStart();
  if (R_PerfActive())
    r_perf_scene_calls++;

  R_SetupScene(); // johnfitz -- this does everything that should be done once
                  // per call to RenderScene

  Fog_EnableGFog(); // johnfitz

  perf_start = R_PerfStart();
  Sky_DrawSky(); // johnfitz
  R_PerfAdd(&r_perf_sky_ms, perf_start);

  perf_start = R_PerfStart();
  R_DrawWorld();
  R_PerfAdd(&r_perf_world_ms, perf_start);

  S_ExtraUpdate(); // don't let sound get messed up if going slow

  perf_start = R_PerfStart();
  R_DrawShadows(); // johnfitz -- render entity shadows
  R_PerfAdd(&r_perf_shadows_ms, perf_start);

  perf_start = R_PerfStart();
  R_DrawEntitiesOnList(
      false); // johnfitz -- false means this is the pass for nonalpha entities
  R_PerfAdd(&r_perf_entities_opaque_ms, perf_start);

  perf_start = R_PerfStart();
  R_DrawWorld_Water(); // johnfitz -- drawn here since they might have
                       // transparency
  R_PerfAdd(&r_perf_water_ms, perf_start);

  perf_start = R_PerfStart();
  R_DrawEntitiesOnList(
      true); // johnfitz -- true means this is the pass for alpha entities
  R_PerfAdd(&r_perf_entities_alpha_ms, perf_start);

  perf_start = R_PerfStart();
  R_RenderDlights(); // triangle fan dlights -- johnfitz -- moved after water
  R_PerfAdd(&r_perf_dlights_ms, perf_start);

  perf_start = R_PerfStart();
  R_DrawParticles();
#ifdef PSET_SCRIPT
  PScript_DrawParticles();
#endif
  R_PerfAdd(&r_perf_particles_ms, perf_start);

  Fog_DisableGFog(); // johnfitz

  if (!skyroom_drawing) {
    perf_start = R_PerfStart();
    R_DrawPlayerOutlines(); // co-op player outlines through walls
    R_DrawWeaponMenuSelectedPlayerSilhouette();
    R_DrawCoopNametags();
    R_PerfAdd(&r_perf_outlines_ms, perf_start);
  }

  if (!skyroom_drawing && cl.in_vr_weaponmenu)
    VR_DrawWeaponMenu();

  perf_start = R_PerfStart();
  R_DrawViewModel(); // johnfitz -- moved here from R_RenderView
  R_PerfAdd(&r_perf_viewmodel_ms, perf_start);

  perf_start = R_PerfStart();
  R_ShowTris(); // johnfitz

  R_ShowBoundingBoxes(); // johnfitz
  R_PerfAdd(&r_perf_debugdraw_ms, perf_start);
  R_PerfAdd(&r_perf_scene_ms, perf_scene_start);
}

static GLuint r_scaleview_texture;
static int r_scaleview_texture_width, r_scaleview_texture_height;

/*
=============
R_ScaleView_DeleteTexture
=============
*/
void R_ScaleView_DeleteTexture(void) {
  glDeleteTextures(1, &r_scaleview_texture);
  r_scaleview_texture = 0;
}

/*
================
R_ScaleView

The r_scale cvar allows rendering the 3D view at 1/2, 1/3, or 1/4 resolution.
This function scales the reduced resolution 3D view back up to fill
r_refdef.vrect. This is for emulating a low-resolution pixellated look,
or possibly as a perforance boost on slow graphics cards.
================
*/
void R_ScaleView(void) {
  float smax, tmax;
  int scale;
  int srcx, srcy, srcw, srch;

  // copied from R_SetupGL()
  scale = CLAMP(1, (int)r_scale.value, 4);
  srcx = glx + r_refdef.vrect.x;
  srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
  srcw = r_refdef.vrect.width / scale;
  srch = r_refdef.vrect.height / scale;

  if (scale == 1)
    return;

  // make sure texture unit 0 is selected
  GL_DisableMultitexture();

  // create (if needed) and bind the render-to-texture texture
  if (!r_scaleview_texture) {
    glGenTextures(1, &r_scaleview_texture);

    r_scaleview_texture_width = 0;
    r_scaleview_texture_height = 0;
  }
  glBindTexture(GL_TEXTURE_2D, r_scaleview_texture);

  // resize render-to-texture texture if needed
  if (r_scaleview_texture_width < srcw || r_scaleview_texture_height < srch) {
    r_scaleview_texture_width = srcw;
    r_scaleview_texture_height = srch;

    if (!gl_texture_NPOT) {
      r_scaleview_texture_width = TexMgr_Pad(r_scaleview_texture_width);
      r_scaleview_texture_height = TexMgr_Pad(r_scaleview_texture_height);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r_scaleview_texture_width,
                 r_scaleview_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }

  // copy the framebuffer to the texture
  glBindTexture(GL_TEXTURE_2D, r_scaleview_texture);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);

  // draw the texture back to the framebuffer
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  glViewport(srcx, srcy, r_refdef.vrect.width, r_refdef.vrect.height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  // correction factor if we lack NPOT textures, normally these are 1.0f
  smax = srcw / (float)r_scaleview_texture_width;
  tmax = srch / (float)r_scaleview_texture_height;

  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2f(-1, -1);
  glTexCoord2f(smax, 0);
  glVertex2f(1, -1);
  glTexCoord2f(smax, tmax);
  glVertex2f(1, 1);
  glTexCoord2f(0, tmax);
  glVertex2f(-1, 1);
  glEnd();

  // clear cached binding
  GL_ClearBindings();
}

/*
================
R_RenderView
================
*/
void R_RenderView(void) {
  double time1, time2, perf_frame_start, perf_start;

  if (r_norefresh.value)
    return;

  if (!cl.worldmodel)
    Sys_Error("R_RenderView: NULL worldmodel");

  perf_frame_start = R_PerfStart();
  if (R_PerfActive())
    R_PerfResetFrame();

  time1 = 0; /* avoid compiler warning */
  if (r_speeds.value) {
    glFinish();
    time1 = Sys_DoubleTime();

    // johnfitz -- rendering statistics
    rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_dynamiclightmaps =
        rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
  } else if (gl_finish.value)
    glFinish();

  // If the previous frame preserved a skyroom, update any pending warp
  // textures before drawing this frame's skyroom. The skyroom color clear will
  // erase the framebuffer scratch area before the user can see it.
  if (skyroom_drawn) {
    perf_start = R_PerfStart();
    R_UpdateWarpTextures();
    R_PerfAdd(&r_perf_warp_ms, perf_start);
  }
  perf_start = R_PerfStart();
  Sky_DrawSkyRoom();
  R_PerfAdd(&r_perf_skyroom_ms, perf_start);

  R_SetupView(); // johnfitz -- this does everything that should be done once
                 // per frame

  // johnfitz -- stereo rendering -- full of hacky goodness
  if (r_stereo.value) {
    float eyesep = CLAMP(-8.0f, r_stereo.value, 8.0f);
    float fdepth = CLAMP(32.0f, r_stereodepth.value, 1024.0f);

    AngleVectors(r_refdef.viewangles, vpn, vright, vup);

    // render left eye (red)
    glColorMask(1, 0, 0, 1);
    VectorMA(r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
    frustum_skew = 0.5 * eyesep * NEARCLIP / fdepth;
    srand((int)(cl.time * 1000)); // sync random stuff between eyes

    R_RenderScene();

    // render right eye (cyan)
    glClear(GL_DEPTH_BUFFER_BIT);
    glColorMask(0, 1, 1, 1);
    VectorMA(r_refdef.vieworg, 1.0f * eyesep, vright, r_refdef.vieworg);
    frustum_skew = -frustum_skew;
    srand((int)(cl.time * 1000)); // sync random stuff between eyes

    R_RenderScene();

    // restore
    glColorMask(1, 1, 1, 1);
    VectorMA(r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
    frustum_skew = 0.0f;
  } else {
    R_RenderScene();
  }
  // johnfitz

  perf_start = R_PerfStart();
  R_ScaleView();
  R_PerfAdd(&r_perf_scale_ms, perf_start);

  // johnfitz -- modified r_speeds output
  time2 = Sys_DoubleTime();
  R_PerfLogFrame((time2 - perf_frame_start) * 1000.0);
  if (r_pos.value)
    Con_Printf("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
               (int)cl.entities[cl.viewentity].origin[0],
               (int)cl.entities[cl.viewentity].origin[1],
               (int)cl.entities[cl.viewentity].origin[2],
               (int)cl.viewangles[PITCH], (int)cl.viewangles[YAW],
               (int)cl.viewangles[ROLL]);
  else if (r_speeds.value == 2)
    Con_Printf(
        "%3i ms  %4i/%4i wpoly %4i/%4i epoly %3i lmap %4i/%4i sky %1.1f mtex\n",
        (int)((time2 - time1) * 1000), rs_brushpolys, rs_brushpasses,
        rs_aliaspolys, rs_aliaspasses, rs_dynamiclightmaps, rs_skypolys,
        rs_skypasses, TexMgr_FrameUsage());
  else if (r_speeds.value)
    Con_Printf("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
               (int)((time2 - time1) * 1000), rs_brushpolys, rs_aliaspolys,
               rs_dynamiclightmaps);
  // johnfitz
}
