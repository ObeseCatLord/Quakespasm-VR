/*
 * Small, engine-independent policy primitives for co-op inventory sharing.
 *
 * QuakeC pickup handlers are free to rewrite their inventory fields however
 * they like.  The engine must only copy verified additive deltas to other
 * players: a no-op touch or a loss must never erase a receiver's inventory.
 */
#ifndef COOP_INVENTORY_POLICY_H
#define COOP_INVENTORY_POLICY_H

#include <stddef.h>

/* Header-only so all existing platform build lists continue to use the same
 * production policy without adding another link input. */
static inline int CoopInventoryPolicy_AddedBits (int before, int after)
{
	return after & ~before;
}

static inline int CoopInventoryPolicy_RemovedBits (int before, int after)
{
	return before & ~after;
}

static inline int CoopInventoryPolicy_HasFloatGain (const float *before,
	const float *after, size_t count)
{
	size_t i;

	if (!before || !after)
		return 0;
	for (i = 0; i < count; ++i)
		if (after[i] > before[i])
			return 1;
	return 0;
}

static inline int CoopInventoryPolicy_HasValueGain (int before_valid,
	float before, int after_valid, float after)
{
	return after_valid && after > (before_valid ? before : 0.0f);
}

static inline int CoopInventoryPolicy_HasAcceptedBaseGain (int before_items,
	int after_items, const float *before_ammo, const float *after_ammo,
	size_t ammo_count)
{
	return CoopInventoryPolicy_AddedBits(before_items, after_items) != 0 ||
		CoopInventoryPolicy_HasFloatGain(before_ammo, after_ammo,
			ammo_count);
}

static inline int CoopInventoryPolicy_ConfirmedDeclaredBits (int after,
	int declared, int allowed_mask)
{
	return after & declared & allowed_mask;
}

static inline int CoopInventoryPolicy_UnionBits (int current, int gained)
{
	return current | gained;
}

#endif /* COOP_INVENTORY_POLICY_H */
