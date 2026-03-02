#include "quakedef.h"
#include "vr_openxr.h"

#ifdef VR_ENABLED

extern int glx, gly, glwidth, glheight;
extern cvar_t scr_sbaralpha;
extern cvar_t gl_farclip;
extern cvar_t vr_hud_dist;
extern cvar_t vr_hud_voffset;
extern cvar_t vr_hud_scale;
extern cvar_t vr_menu_dist;
extern cvar_t vr_menu_scale;

extern cvar_t vr_mirror;
extern float r_matproj[16];
extern float r_matview[16];

static GLuint vr_ui_prog = 0;
static GLint vr_ui_viewproj_loc = -1;

void VR_InitUI(void) {
  // HUD
  oxr.hud_width = 1024;
  oxr.hud_height = 512;
  GL_GenFramebuffersFunc(1, &oxr.hud_fbo);
  glGenTextures(1, &oxr.hud_texture);
  GL_BindNative(GL_TEXTURE0, GL_TEXTURE_2D, oxr.hud_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, oxr.hud_width, oxr.hud_height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  GL_BindFramebufferFunc(GL_FRAMEBUFFER, oxr.hud_fbo);
  GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, oxr.hud_texture, 0);

  // Menu
  oxr.menu_width = 1024;
  oxr.menu_height = 1024;
  GL_GenFramebuffersFunc(1, &oxr.menu_fbo);
  glGenTextures(1, &oxr.menu_texture);
  GL_BindNative(GL_TEXTURE0, GL_TEXTURE_2D, oxr.menu_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, oxr.menu_width, oxr.menu_height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  GL_BindFramebufferFunc(GL_FRAMEBUFFER, oxr.menu_fbo);
  GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, oxr.menu_texture, 0);

  GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
}

void VR_ShutdownUI(void) {
  if (oxr.hud_fbo) {
    GL_DeleteFramebuffersFunc(1, &oxr.hud_fbo);
    oxr.hud_fbo = 0;
  }
  if (oxr.hud_texture) {
    glDeleteTextures(1, &oxr.hud_texture);
    oxr.hud_texture = 0;
  }
  if (oxr.menu_fbo) {
    GL_DeleteFramebuffersFunc(1, &oxr.menu_fbo);
    oxr.menu_fbo = 0;
  }
  if (oxr.menu_texture) {
    glDeleteTextures(1, &oxr.menu_texture);
    oxr.menu_texture = 0;
  }
}

static void VR_InitUIShader(void) {
  if (vr_ui_prog)
    return;

  const GLchar *v_src = "#version 330 core\n"
                        "layout(location=0) in vec3 in_pos;\n"
                        "layout(location=1) in vec2 in_uv;\n"
                        "uniform mat4 ViewProj;\n"
                        "out vec2 out_uv;\n"
                        "void main() {\n"
                        "  gl_Position = ViewProj * vec4(in_pos, 1.0);\n"
                        "  out_uv = in_uv;\n"
                        "}\n";

  const GLchar *f_src = "#version 330 core\n"
                        "uniform sampler2D Tex;\n"
                        "in vec2 out_uv;\n"
                        "out vec4 out_fragcolor;\n"
                        "void main() {\n"
                        "  out_fragcolor = texture(Tex, out_uv);\n"
                        "}\n";

  GLuint v_shader = GL_CreateShaderFunc(GL_VERTEX_SHADER);
  GL_ShaderSourceFunc(v_shader, 1, &v_src, NULL);
  GL_CompileShaderFunc(v_shader);

  GLuint f_shader = GL_CreateShaderFunc(GL_FRAGMENT_SHADER);
  GL_ShaderSourceFunc(f_shader, 1, &f_src, NULL);
  GL_CompileShaderFunc(f_shader);

  vr_ui_prog = GL_CreateProgramFunc();
  GL_AttachShaderFunc(vr_ui_prog, v_shader);
  GL_AttachShaderFunc(vr_ui_prog, f_shader);
  GL_LinkProgramFunc(vr_ui_prog);

  GL_DeleteShaderFunc(v_shader);
  GL_DeleteShaderFunc(f_shader);

  vr_ui_viewproj_loc = GL_GetUniformLocationFunc(vr_ui_prog, "ViewProj");
}

static void VR_MatrixMultiply(const float *a, const float *b, float *out) {
  for (int col = 0; col < 4; col++) {
    for (int row = 0; row < 4; row++) {
      out[col * 4 + row] =
          a[0 * 4 + row] * b[col * 4 + 0] + a[1 * 4 + row] * b[col * 4 + 1] +
          a[2 * 4 + row] * b[col * 4 + 2] + a[3 * 4 + row] * b[col * 4 + 3];
    }
  }
}

static GLuint vr_quad_vao = 0;
static GLuint vr_quad_vbo = 0;

static void VR_DrawQuad(GLuint tex, float *verts) {
  VR_InitUIShader();
  GL_UseProgramFunc(vr_ui_prog);

  float mvp[16];
  VR_MatrixMultiply(r_matproj, r_matview, mvp);
  GL_UniformMatrix4fvFunc(vr_ui_viewproj_loc, 1, GL_FALSE, mvp);

  GL_BindNative(GL_TEXTURE0, GL_TEXTURE_2D, tex);

  // Enable blending for transparency
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (!vr_quad_vao) {
    GL_GenVertexArraysFunc(1, &vr_quad_vao);
    GL_GenBuffersFunc(1, &vr_quad_vbo);
  }
  GL_BindVertexArrayFunc(vr_quad_vao);

  // Upload vertex data to VBO (required for core profile)
  GL_BindBufferFunc(GL_ARRAY_BUFFER, vr_quad_vbo);
  GL_BufferDataFunc(GL_ARRAY_BUFFER, 4 * 5 * sizeof(float), verts,
                    GL_STREAM_DRAW);

  GL_EnableVertexAttribArrayFunc(0);
  GL_EnableVertexAttribArrayFunc(1);
  GL_VertexAttribPointerFunc(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                             (void *)0);
  GL_VertexAttribPointerFunc(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                             (void *)(3 * sizeof(float)));

  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  GL_BindBufferFunc(GL_ARRAY_BUFFER, 0);
  GL_BindVertexArrayFunc(0);

  GL_DisableVertexAttribArrayFunc(0);
  GL_DisableVertexAttribArrayFunc(1);
  glDisable(GL_BLEND);
}

void VR_DrawSbar(void) {
  if (!VR_Enabled() || !oxr.hud_fbo)
    return;

  int old_x = glx, old_y = gly;
  int old_w = glwidth, old_h = glheight;
  float old_guiw = vid.guiwidth, old_guih = vid.guiheight;

  GL_BindFramebufferFunc(GL_FRAMEBUFFER, oxr.hud_fbo);
  glViewport(0, 0, oxr.hud_width, oxr.hud_height);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glx = 0;
  gly = 0;
  glwidth = oxr.hud_width;
  glheight = oxr.hud_height;
  vid.guiwidth = oxr.hud_width;
  vid.guiheight = oxr.hud_height;

  GL_Set2D();
  Sbar_Draw();

  glx = old_x;
  gly = old_y;
  glwidth = old_w;
  glheight = old_h;
  vid.guiwidth = old_guiw;
  vid.guiheight = old_guih;

  GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
}

void VR_Draw2D(void) {
  if (!VR_Enabled() || !oxr.menu_fbo)
    return;

  int old_x = glx, old_y = gly;
  int old_w = glwidth, old_h = glheight;
  float old_guiw = vid.guiwidth, old_guih = vid.guiheight;

  GL_BindFramebufferFunc(GL_FRAMEBUFFER, oxr.menu_fbo);
  glViewport(0, 0, oxr.menu_width, oxr.menu_height);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glx = 0;
  gly = 0;
  glwidth = oxr.menu_width;
  glheight = oxr.menu_height;
  vid.guiwidth = oxr.menu_width;
  vid.guiheight = oxr.menu_height;

  GL_Set2D();

  // Draw console and menu
  Con_DrawConsole(vid.guiheight, true, true);
  M_Draw();

  glx = old_x;
  gly = old_y;
  glwidth = old_w;
  glheight = old_h;
  vid.guiwidth = old_guiw;
  vid.guiheight = old_guih;

  GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
}

static void VR_DrawHUDLayer(int eye) {
  if (!oxr.hud_texture)
    return;

  extern vec3_t vpn, vright, vup;

  // HUD Quad: use CVARs for positioning
  float w = 40.0f * vr_hud_scale.value;
  float h = 20.0f * vr_hud_scale.value;
  float d = vr_hud_dist.value;
  float y_off = vr_hud_voffset.value;

  vec3_t center, lb, rb, rt, lt;
  VectorMA(r_refdef.vieworg, d, vpn, center);
  VectorMA(center, y_off, vup, center);

  VectorMA(center, -w / 2, vright, lb);
  VectorMA(lb, -h / 2, vup, lb);
  VectorMA(center, w / 2, vright, rb);
  VectorMA(rb, -h / 2, vup, rb);
  VectorMA(center, w / 2, vright, rt);
  VectorMA(rt, h / 2, vup, rt);
  VectorMA(center, -w / 2, vright, lt);
  VectorMA(lt, h / 2, vup, lt);

  float verts[] = {
      lb[0], lb[1], lb[2], 0, 1, rb[0], rb[1], rb[2], 1, 1,
      rt[0], rt[1], rt[2], 1, 0, lt[0], lt[1], lt[2], 0, 0,
  };

  VR_DrawQuad(oxr.hud_texture, verts);
}

static void VR_DrawMenuLayer(int eye) {
  if (!oxr.menu_texture)
    return;

  // Only draw menu if it's active
  if (key_dest == key_game && !con_forcedup)
    return;

  extern vec3_t vpn, vright, vup;

  // Menu Quad: use CVARs for positioning
  float w = 50.0f * vr_menu_scale.value;
  float h = 50.0f * vr_menu_scale.value;
  float d = vr_menu_dist.value;

  vec3_t center, lb, rb, rt, lt;
  VectorMA(r_refdef.vieworg, d, vpn, center);

  VectorMA(center, -w / 2, vright, lb);
  VectorMA(lb, -h / 2, vup, lb);
  VectorMA(center, w / 2, vright, rb);
  VectorMA(rb, -h / 2, vup, rb);
  VectorMA(center, w / 2, vright, rt);
  VectorMA(rt, h / 2, vup, rt);
  VectorMA(center, -w / 2, vright, lt);
  VectorMA(lt, h / 2, vup, lt);

  float verts[] = {
      lb[0], lb[1], lb[2], 0, 1, rb[0], rb[1], rb[2], 1, 1,
      rt[0], rt[1], rt[2], 1, 0, lt[0], lt[1], lt[2], 0, 0,
  };

  VR_DrawQuad(oxr.menu_texture, verts);
}

void VR_Render(void) {
  if (!VR_Enabled())
    return;

  // Render UI to FBOs once per frame
  GL_BeginQueryFunc(GL_TIME_ELAPSED, oxr.queries[1]);
  VR_DrawSbar();
  VR_Draw2D();
  GL_EndQueryFunc(GL_TIME_ELAPSED);

  XrFrameWaitInfo frameWaitInfo = {XR_TYPE_FRAME_WAIT_INFO};
  XrFrameState frameState = {XR_TYPE_FRAME_STATE};
  XrResult res = xrWaitFrame(oxr.session, &frameWaitInfo, &frameState);
  if (XR_FAILED(res)) {
    return;
  }

  oxr.last_predicted_time = frameState.predictedDisplayTime;

  XrFrameBeginInfo frameBeginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
  res = xrBeginFrame(oxr.session, &frameBeginInfo);
  if (XR_FAILED(res)) {
    return;
  }

  XrCompositionLayerProjection projectionLayer = {
      XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  projectionLayer.space = oxr.stage_space;

  uint32_t viewCount = oxr.view_count;
  XrCompositionLayerProjectionView projectionLayerViews[viewCount];
  XrCompositionLayerDepthInfoKHR depthInfo[viewCount];

  if (frameState.shouldRender) {
    // --- LATE LATCHING ---
    // Locate views as late as possible, right before rendering.
    XrViewLocateInfo viewLocateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
    viewLocateInfo.viewConfigurationType = oxr.view_config_type;
    viewLocateInfo.displayTime = frameState.predictedDisplayTime;
    viewLocateInfo.space = oxr.stage_space;

    XrViewState viewState = {XR_TYPE_VIEW_STATE};
    res = xrLocateViews(oxr.session, &viewLocateInfo, &viewState, viewCount,
                        &viewCount, oxr.views);
    if (XR_FAILED(res))
      goto end_frame;

    if (viewCount > 0 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
      if (viewCount >= 2) {
        oxr.head_pose.position.x =
            (oxr.views[0].pose.position.x + oxr.views[1].pose.position.x) *
            0.5f;
        oxr.head_pose.position.y =
            (oxr.views[0].pose.position.y + oxr.views[1].pose.position.y) *
            0.5f;
        oxr.head_pose.position.z =
            (oxr.views[0].pose.position.z + oxr.views[1].pose.position.z) *
            0.5f;
        oxr.head_pose.orientation = oxr.views[0].pose.orientation;
      } else {
        oxr.head_pose = oxr.views[0].pose;
      }
    }

    for (uint32_t i = 0; i < viewCount; i++) {
      vr_swapchain_t *sc = &oxr.swapchains[i];

      uint32_t imageIndex;
      XrSwapchainImageAcquireInfo acquireInfo = {
          XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      xrAcquireSwapchainImage(sc->handle, &acquireInfo, &imageIndex);

      XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
      waitInfo.timeout = XR_INFINITE_DURATION;
      xrWaitSwapchainImage(sc->handle, &waitInfo);

      uint32_t depthImageIndex = 0;
      if (oxr.depth_extension_supported) {
        xrAcquireSwapchainImage(sc->depth_handle, &acquireInfo,
                                &depthImageIndex);
        xrWaitSwapchainImage(sc->depth_handle, &waitInfo);
      }

      projectionLayerViews[i] = (XrCompositionLayerProjectionView){
          XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
      projectionLayerViews[i].pose = oxr.views[i].pose;
      projectionLayerViews[i].fov = oxr.views[i].fov;
      projectionLayerViews[i].subImage.swapchain = sc->handle;
      projectionLayerViews[i].subImage.imageRect.offset.x = 0;
      projectionLayerViews[i].subImage.imageRect.offset.y = 0;
      projectionLayerViews[i].subImage.imageRect.extent.width = sc->width;
      projectionLayerViews[i].subImage.imageRect.extent.height = sc->height;
      projectionLayerViews[i].subImage.imageArrayIndex = 0;

      if (oxr.depth_extension_supported) {
        depthInfo[i] = (XrCompositionLayerDepthInfoKHR){
            XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR};
        depthInfo[i].subImage.swapchain = sc->depth_handle;
        depthInfo[i].subImage.imageRect.offset.x = 0;
        depthInfo[i].subImage.imageRect.offset.y = 0;
        depthInfo[i].subImage.imageRect.extent.width = sc->width;
        depthInfo[i].subImage.imageRect.extent.height = sc->height;
        depthInfo[i].subImage.imageArrayIndex = 0;
        depthInfo[i].minDepth = 0.0f;
        depthInfo[i].maxDepth = 1.0f;
        depthInfo[i].nearZ = 4.0f;
        depthInfo[i].farZ = gl_farclip.value;

        projectionLayerViews[i].next = &depthInfo[i];
      }

      GL_BindFramebufferFunc(GL_FRAMEBUFFER, sc->fbos[imageIndex]);
      if (oxr.depth_extension_supported) {
        GL_FramebufferTexture2DFunc(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                    GL_TEXTURE_2D,
                                    sc->depth_images[depthImageIndex].image, 0);
      }
      glViewport(0, 0, sc->width, sc->height);

      VR_SetMatrices(i);

      GL_BeginQueryFunc(GL_TIME_ELAPSED, oxr.queries[2 + i]);
      R_RenderView();
      GL_EndQueryFunc(GL_TIME_ELAPSED);

      // Draw VR overlays (Phase 11)
      VR_DrawHUDLayer(i);
      VR_DrawMenuLayer(i);

      // Mirror left eye to the desktop window before releasing the swapchain image
      if (i == 0 && vr_mirror.value)
      {
        GL_BindFramebufferFunc(GL_READ_FRAMEBUFFER, sc->fbos[imageIndex]);
        GL_BindFramebufferFunc(GL_DRAW_FRAMEBUFFER, 0);
        GL_BlitFramebufferFunc(0, 0, sc->width, sc->height,
                               0, 0, vid.width, vid.height,
                               GL_COLOR_BUFFER_BIT, GL_LINEAR);
        GL_BindFramebufferFunc(GL_FRAMEBUFFER, 0);
      }

      XrSwapchainImageReleaseInfo releaseInfo = {
          XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(sc->handle, &releaseInfo);
      if (oxr.depth_extension_supported) {
        xrReleaseSwapchainImage(sc->depth_handle, &releaseInfo);
      }
    }

    projectionLayer.viewCount = viewCount;
    projectionLayer.views = projectionLayerViews;
  }

end_frame: {
  XrFrameEndInfo frameEndInfo = {XR_TYPE_FRAME_END_INFO};
  frameEndInfo.displayTime = frameState.predictedDisplayTime;
  frameEndInfo.environmentBlendMode = oxr.blend_mode;
  frameEndInfo.layerCount = (frameState.shouldRender && viewCount > 0) ? 1 : 0;
  const XrCompositionLayerBaseHeader *layers[] = {
      (XrCompositionLayerBaseHeader *)&projectionLayer};
  frameEndInfo.layers = layers;
  xrEndFrame(oxr.session, &frameEndInfo);
}

  // Collect profiling results (with 1-frame latency to avoid stalls)
  for (int i = 0; i < 4; i++) {
    GLuint available = 0;
    GL_GetQueryObjectuivFunc(oxr.queries[i], GL_QUERY_RESULT_AVAILABLE,
                             &available);
    if (available) {
      GLuint64 time = 0;
      GL_GetQueryObjectui64vFunc(oxr.queries[i], GL_QUERY_RESULT, &time);
      oxr.frame_times[i] = (double)time / 1000000.0; // ns to ms
    }
  }
}

#endif // VR_ENABLED
