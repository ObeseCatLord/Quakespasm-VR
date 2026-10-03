# Senior acceptance review: two remaining native renderer boundaries

2026-10-02. Solo migration, frozen F05/F06 only, no new feature list or whole-goal
signoff. User asks reuse existing code; all production/source shipping inputs
remain unchanged. Local Astra/xhigh read-only verify-then-critique requested.
Main has completed bounded code review, actual GPU/component runs and independent
observations. Decide scope/independence of acceptance, not a renderer rewrite.

| Fact | Verified evidence |
| --- | --- |
| Workspace | writable2.0 only /home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0; main/reference read-only; user docs/migration-2.0.md dirty, never touch |
| Native culling host | existing parser-fixture host SHA13b7d270b66aea8812edf064ab29f505d18f38092a02e93cda8c008943597753; borrowed prior native graph/current SDK links, not new full shipping build |
| GPU/runtime | owned isolated Monado-null actual gameVulkan/X11mirror; service normally stopped after client0. No validation layer/hardware/gaze/benchmark, no driver/reset/globalsettings. |
| Culling artifacts | /home/obesecatlord/FastGames/qsvr-culling-all-n8x8d43p/output/culling.json, geometry.json,32PNGs; checker.json,main-culling-receipt.json,run.log/exit.json. First8phase split /home/obesecatlord/FastGames/qsvr-culling-source-z4c3p59_ also passes. Initial quoted-source error /home/obesecatlord/FastGames/qsvr-culling-native-ncf9b417 exits255 before inferior/GPUstart, retained. |
| Inputs | same actual parsed static aliases, native efrags/boxes/materials,12x1.5 planes; only controlled head/eye poses and per-eye upper/lowerhalfFoVs plus normal alpha0/.5, OIT1,water0 commands. No frustum/list/cull/return/result assignments. Runtimevalid/tracked unchanged. |
| Culling quality | all32phases normal engine/GDB0, no observererrors,240successful matching native snapshot/presentation receipts, >=8notify-expiredsettledframes, timefrozen,4xMSAA/SSAO1/OIT1/foveationoff. Exact same-state decoded repeats pass. |
| Culling main verification | independent native loaded-box homogeneous6plane tests from recordedcolumn-major clips agree with per-view eligibility. Native unionmodelcull false for either-eye, true foroutsideboth. Actual in-path return+aliasFinish observers require4acceptedtriangles forsurvivors,0forculled. Pure end-frame modelcull query is labeled separately and notcounted as in-path. Visible nativequadfull2x2source footprints withauthored red/bluecolorandB/E influence:1485 each of8positive cells.16total cells;8outsideviewcells projectedempty,4outsideboth have8actual rejectedalias calls. |
| Constructor artifacts | /home/obesecatlord/FastGames/qsvr-density-constructor-0l6_mc49/main-build-run-receipt.json +two executables/build/runlogs; strictGNU11SDL3WallWextraWerror compile/run0 for new densityfixture and unchanged acquisitionfixture defaultguardunset. Worker also runs0, initialbadassertion note only /tmp/FastGames-openxr-pass-framebuffer.mCmANF. |
| Constructor scope | actual r_passes.c R_Setup/R_CreateRenderPasses/R_CreateFrameBuffers/R_Destroy*, inherited render_acquirefixture via2lineoptionalspyguard. Native framegraph/handlearrays notsupplied as result. Controlled validstereo/MSAA4/AO1/caps; Vulkancreation/destruction spies uniqueadmittedhandles. Second densitypass andsecond framebuffer reject withnegativeerror+nonnullunownedout; actualreturnfalse/earlystop; onlysuccessfulhandlesowned/retiredexactlyonce; nullFBdestroyallowed; nativearrays/secondary-context.render_pass fieldszero. Warpseparateownerexplicitlyretired. Ordinarynodensityconstructors subsequentlypass; resetnativeinputs doesNOTprove actualGLcoordinatorfallback. That distinctowner is already qualified in docs/foveation-recovery-current-qualification-2.0.md. |

Source/reference: gl_rmain.c R_CullBox/R_CullModelForEntity/R_SetStereoFrustum;
read-only vkquake counterpart:143/166 and inheritedOpenVR:608/632; nativebounds
Mod_CalcAliasBounds. tests/openxr-stereo-culling.gdb reuses alpha-reference receipt/
normalcommand/notification owners through exactsource-seam adapter, customposes/
observations only. tests/check_stereo_culling.py reuses prior projectionhelpers;
standardstaticaliases, notpreparedavatar/submodel/world-onlyPVS exhaustive proof.

Constructor source r_passes.c:713–839/1572, gl_vidsdl.c:4321–4347 nativecoordinator.
New tests/openxr_pass_framebuffer_fault_fixture.c (362lines) includes existing
fixture, no productionchanges. Nativecontext render_pass input sentinels prime
cleanupchecks, not fabricated constructor outputs. Exact densityFB views checked
for eachscene-slot andborrowed-map index, notmerely recognizedimageclass.

Main lean: accept bounded ordinary static alias either-eye/excluded-both culling
plus targetedinspectednativeoutput; don'tclaim allavatar/BSP-onlybounds. Accept
native pass/FB rejection/partialcleanup premises underF06; don'tclaim realVulkan
failure orFB/META/provider compatibility orcoordinatorend-to-end fromspies.
Keep protectedfoveationGPUoutput andotherFownersopen. No missingproductionbug found.

Open decisions: Is cullingvisible/negative output proof sufficient for this
boundary, or is a smallindependent counterexample/control necessary? Is the
constructorordinaryrecovery label scoped correctly given inputreset andseparate
coordinator evidence? Any stalehandle/resourceownership assertion falselypassing?
Does adapter/fixture complexity justify simplification/deletion? These overlap;
merge/killunnecessarytestmachinery. Rejectanotherframegraph/runtime/software-gaze
provider, exhaustiveentitycases oruniversalGPUcertifier. No humanprioritydecision
required unless actualscope conflict emerges.

Read actual sources/receipts/images beforecritique. No edits/GPU/build/driver/
system changes/subagents/sessionlogsrawexport. Return<=900words prioritized
adopt/adapt/reject withfile/lineevidence, independently verified facts/unknowns.
Main spotchecks/disposes before finalacceptancecommit. No finalwholegoalsignoff.
