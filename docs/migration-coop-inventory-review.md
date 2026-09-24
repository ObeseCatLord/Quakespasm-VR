# Co-op inventory owner review

The `2.0` port keeps vkQuake's QuakeC touch and physics owners. It brings the
inherited shared-pickup and typed dead-player inventory policies across at
those boundaries; it does not replace the save parser or the server physics
loop. The behavior reference is the pinned product commit `1327f795`.

An Astra xhigh senior review of the combined port found no P0 defect and four
integration gaps. The dispositions are:

| Finding | Disposition |
|---|---|
| Solid-door `SV_Impact` callbacks bypassed shared key consumption tracking. | Wrap both existing impact callbacks with the same begin/end touch policy used by trigger touches, retaining the donor edict lifetime checks. |
| Player death could clear or duplicate canonical keys after the pickup transaction. | Observe alive-to-dead transitions around the existing physics frame, including deaths from later entities and think-only modes; reconcile once. No new respawn state machine. |
| A newly shared weapon could be lost from a dead-player save projection if QuakeC killed the receiver before the next post-think cache refresh. | Refresh typed inventory immediately before each client pre-think as well as after accepted post-think work. |
| A fresh late joiner had no call into the remembered progression owner. | Apply shared progression after `PutClientInServer` initializes a new player. Saved-client handling belongs to the separate save adapter. |

The review found no demonstrated defect in accepted pickup delta checks,
duplicate-weapon gating, typed-field validation, detached save projection,
exact zero-ammo restoration, or slot/map resets. These are static findings and
build checks, not gameplay qualification. The inherited inventory schemas have
different policies; no demonstrated mismatch justified merging them into a new
generic state owner.

The save/load adapter is still required to call the projection, restore, and
restored-client merge APIs. End-to-end proof remains: two clients and an empty
reserved slot; solid-door stock and counted key consumption; a no-op pickup;
weapon share followed by lethal pre-think; death from a later projectile; dead
player save/reload with zero ammo; reverse-order reconnect; and a late joiner
after a spent key. Classic co-op must retain its opt-out behavior.
