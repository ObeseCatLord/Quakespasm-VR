# Native capture fault and retry qualification plan

2026-10-02. Existing F08 boundary: direct discontinuity owner already passes,
but actual capture-availability/read API failures through public Voice_Frame and
native retry timing remain distinct. All product implementation is present;
qualify this boundary with the existing initialized SDL3 dummy/Opus/native graph.
No production edit, physical recording, GPU/runtime launch or additional codec.

Main verified Voice_ProcessCapture availability/read negative paths invoke the
existing discontinuity owner, which clears sender/VAD/level, closes capture and
sets retry deadline=realtime+10. Voice_RefreshCapture(false) preserves permission
and route, refuses premature retry, and failed native open schedules another10s.
Existing PTT state intentionally survives device failure (not consent revocation).

Wrap only the three SDL3 API boundaries in the existing native fixture. Unarmed
calls forward to real SDL; armed exact capture stream gets one negative available
or read return. Native dummy open failure is injected once at the actual open
boundary; API call counters prove no early retry and attempt at exact deadline.
Use controlled game realtime, not wall timing/performance claims. Prime sending,
VAD/preroll and meter using real generated PCM/public physical-key PTT handling,
never directly assign sending=true. After failure observe native END-only queue,
retained permission/PTT, retired capture/level/VAD. Clear only test-owned pending
END through existing queue owner, then positive reopen/new PCM production.

Luna owns new voice_device_fault_native_fixture.h and minimal include/flag/call in
voice_pcm_native_fixture.c plus existing build helper's three wrap arguments.
Main reviews predicates and uses the existing combined fixture runner with dummy,
private preferences and12prior cases plus this one. No copied DSP/route state
machine, synthetic runtime/device provider, global clock wrapper or512-case suite.
Software injection qualifies native failure handling and prepared retry clocks;
actual OS/device removal, permission-dialog behavior and hardware remain user tests.
