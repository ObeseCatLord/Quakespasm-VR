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
	fixture_t source, target; r_avatar_rig_t sr, tr; float presentation[12], hip[3], head[3], mappedhip[3], mappedhead[3];
	ranger(&source, 1); ranger(&target, 1);
	/* Deliberately make the canonical bind face +Y and target bind face +X.
	 * The target left span is +Y; both bases remain proper handed frames. */
	origin(source.joints[0].bind, 0, 0, 0); origin(source.joints[4].bind, 0, 10, 0);
	origin(source.joints[5].bind, 1, 5, 0); origin(source.joints[9].bind, -1, 5, 0);
	origin(target.joints[0].bind, 0, 0, 0); origin(target.joints[4].bind, 10, 0, 0);
	origin(target.joints[5].bind, 5, 1, 0); origin(target.joints[9].bind, 5, -1, 0);
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &source.live, &sr));
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER), &target.live, &tr));
	assert(R_AvatarTargetToCanonicalPresentation(&sr, &tr, presentation));
	origin(source.joints[0].bind, 0, 0, 0);
	hip[0] = target.joints[0].bind[3]; hip[1] = target.joints[0].bind[7]; hip[2] = target.joints[0].bind[11];
	head[0] = target.joints[4].bind[3]; head[1] = target.joints[4].bind[7]; head[2] = target.joints[4].bind[11];
	point(presentation, hip, mappedhip); point(presentation, head, mappedhead);
	assert(fabsf(mappedhip[0]) < .01f && fabsf(mappedhip[1]) < .01f && fabsf(mappedhip[2]) < .01f);
	assert(fabsf(mappedhead[0]) < .01f && mappedhead[1] > 9.9f && fabsf(mappedhead[2]) < .01f);
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

int main(void)
{
	test_identity_and_locals(); test_rotation_and_basis(); test_dynamic_presentation_basis(); test_rejection_and_monsters();
	puts("avatar retarget fixture: ok"); return 0;
}
