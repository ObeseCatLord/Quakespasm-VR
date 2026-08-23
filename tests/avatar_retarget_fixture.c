#include "r_avatar.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture_s {
	md5livejoint_t joints[MAX_MD5_JOINTS];
	md5liveinfo_t live;
} fixture_t;

static void ident(float m[12], float x, float y, float z)
{
	memset(m, 0, 12 * sizeof(*m));
	m[0] = m[5] = m[10] = 1.0f;
	m[3] = x; m[7] = y; m[11] = z;
}

static void rotation_x(float m[12])
{
	ident(m, 0, 0, 0);
	m[5] = 0; m[6] = -1;
	m[9] = 1; m[10] = 0;
}

static void rotation_z(float m[12])
{
	ident(m, 0, 0, 0);
	m[0] = 0; m[1] = -1;
	m[4] = 1; m[5] = 0;
}

static void multiply(const float a[12], const float b[12], float out[12])
{
	int r, c; float t[12];
	for (r = 0; r < 3; ++r) {
		for (c = 0; c < 3; ++c)
			t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c];
		t[r * 4 + 3] = a[r * 4] * b[3] + a[r * 4 + 1] * b[7] + a[r * 4 + 2] * b[11] + a[r * 4 + 3];
	}
	memcpy(out, t, sizeof(t));
}

static void inverse(const float in[12], float out[12])
{
	int r, c;
	for (r = 0; r < 3; ++r) for (c = 0; c < 3; ++c) out[r * 4 + c] = in[c * 4 + r];
	for (r = 0; r < 3; ++r) out[r * 4 + 3] = -(out[r * 4] * in[3] + out[r * 4 + 1] * in[7] + out[r * 4 + 2] * in[11]);
}

static void point(const float m[12], const float in[3], float out[3])
{
	out[0] = m[0] * in[0] + m[1] * in[1] + m[2] * in[2] + m[3];
	out[1] = m[4] * in[0] + m[5] * in[1] + m[6] * in[2] + m[7];
	out[2] = m[8] * in[0] + m[9] * in[1] + m[10] * in[2] + m[11];
}

static void origin(float m[12], float x, float y, float z)
{
	m[3] = x; m[7] = y; m[11] = z;
}

static void add(fixture_t *f, int *count, const char *name, int parent, float x, float y, float z)
{
	int n = (*count)++;
	strncpy(f->joints[n].name, name, sizeof(f->joints[n].name) - 1);
	f->joints[n].parent = parent;
	if (parent < 0) ident(f->joints[n].bind, x, y, z);
	else { float local[12]; ident(local, x, y, z); multiply(f->joints[parent].bind, local, f->joints[n].bind); }
}

static void ranger(fixture_t *f, float scale)
{
	static const char *names[] = { "Hip", "Spine1", "Spine2", "Neck", "Head", "Shoulder_L", "UpperArm_L", "LowerArm_L", "Hand_L", "Shoulder_R", "UpperArm_R", "LowerArm_R", "Hand_R", "UpperLeg_L", "LowerLeg_L", "Foot_L", "UpperLeg_R", "LowerLeg_R", "Foot_R" };
	int i, n = 0, parent[] = {-1,0,1,2,3,2,5,6,7,2,9,10,11,0,13,14,0,16,17};
	memset(f, 0, sizeof(*f));
	for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); ++i)
		add(f, &n, names[i], parent[i], (i == 5 || i == 6 || i == 7 || i == 8 || i == 13 || i == 14 || i == 15) ? -scale : scale, 0, scale);
	f->live.joints = f->joints; f->live.numbones = n;
}

static void named_profile(fixture_t *f, const r_avatar_profile_t *profile)
{
	int semantic, n = 0, index[MD5_VRIK_JOINT_COUNT], parent;
	memset(f, 0, sizeof(*f));
	for (semantic = 0; semantic < MD5_VRIK_JOINT_COUNT; ++semantic) {
		int i; index[semantic] = -1;
		if (!profile->joint[semantic].name) continue;
		for (i = 0; i < n; ++i) if (!strcmp(f->joints[i].name, profile->joint[semantic].name)) { index[semantic] = i; break; }
		if (index[semantic] < 0) { index[semantic] = n; add(f, &n, profile->joint[semantic].name, -1, 0, 0, 0); }
	}
	/* Rebuild bind globals in a safe semantic tree.  Virtual aliases retain
	 * their joint's first real parent; these fixtures exercise no raw name
	 * assumptions beyond the profile data. */
	for (semantic = 0; semantic < MD5_VRIK_JOINT_COUNT; ++semantic) if (index[semantic] >= 0) {
		parent = -1;
		if (semantic == MD5_VRIK_SPINE1 || semantic == MD5_VRIK_UPPERLEG_L || semantic == MD5_VRIK_UPPERLEG_R) parent = index[MD5_VRIK_HIP];
		else if (semantic == MD5_VRIK_SPINE2) parent = index[MD5_VRIK_SPINE1];
		else if (semantic == MD5_VRIK_NECK) parent = index[MD5_VRIK_SPINE2];
		else if (semantic == MD5_VRIK_HEAD) parent = index[MD5_VRIK_NECK] >= 0 ? index[MD5_VRIK_NECK] : index[MD5_VRIK_SPINE2];
		else if (semantic == MD5_VRIK_SHOULDER_L || semantic == MD5_VRIK_SHOULDER_R) parent = index[MD5_VRIK_SPINE2];
		else if (semantic == MD5_VRIK_UPPERARM_L) parent = index[MD5_VRIK_SHOULDER_L];
		else if (semantic == MD5_VRIK_UPPERARM_R) parent = index[MD5_VRIK_SHOULDER_R];
		else if (semantic == MD5_VRIK_LOWERARM_L) parent = index[MD5_VRIK_UPPERARM_L];
		else if (semantic == MD5_VRIK_LOWERARM_R) parent = index[MD5_VRIK_UPPERARM_R];
		else if (semantic == MD5_VRIK_HAND_L) parent = index[MD5_VRIK_LOWERARM_L];
		else if (semantic == MD5_VRIK_HAND_R) parent = index[MD5_VRIK_LOWERARM_R];
		else if (semantic == MD5_VRIK_LOWERLEG_L) parent = index[MD5_VRIK_UPPERLEG_L];
		else if (semantic == MD5_VRIK_LOWERLEG_R) parent = index[MD5_VRIK_UPPERLEG_R];
		else if (semantic == MD5_VRIK_FOOT_L) parent = index[MD5_VRIK_LOWERLEG_L];
		else if (semantic == MD5_VRIK_FOOT_R) parent = index[MD5_VRIK_LOWERLEG_R];
		if (parent >= 0 && parent != index[semantic] && parent < index[semantic]) {
			float local[12], lateral = 0.0f;
			if ((semantic >= MD5_VRIK_SHOULDER_L && semantic <= MD5_VRIK_HAND_L) ||
				(semantic >= MD5_VRIK_UPPERLEG_L && semantic <= MD5_VRIK_FOOT_L)) lateral = -1.0f;
			else if ((semantic >= MD5_VRIK_SHOULDER_R && semantic <= MD5_VRIK_HAND_R) ||
				(semantic >= MD5_VRIK_UPPERLEG_R && semantic <= MD5_VRIK_FOOT_R)) lateral = 1.0f;
			f->joints[index[semantic]].parent = parent; ident(local, lateral, 0, 1); multiply(f->joints[parent].bind, local, f->joints[index[semantic]].bind);
		}
	}
	f->live.joints = f->joints; f->live.numbones = n;
}

static void test_identity_and_locals(void)
{
	fixture_t source, target; r_avatar_rig_t sr, tr; float solved[MAX_MD5_JOINTS * 12], output[MAX_MD5_JOINTS * 12]; int i;
	ranger(&source, 1); ranger(&target, 2);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr));
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_SOLDIER), &target.live, &tr));
	for (i = 0; i < source.live.numbones; ++i) memcpy(solved + i * 12, source.joints[i].bind, 12 * sizeof(float));
	assert(R_AvatarRetargetPalette(&sr, &tr, solved, output));
	for (i = 0; i < target.live.numbones; ++i) {
		float invp[12], local[12], expected[12];
		if (target.joints[i].parent < 0) {
			memcpy(local, output + i * 12, sizeof(local));
			memcpy(expected, target.joints[i].bind, sizeof(expected));
		} else {
			inverse(output + target.joints[i].parent * 12, invp);
			multiply(invp, output + i * 12, local);
			inverse(target.joints[target.joints[i].parent].bind, invp);
			multiply(invp, target.joints[i].bind, expected);
		}
		assert(fabsf(local[3] - expected[3]) < 0.001f &&
			fabsf(local[7] - expected[7]) < 0.001f &&
			fabsf(local[11] - expected[11]) < 0.001f);
	}
	solved[0] = -1.0f;
	assert(!R_AvatarRetargetPalette(&sr, &tr, solved, output));
}

static void test_rotation_and_basis(void)
{
	fixture_t source, target; r_avatar_rig_t sr, tr; float solved[MAX_MD5_JOINTS * 12], output[MAX_MD5_JOINTS * 12], a[12], b[12], product[12], p[3] = {2,3,4}, q[3], back[3]; int i, upper;
	ranger(&source, 1); ranger(&target, 1);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr)); assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &target.live, &tr));
	for (i = 0; i < source.live.numbones; ++i) memcpy(solved + i * 12, source.joints[i].bind, 12 * sizeof(float));
	upper = sr.joint[MD5_VRIK_UPPERARM_L]; solved[upper * 12] = 0; solved[upper * 12 + 1] = -1; solved[upper * 12 + 4] = 1; solved[upper * 12 + 5] = 0; solved[upper * 12 + 10] = 1;
	assert(R_AvatarRetargetPalette(&sr, &tr, solved, output)); assert(fabsf(output[upper * 12 + 1]) > .9f);
	assert(R_AvatarCanonicalToTargetBasis(&tr, a)); assert(R_AvatarTargetToCanonicalBasis(&tr, b)); multiply(a, b, product); assert(fabsf(product[0] - 1) < .01f && fabsf(product[5] - 1) < .01f && fabsf(product[10] - 1) < .01f);
	assert(R_AvatarTargetToCanonicalPresentation(&sr, &tr, a)); assert(R_AvatarCanonicalToTargetPresentation(&sr, &tr, b)); point(a, p, q); point(b, q, back); assert(fabsf(back[0] - p[0]) < .01f && fabsf(back[1] - p[1]) < .01f && fabsf(back[2] - p[2]) < .01f);
	assert(R_AvatarQuantizedDisplayScale(R_AvatarProfileForId(PLAYER_AVATAR_DOG)) > 1.3f);
}

static void test_dynamic_presentation_basis(void)
{
	fixture_t source, target; r_avatar_rig_t sr, tr; r_avatar_presentation_context_t context; float presentation[12], hip[3], head[3], mappedhip[3], mappedhead[3];
	ranger(&source, 1); ranger(&target, 1);
	/* Deliberately make the canonical bind face +Y and target bind face +X.
	 * The target left span is +Y; both bases remain proper handed frames. */
	origin(source.joints[0].bind, 0, 0, 0); origin(source.joints[4].bind, 0, 10, 0);
	origin(source.joints[5].bind, 1, 5, 0); origin(source.joints[9].bind, -1, 5, 0);
	origin(target.joints[0].bind, 0, 0, 0); origin(target.joints[4].bind, 10, 0, 0);
	origin(target.joints[5].bind, 5, 1, 0); origin(target.joints[9].bind, 5, -1, 0);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr));
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &target.live, &tr));
	assert(R_AvatarBuildPresentationContext(&sr, &tr, &context));
	/* Stored semantic vertical/facing axes come from the authored bind body
	 * frame, rather than raw source +X/+Z. */
	assert(fabsf(context.source_semantic_vertical[0]) < .01f &&
		context.source_semantic_vertical[1] > .99f &&
		fabsf(context.source_semantic_vertical[2]) < .01f);
	assert(fabsf(context.source_semantic_facing[0]) < .01f &&
		fabsf(context.source_semantic_facing[1]) < .01f &&
		context.source_semantic_facing[2] < -.99f);
	assert(R_AvatarTargetToCanonicalPresentation(&sr, &tr, presentation));
	origin(source.joints[0].bind, 0, 0, 0);
	hip[0] = target.joints[0].bind[3]; hip[1] = target.joints[0].bind[7]; hip[2] = target.joints[0].bind[11];
	head[0] = target.joints[4].bind[3]; head[1] = target.joints[4].bind[7]; head[2] = target.joints[4].bind[11];
	point(presentation, hip, mappedhip); point(presentation, head, mappedhead);
	assert(fabsf(mappedhip[0]) < .01f && fabsf(mappedhip[1]) < .01f && fabsf(mappedhip[2]) < .01f);
	assert(fabsf(mappedhead[0]) < .01f && mappedhead[1] > 9.9f && fabsf(mappedhead[2]) < .01f);
}

static void test_profile_basis_policies(void)
{
	fixture_t humanoid, animal;
	r_avatar_rig_t humanoidrig, animalrig;
	const r_avatar_profile_t *dog, *fiend, *shambler, *vore;
	float basis[12], headfromhip[3], forward[3], determinant;

	/* This is the legacy humanoid construction: head-forward then shoulder-left.
	 * Keep these exact axes while adding the explicitly opted-in animal path. */
	ranger(&humanoid, 1);
	origin(humanoid.joints[0].bind, 0, 0, 0);
	origin(humanoid.joints[4].bind, 0, 10, 0);
	origin(humanoid.joints[5].bind, 1, 5, 0);
	origin(humanoid.joints[9].bind, -1, 5, 0);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER),
		&humanoid.live, &humanoidrig));
	assert(R_AvatarCanonicalToTargetBasis(&humanoidrig, basis));
	assert(fabsf(basis[0]) < .001f && fabsf(basis[4] - 1.0f) < .001f &&
		fabsf(basis[8]) < .001f);
	assert(fabsf(basis[1] - 1.0f) < .001f && fabsf(basis[5]) < .001f &&
		fabsf(basis[9]) < .001f);
	assert(fabsf(basis[2]) < .001f && fabsf(basis[6]) < .001f &&
		fabsf(basis[10] + 1.0f) < .001f);

	dog = R_AvatarProfileForId(PLAYER_AVATAR_DOG);
	assert(dog->basis_policy == R_AVATAR_BASIS_FEET_UP_HEAD_FORWARD);
	assert(!strcmp(dog->joint[MD5_VRIK_FOOT_L].name, "RearFoot_L"));
	assert(!strcmp(dog->joint[MD5_VRIK_FOOT_R].name, "RearFoot_R"));
	assert(!strcmp(dog->contact_root[0], "RearFoot_L"));
	assert(!strcmp(dog->contact_root[1], "RearFoot_R") && !dog->contact_root[2]);
	assert(dog->desktop_refine && dog->arm_pole_outward == 1.0f &&
		dog->arm_pole_back == 0.35f && !dog->mirror_outer_leg_poles &&
		dog->posture_policy == R_AVATAR_POSTURE_UPRIGHT && dog->posture_degrees == 60.0f &&
		dog->head_forward_axis[1] == 1.0f && dog->preserve_hip_rotation &&
		dog->actual_path_ik);
	assert((dog->joint[MD5_VRIK_SHOULDER_L].flags & R_AVATAR_MAP_VIRTUAL) &&
		!strcmp(dog->joint[MD5_VRIK_SHOULDER_L].name, "FrontHigh_L") &&
		!strcmp(dog->joint[MD5_VRIK_UPPERARM_L].name, "FrontHigh_L") &&
		!strcmp(dog->joint[MD5_VRIK_LOWERARM_L].name, "FrontMid_L") &&
		!strcmp(dog->joint[MD5_VRIK_HAND_L].name, "FrontFoot_L"));
	fiend = R_AvatarProfileForId(PLAYER_AVATAR_FIEND);
	assert(fiend->basis_policy == R_AVATAR_BASIS_FEET_UP_HEAD_FORWARD);
	assert(!strcmp(fiend->contact_root[0], "hoof_L") &&
		!strcmp(fiend->contact_root[1], "hoof_R") && fiend->desktop_refine &&
		fiend->posture_policy == R_AVATAR_POSTURE_UPRIGHT && fiend->posture_degrees == 35.0f &&
		fiend->head_forward_axis[1] == 1.0f && fiend->preserve_hip_rotation &&
		fiend->actual_path_ik && !strcmp(fiend->joint[MD5_VRIK_FOOT_L].name, "hoof_L") &&
		!strcmp(fiend->joint[MD5_VRIK_FOOT_R].name, "hoof_R"));
	shambler = R_AvatarProfileForId(PLAYER_AVATAR_SHAMBLER);
	assert(!shambler->desktop_refine && shambler->arm_pole_outward == 0.136f &&
		shambler->arm_pole_back == 0.223f && shambler->arm_pole_up == 0.965f &&
		shambler->posture_policy == R_AVATAR_POSTURE_AUTHORED &&
		!shambler->preserve_hip_rotation &&
		shambler->desktop_upperbody_bind_root == MD5_VRIK_SPINE2 &&
		fabsf (shambler->desktop_weapon_support[0] + 4.55128f) < .00001f &&
		fabsf (shambler->desktop_weapon_support[1] - 11.20621f) < .00001f &&
		fabsf (shambler->desktop_weapon_support[2] + 17.71073f) < .00001f);
	vore = R_AvatarProfileForId(PLAYER_AVATAR_VORE);
	assert(vore->mirror_outer_leg_poles && !vore->preserve_hip_rotation &&
		vore->posture_policy == R_AVATAR_POSTURE_AUTHORED);

	/* A synthetic quadruped makes its floor-to-hip direction the first column
	 * and flips its left/forward columns together so the head stays forward. */
	named_profile(&animal, dog);
	assert(R_AvatarResolveRig(dog, &animal.live, &animalrig));
	origin(animal.joints[animalrig.joint[MD5_VRIK_HIP]].bind, 0, 0, 2);
	origin(animal.joints[animalrig.joint[MD5_VRIK_HEAD]].bind, 10, 0, 4);
	origin(animal.joints[animalrig.joint[MD5_VRIK_SHOULDER_L]].bind, 4, 2, 3);
	origin(animal.joints[animalrig.joint[MD5_VRIK_SHOULDER_R]].bind, 4, -2, 3);
	origin(animal.joints[animalrig.joint[MD5_VRIK_FOOT_L]].bind, -2, 2, 0);
	origin(animal.joints[animalrig.joint[MD5_VRIK_FOOT_R]].bind, -2, -2, 0);
	assert(R_AvatarCanonicalToTargetBasis(&animalrig, basis));
	assert(basis[0] > .7f && basis[8] > .7f);
	assert(basis[5] < -.99f && basis[2] > .7f && basis[10] < -.7f);
	headfromhip[0] = 10; headfromhip[1] = 0; headfromhip[2] = 2;
	forward[0] = basis[2]; forward[1] = basis[6]; forward[2] = basis[10];
	assert(forward[0] * headfromhip[0] + forward[1] * headfromhip[1] +
		forward[2] * headfromhip[2] > 0.0f);
	determinant = basis[0] * (basis[5] * basis[10] - basis[6] * basis[9]) -
		basis[1] * (basis[4] * basis[10] - basis[6] * basis[8]) +
		basis[2] * (basis[4] * basis[9] - basis[5] * basis[8]);
	assert(fabsf(determinant - 1.0f) < .001f);
}

static void test_ancestor_translation_applied_once(void)
{
	fixture_t source, target;
	r_avatar_rig_t sr, tr;
	float solved[MAX_MD5_JOINTS * 12], output[MAX_MD5_JOINTS * 12];
	float parentmotion[12], invparent[12], childbindlocal[12], unmappedlocal[12];
	float parentdelta[3], childdelta[3], local[12];
	int i, parent, child, targetparentindex, targetchild, unmapped;

	ranger(&source, 1); ranger(&target, 1);
	/* An unmapped descendant must retain its authored bind-local under the
	 * solved parent.  Its global motion comes from that parent exactly once. */
	unmapped = target.live.numbones++;
	strncpy(target.joints[unmapped].name, "UnmappedChild",
		sizeof(target.joints[unmapped].name) - 1);
	target.joints[unmapped].parent = 2; /* target Spine2 */
	rotation_z(unmappedlocal);
	origin(unmappedlocal, 7, -3, 2);
	multiply(target.joints[target.joints[unmapped].parent].bind, unmappedlocal,
		target.joints[unmapped].bind);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr));
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &target.live, &tr));
	for (i = 0; i < source.live.numbones; ++i)
		memcpy(solved + i * 12, source.joints[i].bind, 12 * sizeof(float));
	parent = sr.joint[MD5_VRIK_SPINE1];
	child = sr.joint[MD5_VRIK_SPINE2];
	ident(parentmotion, 2, -3, 4);
	multiply(parentmotion, source.joints[parent].bind, solved + parent * 12);
	inverse(source.joints[parent].bind, invparent);
	multiply(invparent, source.joints[child].bind, childbindlocal);
	multiply(solved + parent * 12, childbindlocal, solved + child * 12);
	assert(R_AvatarRetargetPalette(&sr, &tr, solved, output));
	targetparentindex = tr.joint[MD5_VRIK_SPINE1];
	targetchild = tr.joint[MD5_VRIK_SPINE2];
	for (i = 0; i < 3; ++i) {
		int origincomponent = i * 4 + 3;
		parentdelta[i] = output[targetparentindex * 12 + origincomponent] -
			target.joints[targetparentindex].bind[origincomponent];
		childdelta[i] = output[targetchild * 12 + origincomponent] -
			target.joints[targetchild].bind[origincomponent];
		assert(fabsf(parentdelta[i] - parentmotion[origincomponent]) < 0.001f);
		assert(fabsf(childdelta[i] - parentdelta[i]) < 0.001f);
	}
	inverse(output + targetchild * 12, invparent);
	multiply(invparent, output + unmapped * 12, local);
	for (i = 0; i < 12; ++i)
		assert(fabsf(local[i] - unmappedlocal[i]) < 0.001f);
}

static void test_rejection_and_monsters(void)
{
	fixture_t bad, dog, vore, enforcer; r_avatar_rig_t rig;
	const r_avatar_profile_t *p;
	ranger(&bad, 1); bad.joints[8].parent = 0; assert(!R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &bad.live, &rig));
	ranger(&bad, 1); bad.joints[0].bind[10] = -1; assert(!R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &bad.live, &rig));
	named_profile(&dog, R_AvatarProfileForId(PLAYER_AVATAR_DOG)); assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_DOG), &dog.live, &rig));
	assert(R_AvatarCanonicalToTargetBasis(&rig, dog.joints[0].bind));
	named_profile(&vore, R_AvatarProfileForId(PLAYER_AVATAR_VORE)); assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_VORE), &vore.live, &rig));
	named_profile(&enforcer, R_AvatarProfileForId(PLAYER_AVATAR_ENFORCER)); assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_ENFORCER), &enforcer.live, &rig));
	p = R_AvatarProfileForId(PLAYER_AVATAR_ENFORCER);
	assert(!strcmp(p->joint[MD5_VRIK_UPPERARM_R].name, "upper_arm_R"));
	p = R_AvatarProfileForId(PLAYER_AVATAR_VORE);
	assert(!strcmp(p->joint[MD5_VRIK_UPPERLEG_L].name, "leg_1_L"));
	assert(!strcmp(p->joint[MD5_VRIK_HAND_R].name, "hand_R"));
}

static void test_all_profile_palettes_are_bounded(void)
{
	fixture_t source, target;
	r_avatar_rig_t sr, tr;
	float solved[MAX_MD5_JOINTS * 12], output[MAX_MD5_JOINTS * 12];
	int avatar, joint, component;

	ranger(&source, 1);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr));
	for (joint = 0; joint < source.live.numbones; ++joint)
		memcpy(solved + joint * 12, source.joints[joint].bind, 12 * sizeof(float));
	for (avatar = PLAYER_AVATAR_RANGER; avatar < PLAYER_AVATAR_COUNT; ++avatar) {
		named_profile(&target, R_AvatarProfileForId(avatar));
		assert(R_AvatarResolveRig(R_AvatarProfileForId(avatar), &target.live, &tr));
		assert(R_AvatarRetargetPalette(&sr, &tr, solved, output));
		for (joint = 0; joint < target.live.numbones; ++joint)
			for (component = 0; component < 12; ++component)
				assert(isfinite(output[joint * 12 + component]) &&
					fabsf(output[joint * 12 + component]) < 10000.0f);
	}
}

static void test_absolute_global_transport(void)
{
	fixture_t source, target;
	r_avatar_rig_t sr, tr;
	r_avatar_presentation_context_t context;
	float solved[MAX_MD5_JOINTS * 12], output[MAX_MD5_JOINTS * 12];
	float motion[12], invbind[12], delta[12], expected[12], inverse_r[12], mapped[12];
	int i, joint;
	ranger(&source, 1); ranger(&target, 2);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr));
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &target.live, &tr));
	assert(R_AvatarBuildPresentationContext(&sr, &tr, &context));
	for (i = 0; i < source.live.numbones; ++i) memcpy(solved + i * 12, source.joints[i].bind, 12 * sizeof(float));
	joint = sr.joint[MD5_VRIK_HAND_R];
	rotation_x(motion); motion[3] = 3; motion[7] = -2; motion[11] = 1;
	multiply(motion, source.joints[joint].bind, solved + joint * 12);
	assert(R_AvatarRetargetPaletteWithContext(&sr, &tr, &context, solved, output));
	inverse(source.joints[joint].bind, invbind); multiply(solved + joint * 12, invbind, delta);
	delta[3] = delta[7] = delta[11] = 0; inverse(context.rotation, inverse_r);
	multiply(delta, context.rotation, mapped); multiply(inverse_r, mapped, expected);
	multiply(expected, target.joints[tr.joint[MD5_VRIK_HAND_R]].bind, expected);
	expected[3] = target.joints[tr.joint[MD5_VRIK_HAND_R]].bind[3] + context.inverse[0]*(solved[joint*12+3]-source.joints[joint].bind[3]) + context.inverse[1]*(solved[joint*12+7]-source.joints[joint].bind[7]) + context.inverse[2]*(solved[joint*12+11]-source.joints[joint].bind[11]);
	expected[7] = target.joints[tr.joint[MD5_VRIK_HAND_R]].bind[7] + context.inverse[4]*(solved[joint*12+3]-source.joints[joint].bind[3]) + context.inverse[5]*(solved[joint*12+7]-source.joints[joint].bind[7]) + context.inverse[6]*(solved[joint*12+11]-source.joints[joint].bind[11]);
	expected[11] = target.joints[tr.joint[MD5_VRIK_HAND_R]].bind[11] + context.inverse[8]*(solved[joint*12+3]-source.joints[joint].bind[3]) + context.inverse[9]*(solved[joint*12+7]-source.joints[joint].bind[7]) + context.inverse[10]*(solved[joint*12+11]-source.joints[joint].bind[11]);
	for (i = 0; i < 12; ++i) assert(fabsf(output[tr.joint[MD5_VRIK_HAND_R]*12+i] - expected[i]) < .001f);
}

static void test_preserve_hip_rotation(void)
{
	fixture_t source, target;
	r_avatar_rig_t sr, tr;
	r_avatar_presentation_context_t context;
	float solved[MAX_MD5_JOINTS * 12], output[MAX_MD5_JOINTS * 12];
	float rotate[12], inversehip[12], local[12], tailbindlocal[12];
	int hip, tail, i;

	ranger(&source, 1); named_profile(&target, R_AvatarProfileForId(PLAYER_AVATAR_DOG));
	tail = target.live.numbones++;
	strncpy(target.joints[tail].name, "Tail1", sizeof(target.joints[tail].name) - 1);
	hip = 0; /* Dog's named fixture creates Hip first. */
	target.joints[tail].parent = hip;
	ident(tailbindlocal, 3, -2, 1);
	multiply(target.joints[hip].bind, tailbindlocal, target.joints[tail].bind);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr));
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_DOG), &target.live, &tr));
	assert(R_AvatarBuildPresentationContext(&sr, &tr, &context));
	for (i = 0; i < source.live.numbones; ++i)
		memcpy(solved + i * 12, source.joints[i].bind, 12 * sizeof(float));
	hip = sr.joint[MD5_VRIK_HIP]; rotation_z(rotate); rotate[3] = 4; rotate[7] = -3; rotate[11] = 2;
	multiply(rotate, source.joints[hip].bind, solved + hip * 12);
	assert(R_AvatarRetargetPaletteWithContext(&sr, &tr, &context, solved, output));
	hip = tr.joint[MD5_VRIK_HIP];
	for (i = 0; i < 12; ++i) if ((i % 4) != 3)
		assert(fabsf(output[hip * 12 + i] - target.joints[hip].bind[i]) < .001f);
	assert(fabsf(output[hip * 12 + 3] - target.joints[hip].bind[3]) > .01f ||
		fabsf(output[hip * 12 + 7] - target.joints[hip].bind[7]) > .01f ||
		fabsf(output[hip * 12 + 11] - target.joints[hip].bind[11]) > .01f);
	inverse(output + hip * 12, inversehip); multiply(inversehip, output + tail * 12, local);
	for (i = 0; i < 12; ++i)
		assert(fabsf(local[i] - tailbindlocal[i]) < .001f);
}

static void test_nonunit_presentation_roundtrips(void)
{
	fixture_t source, target;
	r_avatar_rig_t sr, tr;
	r_avatar_presentation_context_t context;
	vec3_t point_in = {3, -4, 5}, mapped, roundtrip, inplace;
	int avatar;
	ranger(&source, 1);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr));
	for (avatar = PLAYER_AVATAR_DOG; avatar <= PLAYER_AVATAR_SHAMBLER; avatar += PLAYER_AVATAR_SHAMBLER - PLAYER_AVATAR_DOG) {
		named_profile(&target, R_AvatarProfileForId(avatar));
		assert(R_AvatarResolveRig(R_AvatarProfileForId(avatar), &target.live, &tr));
		assert(R_AvatarBuildPresentationContext(&sr, &tr, &context));
		assert(fabsf(context.scale - 1.0f) > .01f);
		R_AvatarPresentationPoint(&context, point_in, mapped);
		R_AvatarPresentationInversePoint(&context, mapped, roundtrip);
		assert(fabsf(roundtrip[0]-point_in[0]) < .01f && fabsf(roundtrip[1]-point_in[1]) < .01f && fabsf(roundtrip[2]-point_in[2]) < .01f);
		memcpy(inplace, point_in, sizeof(inplace));
		R_AvatarPresentationPoint(&context, inplace, inplace);
		assert(fabsf(inplace[0]-mapped[0]) < .01f && fabsf(inplace[1]-mapped[1]) < .01f && fabsf(inplace[2]-mapped[2]) < .01f);
		R_AvatarPresentationInversePoint(&context, inplace, inplace);
		assert(fabsf(inplace[0]-point_in[0]) < .01f && fabsf(inplace[1]-point_in[1]) < .01f && fabsf(inplace[2]-point_in[2]) < .01f);
		assert(fabsf(context.rotation[0]*(context.rotation[5]*context.rotation[10]-context.rotation[6]*context.rotation[9]) - context.rotation[1]*(context.rotation[4]*context.rotation[10]-context.rotation[6]*context.rotation[8]) + context.rotation[2]*(context.rotation[4]*context.rotation[9]-context.rotation[5]*context.rotation[8])-1.0f) < .03f);
	}
}

int main(void)
{
	test_identity_and_locals(); test_rotation_and_basis(); test_dynamic_presentation_basis(); test_profile_basis_policies(); test_ancestor_translation_applied_once(); test_rejection_and_monsters(); test_all_profile_palettes_are_bounded(); test_absolute_global_transport(); test_preserve_hip_rotation(); test_nonunit_presentation_roundtrips();
	puts("avatar retarget fixture: ok"); return 0;
}
