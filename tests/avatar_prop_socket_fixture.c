/* The detached Ranger weapon follows the target hand without body scaling. */
#include "r_avatar.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void identity_at(float m[12], float x, float y, float z)
{
	memset(m, 0, 12 * sizeof(*m));
	m[0] = m[5] = m[10] = 1.0f;
	m[3] = x;
	m[7] = y;
	m[11] = z;
}

static void near(float actual, float expected)
{
	assert(fabsf(actual - expected) < 0.0001f);
}

int main(void)
{
	r_avatar_presentation_context_t context = {0};
	float source_hand[12], source_bind[12], target_hand[12];
	float target_bind[12], source_prop[12], result[12];

	identity_at(context.rotation, 0, 0, 0);
	identity_at(context.forward, 10, 20, 33); /* Includes +3 bind-floor Z. */
	context.forward[0] = context.forward[5] = context.forward[10] = 2.0f;
	identity_at(source_hand, 1, 0, 0);
	identity_at(source_bind, 0, 0, 0);
	identity_at(target_hand, 2, 0, 0);
	identity_at(target_bind, 0, 0, 0);
	identity_at(source_prop, 4, 0, 0);
	assert(R_AvatarBuildAttachedPropTransform(&context, source_hand,
		source_bind, target_hand, target_bind, source_prop, result));
	/* Canonical hand 14 plus unscaled hand-to-prop separation 3. */
	near(result[3], 17);
	near(result[7], 20);
	near(result[11], 33);
	near(result[0], 1);
	near(result[5], 1);
	near(result[10], 1);

	/* The target's authored quarter-turn bind grip must cancel at bind. */
	target_bind[0] = target_hand[0] = 0;
	target_bind[1] = target_hand[1] = -1;
	target_bind[4] = target_hand[4] = 1;
	target_bind[5] = target_hand[5] = 0;
	assert(R_AvatarBuildAttachedPropTransform(&context, source_hand,
		source_bind, target_hand, target_bind, source_prop, result));
	near(result[0], 1);
	near(result[1], 0);
	near(result[4], 0);
	near(result[5], 1);

	target_hand[3] = NAN;
	assert(!R_AvatarBuildAttachedPropTransform(&context, source_hand,
		source_bind, target_hand, target_bind, source_prop, result));
	puts("avatar prop socket fixture: ok");
	return 0;
}
