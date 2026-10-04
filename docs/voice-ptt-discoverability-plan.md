# Push-to-talk discoverability

2026-10-03. This narrow menu change reuses the existing active-profile
`Voice_SetMode` action. The Voice Chat page presents that mode as a
Push-to-talk checkbox: checked selects push-to-talk and unchecked preserves
voice activity detection (VAD). It does not change either profile's stored
default, microphone opt-out, capture behavior, or input mapping.

The page names the navigation action **Bind push-to-talk**. Opening it uses
the established Keys menu, retains Voice Chat as Back's parent, and selects
the existing `+voicerecord` entry after the list has been populated. The
selection computes `first_key` with the normal one-page visibility bounds.

`+voicerecord` remains a single built-in Keys entry. It is placed before the
first `*` marker so a mod's replaceable gameplay section cannot suppress its
label; custom lists still cannot duplicate the protected command. No voice
protocol, capture, profile, audio, VR input, or default-binding code changes
are part of this slice.

Source acceptance: inspect the Voice page's mode draw/action and Controls
route; inspect the populated Keys list to confirm selection and a valid page
offset; inspect the default binding marker boundaries to confirm the row is
outside the replaceable gameplay region. Runtime builds and tests are deferred
for the main integration pass.
