# Left-stick recovery after an XR interruption

The user reports a brief black flash followed by lost left-stick movement while
other controls still work. Runtime focus/tracking cause remains unconfirmed.
The verified input defect is that movement rearming requires `VR_InputNeutral`,
which tests every button and the trigger as well as the stick. In addition,
`VR_InputHandAccepted` blocks movement until the button channel passes that
same test. Holding grip can therefore prevent a centered left stick from ever
rearming after one interrupted sample.

Keep the existing focus, pose, mapping, context, and controller-identity gates.
Separate continuous movement eligibility from discrete button rearming at the
existing hand-acceptance boundary. Use centered finite stick axes to rearm only
the left movement channel; held grip/trigger cannot block it. Button dispatch,
attack release, and snap-turn neutral gates retain their existing policy. A
still-deflected stick remains gated until it centers, avoiding unintended
movement on menu/context transitions. No new recovery state machine or timer.

Extend the actual input fixture: focus loss releases controls; after focus
returns, hold left grip while centering its stick, then deflect it. Verify normal
movement resumes while button rearming remains gated. Verify that a still-held
deflection remains blocked until centered and that recovered movement reaches
`VR_InputApplyPending`. These ASAN/UBSAN checks pass alongside the other gameplay
repairs. This fixes a demonstrated persistent gate, not the unknown source of
the black flash itself.
