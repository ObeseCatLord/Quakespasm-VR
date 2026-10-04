#ifndef R_SSAO_H
#define R_SSAO_H

// 4 input, 22 sampled working, 16 storage working, 8 composite,
// 8 preparation, 4 seven-binding mip and 4 two-binding lookup sets.
enum { SSAO_MAX_DESCRIPTOR_SETS = 66, SSAO_MAX_SAMPLED_DESCRIPTORS = 50, SSAO_MAX_STORAGE_DESCRIPTORS = 44 };

extern cvar_t					r_ssao;
extern vulkan_pipeline_layout_t ssao_layout;
extern vulkan_pipeline_t		ssao_pipelines[MAIN_RENDER_PASS_VARIANT_COUNT];
extern vulkan_pipeline_layout_t ssao_compute_layout;
extern vulkan_pipeline_t		ssao_prepare_pipeline, ssao_filter_pipeline;
extern vulkan_pipeline_t		ssao_evaluate_pipelines[3];
extern vulkan_pipeline_t		ssao_mip_pipeline;
extern vulkan_desc_set_layout_t ssao_mip_set_layout;
extern vulkan_desc_set_layout_t ssao_lookup_set_layout;

typedef struct
{
	vec4_t viewport;   // pixel origin and inverse size
	vec4_t projection; // inverse projection X/Y, near plane, sample count
	vec4_t settings;   // radius, strength
} ssao_constants_t;

void R_InitSSAO (void);
qboolean R_SSAOEnabled (void);
qboolean R_SSAOSupported (void);
void R_CreateSSAO (VkImage depth);
void R_DestroySSAO (void);
void R_PrepareSSAOWorldDepth (cb_context_t *cbx);
void R_ComputeSSAO (cb_context_t *cbx);
void R_DrawSSAOTask (void *unused);

#endif
