"""GDB fixture for the admitted, unmodified Bonk QC (not adapter storage).

Use Bonk_skins_verify.py. Unpaired socket records and item inputs are injected;
weapon selection, nested touch/inspection, sounds, parms and save/load execute
the original program or existing native entry points. No adapter helper API.
"""
import json
import os
from pathlib import Path

import gdb


def run(command):
    return gdb.execute(command, to_string=True)


def integer(expression):
    return int(gdb.parse_and_eval(expression))


def number(expression):
    return float(gdb.parse_and_eval(expression))


def string(expression):
    return gdb.parse_and_eval(expression).string()


def qc_function(name):
    run('set $skin_fn = ED_FindFunction(%s)' % json.dumps(name))
    assert integer('$skin_fn != 0'), 'missing original QC function: ' + name
    return integer('$skin_fn - qcvm->functions')


def entity_global(name, entity):
    run('set pr_global_struct->%s = (int)((char *)%s - (char *)qcvm->edicts)' % (name, entity))


def execute(name, actor=None, other=None):
    if actor is not None:
        entity_global('self', actor)
    if other is not None:
        entity_global('other', other)
    run('set pr_global_struct->time = qcvm->time')
    run('call (void)PR_ExecuteProgram(%d)' % qc_function(name))


def field(entity, name):
    run('set $skin_field = GetEdictFieldValueByName(%s,%s)' % (entity, json.dumps(name)))
    assert integer('$skin_field != 0'), 'missing original field: ' + name
    return '$skin_field'


def set_field(entity, name, value):
    run('set %s->_float = %g' % (field(entity, name), value))


def global_ref(name):
    run('set $skin_global = ED_FindGlobal(%s)' % json.dumps(name))
    assert integer('$skin_global != 0'), 'missing original global: ' + name
    return 'qcvm->globals[%d]' % integer('$skin_global->ofs')


def bind_players():
    # Reacquire map-owned edicts after native load; old addresses are invalid.
    for slot in range(2):
        run('set $c%d = &svs.clients[%d]' % (slot, slot))
        run('set $p%d = EDICT_NUM(%d)' % (slot, slot + 1))


def client_record(slot, clear=False):
    c, p = '$c%d' % slot, '$p%d' % slot
    if clear:
        # Native client initialization owns zeroing and original SetNewParms.
        # An unpaired loopback socket supplies native signon's timestamp only;
        # it has no peer, packets, UDP listener or network admission proof.
        run('call (void)SV_UnlinkEdict(%s)' % p)
        run('set %s->netconnection = NET_NewQSocket()' % c)
        assert integer('%s->netconnection != 0' % c), 'no private socket record'
        run('set %s->netconnection->driver = 0' % c)  # existing Loop driver
        run('set host_client = %s' % c)
        run('call (void)SV_ConnectClient(%d)' % slot)
    run('set %s->free = 0' % p)
    run('set %s->edict = %s' % (c, p))
    run('set %s->active = 1' % c)
    run('set net_activeconnections = %d' % sum(integer('svs.clients[%d].active' % i) != 0 for i in range(2)))
    for buffer, backing in (('message', 'msgbuf'), ('datagram', 'datagram_buf')):
        run('set %s->%s.data = %s->%s' % (c, buffer, c, backing))
        run('set %s->%s.maxsize = sizeof(%s->%s)' % (c, buffer, c, backing))
        run('set %s->%s.allowoverflow = 1' % (c, buffer))
    run('call (void)strcpy(%s->name,"skin_fixture_%d")' % (c, slot))
    run('set %s->v.netname = PR_SetEngineString("skin_fixture_%d")' % (p, slot))
    return c, p


def transfer_parms(slot, to_client):
    # Existing host spawn/connect boundary; do not synthesize skin values.
    for index in range(integer('sizeof($c0->spawn_parms) / sizeof(float)')):
        run('set $skin_global = ED_FindGlobal("parm%d")' % (index + 1))
        if integer('$skin_global == 0'):
            continue
        qc = 'qcvm->globals[%d]' % integer('$skin_global->ofs')
        native = '$c%d->spawn_parms[%d]' % (slot, index)
        run('set %s = %s' % ((native, qc) if to_client else (qc, native)))


def move_aside(slot):
    # Keep later spawns out of authored teleport limbo. Only geometry changes.
    run('set $p%d->v.origin[0] += %d' % (slot, 256 * (slot + 1)))
    run('call (void)SV_LinkEdict($p%d,0)' % slot)


def spawn(slot):
    run('set host_client = $c%d' % slot)
    run('set sv_player = $p%d' % slot)
    run('call (void)Cmd_ExecuteString("spawn",src_client)')
    run('call (void)Cmd_ExecuteString("begin",src_client)')
    move_aside(slot)
    assert string('PR_GetString($p%d->v.classname)' % slot) == 'player'
    assert number('$p%d->v.health' % slot) > 0
    acquire_weapons(slot)


def acquire_weapons(slot):
    # The authored start spawn fires target_items_takeinv, stripping both
    # weapons. Acquire them through original pickup initialization/touch;
    # never patch player inventory, selection, skin or weaponmodel.
    for kind in ('axe', 'shotgun'):
        run('set $pickup = ED_Alloc()')
        run('set $pickup->v.classname = PR_SetEngineString("weapon_%s")' % kind)
        execute('weapon_' + kind, '$pickup')
        assert integer('$pickup->v.touch') == qc_function('weapon_touch')
        execute('weapon_touch', '$pickup', '$p%d' % slot)
    assert integer('$p%d->v.items' % slot) & 4097 == 4097, 'original weapon pickups failed'


MODELS = {
    1: 'progs/v_hammer_default.mdl',
    9: 'progs/v_hammer_sblade.mdl',
    22: 'progs/v_hammer_katana.mdl',
}
observations = []


def model(slot, style, label):
    actual = string('PR_GetString($p%d->v.weaponmodel)' % slot)
    assert actual == MODELS[style], (label, slot, actual, MODELS[style])
    observations.append({'check': label, 'actor': slot, 'model': actual})


def reselect(slot, style):
    # Original impulse route switches away and back, including nested reset.
    for impulse in (2, 1):
        run('set $p%d->v.impulse = %d' % (slot, impulse))
        execute('W_ChangeWeapon', '$p%d' % slot)
        if impulse == 2:
            actual = string('PR_GetString($p%d->v.weaponmodel)' % slot)
            assert actual == 'progs/v_shot.mdl', (slot, actual,
                number('$p%d->v.items' % slot), number('$p%d->v.ammo_shells' % slot),
                number('$p%d->v.weapon' % slot), number(field('$p%d' % slot, 'customflags') + '->_float'))
    model(slot, style, 'original weapon reselect')
    execute('W_ResetWeaponState', '$p%d' % slot)
    model(slot, style, 'original explicit weapon reset')


def vr_selected(slot, style, foreign):
    p = '$p%d' % slot
    model(slot, style, 'VR actor input model')
    # Existing native guard must recognize both owners independently of the
    # last QC actor. A valid foreign hammer must fail, not just a bad model.
    assert integer('SV_BonkHammerWeaponSelected(%s)' % p), ('VR own model rejected', slot)
    saved = integer(p + '->v.weaponmodel')
    try:
        run('set %s->v.weaponmodel = PR_SetEngineString(%s)' % (p, json.dumps(MODELS[foreign])))
        assert not integer('SV_BonkHammerWeaponSelected(%s)' % p), ('VR foreign skin accepted', slot)
    finally:
        run('set %s->v.weaponmodel = %d' % (p, saved))


def pedestal(style, name, locked=False):
    run('set %s = ED_Alloc()' % name)
    run('set %s->v.classname = PR_SetEngineString("item_hammerskin")' % name)
    set_field(name, 'style', style)
    execute('item_hammerskin', name)
    assert integer(name + '->v.solid') == 1, 'original pedestal did not spawn'
    if locked:
        set_field(name, 'customflags', 16)  # authored CFL_LOCKED
    return name


def availability(ped, enabled):
    execute('hammerskin_think', ped, 'qcvm->edicts')
    expected = qc_function('hammerskin_touch' if enabled else 'SUB_Null')
    assert integer(ped + '->v.touch') == expected, ('pedestal callback', ped, enabled)
    assert abs(number(field(ped, 'alpha') + '->_float') - (1 if enabled else .5)) < .001
    assert abs(number(ped + '->v.nextthink') - number('qcvm->time') - .1) < .001
    if not enabled:
        assert integer(field(ped, 'customflags') + '->_float') & 16, 'original lock cleared'


def select(slot, style):
    ped = pedestals[style]
    if string('PR_GetString($p%d->v.weaponmodel)' % slot) == MODELS[style]:
        same_skin_touch(slot, style)
        return
    availability(ped, True)
    execute('hammerskin_touch', ped, '$p%d' % slot)
    model(slot, style, 'original touch nested reset/inspection')
    assert integer('$p%d->v.think' % slot) == qc_function('p_hammer_inspect_anim')


class CallCounter(gdb.Breakpoint):
    def __init__(self, symbol, condition=None):
        super().__init__(symbol, internal=True)
        self.count = 0
        self.predicate = condition

    def stop(self):
        # A Python stop callback owns its filtering; a GDB breakpoint condition
        # does not filter these callbacks on the fixture's GDB version.
        if self.predicate and not integer(self.predicate):
            return False
        self.count += 1
        return False


def weapon_state(slot):
    p = '$p%d' % slot
    return (tuple(number(p + '->v.' + name) for name in
                  ('think', 'frame', 'weaponframe', 'nextthink', 'weapon',
                   'weaponmodel')),
            number(field(p, 'attack_finished') + '->_float'),
            number(field(p, 'attack_finished_hammer') + '->_float'))


def no_op_touch(slot, style):
    before = weapon_state(slot)
    calls_before = tuple(counter.count for counter in touch_calls)
    sounds_before = len(sounds.samples)
    for _ in range(3):
        # World availability is general. The touch must enforce actor-specific
        # idempotence even when the shared pedestal is still enabled.
        availability(pedestals[style], True)
        execute('hammerskin_touch', pedestals[style], '$p%d' % slot)
        assert weapon_state(slot) == before, ('no-op touch changed weapon state', slot)
    assert tuple(counter.count for counter in touch_calls) == calls_before, (
        'no-op touch reset/inspect/print/stuffcmd spam', calls_before,
        tuple(counter.count for counter in touch_calls))
    assert len(sounds.samples) == sounds_before, 'no-op touch sound spam'


def same_skin_touch(slot, style):
    no_op_touch(slot, style)
    model(slot, style, 'repeated own pedestal leaves weapon state/calls unchanged')


def ineligible_touches(slot, own_style, different_style):
    p = '$p%d' % slot
    for member, blocked in (('health', '0'), ('movetype', '8'),
                            ('classname', 'PR_SetEngineString("monster_ogre")')):
        # Materialize before mutation: a lazy gdb.Value otherwise reads the
        # blocked value when finally restoring, leaving the actor ineligible.
        saved = (integer if member == 'classname' else number)(p + '->v.' + member)
        try:
            run('set %s->v.%s = %s' % (p, member, blocked))
            no_op_touch(slot, different_style)
            model(slot, own_style, 'original touch eligibility: ' + member)
        finally:
            run('set %s->v.%s = %s' % (p, member, saved))


class SoundSamples(gdb.Breakpoint):
    def __init__(self):
        super().__init__('SV_StartSound', internal=True)
        self.samples = []
        self.errors = []

    def stop(self):
        # Observe the actual sample passed by PF_sound; no inferior call from
        # a breakpoint and no dependency on statement IDs or adapter internals.
        try:
            self.samples.append((integer('entity'), string('sample')))
        except (gdb.error, ValueError) as error:
            self.errors.append(str(error))
        return False


def hit_sound(slot, style):
    start = len(sounds.samples)
    execute('Hammer_Hit_Enemy_Sound', '$p%d' % slot)
    assert not sounds.errors, sounds.errors
    samples = sounds.samples[start:]
    prefix = 'hammer/axhit' if style == 22 else 'weapons/axhit'
    suffix = '_katana.wav' if style == 22 else '.wav'
    allowed = {prefix + str(i) + suffix for i in (1, 2)}
    assert len(samples) == 1 and samples[0][0] == integer('$p%d' % slot) and samples[0][1] in allowed, samples
    observations.append({'check': 'original hit sound builtin', 'actor': slot, 'sample': samples[0][1]})


assert integer('qcvm == &sv.qcvm && sv.active && isDedicated && svs.maxclients == 2')
assert integer('SV_BonkHammerProgramLoaded()'), 'original installed VM/SHA/ABI rejected'
bind_players()
entity_global('self', 'qcvm->edicts')
client_record(0, clear=True)
assert number(global_ref('hammer_skin')) == number(global_ref('parm11')) == 1, 'root initial defaults'
spawn(0)
reselect(0, 1)
pedestals = {style: pedestal(style, '$ped%d' % style) for style in MODELS}
locked = pedestal(1, '$locked', locked=True)
sounds = SoundSamples()
# Resolve original function names, then observe only those QC call boundaries.
# This is invocation evidence, not an assertion about private adapter storage.
reset_id, inspect_id = (qc_function(name) for name in ('W_ResetWeaponState', 'p_hammer_inspect'))
new_parms_id = qc_function('SetNewParms')
new_parms_calls = CallCounter('PR_EnterFunction', 'f == &qcvm->functions[%d]' % new_parms_id)
touch_calls = [CallCounter('PR_EnterFunction',
                          'f == &qcvm->functions[%d] || f == &qcvm->functions[%d]' % (reset_id, inspect_id)),
               CallCounter('PF_sprint'), CallCounter('PF_stuffcmd')]
select(0, 9)
same_skin_touch(0, 9)

# A real root SetNewParms call with a previously selected player's stale self
# and parm11 is the bug trigger; do not zero scratch parms before this call.
assert integer('SV_CoopRespawnSetChangeParms($c0)')
assert number(global_ref('parm11')) == 9
entity_global('self', '$p0')
client_record(1, clear=True)
assert number(global_ref('parm11')) == 1, 'root new-player parms inherited another actor'
assert number(global_ref('hammer_skin')) == 1, 'root selector inherited stale self'
spawn(1)
reselect(1, 1)
reselect(0, 9)
select(1, 22)

# Dispatch the actual pedestal callback twice. Only the first touch's native
# nested refresh may reopen it; no fixture think/availability call intervenes.
availability(pedestals[1], True)
for slot in (0, 1):
    entity_global('self', pedestals[1])
    entity_global('other', '$p%d' % slot)
    run('call (void)PR_ExecuteProgram($ped1->v.touch)')
    model(slot, 1, 'successive actual pedestal callbacks without fixture think')
    assert integer('$p%d->v.think' % slot) == qc_function('p_hammer_inspect_anim')
    assert integer('$ped1->v.touch') == qc_function('hammerskin_touch'), 'touch did not immediately reopen pedestal'
select(0, 9)
select(1, 22)

ineligible_touches(0, 9, 22)
ineligible_touches(1, 22, 9)

# The very pedestal suppressed for its current owner must accept a different
# actor. Restore the two distinct skins afterward through original touches.
same_skin_touch(0, 9)
select(1, 9)
model(0, 9, 'peer selection from shared pedestal preserves current owner')
same_skin_touch(1, 9)
select(1, 22)

for choices in ((9, 22), (22, 9), (1, 22), (9, 1), (9, 22)):
    for slot, style in enumerate(choices):
        select(slot, style)
    for slot in (1, 0, 1, 0):
        reselect(slot, choices[slot])
    for ped in pedestals.values():
        availability(ped, True)  # including selected and default pedestals
    availability(locked, False)
    before_locked = tuple(weapon_state(slot) for slot in (0, 1))
    entity_global('self', locked)
    entity_global('other', '$p0')
    run('call (void)PR_ExecuteProgram($locked->v.touch)')
    assert tuple(weapon_state(slot) for slot in (0, 1)) == before_locked, 'locked callback changed weapon state'
    for slot in (1, 0):
        model(slot, choices[slot], 'locked pedestal preserves both actors')
        vr_selected(slot, choices[slot], 22 if choices[slot] != 22 else 9)
        hit_sound(slot, choices[slot])

# The native changelevel capture invokes original SetChangeParms for each
# actor. Check its QC-visible parm11, then decode each native client's parms.
for slot, style in ((1, 22), (0, 9)):
    assert integer('SV_CoopRespawnSetChangeParms($c%d)' % slot)
    assert number(global_ref('parm11')) == style
run('call (void)SV_SaveSpawnparms()')
for slot, style in ((1, 22), (0, 9)):
    transfer_parms(slot, False)
    execute('DecodeLevelParms', '$p%d' % slot)
    reselect(slot, style)
    model(1 - slot, 9 if slot == 1 else 22, 'map parm decode leaves peer model')

# The authored dead-player branch returns immediately after nested SetNewParms
# without assigning parm11. Seed scratch from the *other* actor, then execute
# original SetChangeParms directly so a native respawn inventory policy cannot
# temporarily revive the player and accidentally bypass this branch.
assert number(global_ref('reset_flag')) == 0, 'fixture requires normal SetChangeParms branch'
for slot, style, peer_style in ((0, 9, 22), (1, 22, 9)):
    execute('SetChangeParms', '$p%d' % (1 - slot))
    assert number(global_ref('parm11')) == peer_style
    saved_health = number('$p%d->v.health' % slot)
    saved_deadflag = number('$p%d->v.deadflag' % slot)
    run('set $p%d->v.health = -1' % slot)
    run('set $p%d->v.deadflag = 2' % slot)
    before_new = new_parms_calls.count
    execute('SetChangeParms', '$p%d' % slot)
    assert new_parms_calls.count == before_new + 1, 'dead original SetChangeParms did not enter nested SetNewParms'
    assert number(global_ref('parm11')) == style, 'dead actor inherited enemy scratch skin'
    transfer_parms(slot, True)
    execute('DecodeLevelParms', '$p%d' % slot)
    run('set $p%d->v.health = %g' % (slot, saved_health))
    run('set $p%d->v.deadflag = %g' % (slot, saved_deadflag))
    reselect(slot, style)
    reselect(1 - slot, peer_style)
    model(slot, style, 'dead nested new parms/decoded actor skin')

# Original coop-1 start-map respawn uses SetCoopParms, followed by
# PutClientInServer -> DecodeLevelParms and the authored weapon-strip target.
for slot, style in ((1, 22), (0, 9)):
    run('set $p%d->v.health = -1' % slot)
    run('set $p%d->v.deadflag = 2' % slot)
    execute('respawn', '$p%d' % slot)
    move_aside(slot)
    acquire_weapons(slot)
    assert number('$p%d->v.health' % slot) > 0
    assert number('$p%d->v.deadflag' % slot) == 0
    reselect(slot, style)
    vr_selected(slot, style, 9 if style == 22 else 22)

# Reuse the second record after its katana selection, with stale first actor
# scratch parms. Native drop/connect/spawn own the slot initialization.
assert integer('SV_CoopRespawnSetChangeParms($c0)')
run('set host_client = $c1')
run('call (void)SV_DropClient(0)')
entity_global('self', '$p0')
client_record(1, clear=True)
assert number(global_ref('parm11')) == 1, 'reused slot new parms not default'
assert number(global_ref('hammer_skin')) == 1, 'reused slot root selector not default'
spawn(1)
reselect(1, 1)
reselect(0, 9)
select(1, 22)

# Actual production serializer/load command, in the private staged gamedir.
# Save immediately after selections: do not refresh parms with SetChangeParms
# to hide a stale-save bug. Loaded living actors use the native inherited spawn.
run('call (void)Cvar_SetQuick(&sv_save_multiplayer,"1")')
run('call (void)Cmd_ExecuteString("save bonk_skins_fixture",src_command)')
run('call (void)Host_SavegameDrain()')
savefile = Path(os.environ['BONK_SKINS_STAGE']) / 'bonkjam' / 'bonk_skins_fixture.sav'
assert savefile.is_file() and savefile.stat().st_size > 0, 'native host save did not write'
select(0, 22)
select(1, 9)
run('call (void)PR_SwitchQCVM(0)')
run('call (void)Cmd_ExecuteString("load bonk_skins_fixture",src_command)')
run('call (void)PR_SwitchQCVM(&sv.qcvm)')
assert integer('sv.active && sv.loadgame_multiplayer'), 'native inherited load failed'
assert integer('SV_BonkHammerProgramLoaded()'), 'native load replaced original program'
bind_players()
for slot, style in ((1, 22), (0, 9)):
    assert integer('sv.loadgame_client_saved[%d]' % slot), 'host load missing saved actor'
    c, p = client_record(slot)
    run('set %s->spawned = 0' % c)
    run('set %s->spawn_parms_pending = 1' % c)
    run('set host_client = %s' % c)
    run('set sv_player = %s' % p)
    run('call (void)Cmd_ExecuteString("spawn",src_client)')
    run('call (void)Cmd_ExecuteString("begin",src_client)')
    assert not integer('sv.loadgame_client_saved[%d]' % slot), 'host spawn did not consume saved actor'
    model(slot, style, 'actual host load restored model')
    reselect(slot, style)
    vr_selected(slot, style, 9 if style == 22 else 22)
    hit_sound(slot, style)
assert not integer('sv.loadgame'), 'native inherited load did not finish'
sounds.delete()
for counter in touch_calls:
    counter.delete()
new_parms_calls.delete()

report = {
    'scope_done': True,
    'observations': observations,
    'coverage': ['two original QC players', 'interleaved touch/reset/reselect',
                 'native client connect/spawn and original weapon pickups after authored start-map strip',
                 'two successive successful actual pedestal callbacks without intervening fixture think',
                 'repeat own pedestal has no state/reset/inspect/sound/print/stuffcmd effects; different actor can select',
                 'root late join/reused slot defaults', 'native SetChangeParms/capture and QC map decode',
                 'dead SetChangeParms nested SetNewParms with enemy scratch parm11',
                 'original dead/noclip/non-player touch eligibility',
                 'original start-map respawn', 'unlocked/default/locked pedestal callbacks',
                 'native VR own/foreign selected-model guard', 'original sound builtin samples',
                 'actual host multiplayer save/load and inherited living spawn'],
    'missing_coverage': ['network admission/disconnect/reconnect and identity matching',
                         'actual changelevel to a different map', 'non-start-map timed co-op death policy',
                         'dead-player save/load', 'single-player and other-mod controls',
                         'physical headset presentation/audio'],
}
Path(os.environ['BONK_SKINS_REPORT']).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('BONK_SKINS_RUNTIME_PASSED')
