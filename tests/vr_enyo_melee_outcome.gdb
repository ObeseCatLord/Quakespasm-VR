# Native Enyo katana leaf, recovery and callback guards. No contact queue or HMD.
# Run only where ptrace is available: bash tests/vr_enyo_melee_outcome.sh
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
set python print-stack full
break SV_Physics
run
python
import gdb

def run(text):
    gdb.execute(text, to_string=True)

def integer(expression):
    return int(gdb.parse_and_eval(expression))

def number(expression):
    return float(gdb.parse_and_eval(expression))

def vector(expression, values):
    for axis, value in enumerate(values):
        run('set %s[%d] = %.9g' % (expression, axis, value))

def outcome(first=True, contact='$trace'):
    return integer('SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,%s,%d,$subtype,%s)' %
                   (contact, first, '$deadline' if first else '0'))

assert integer('SV_EnyoMeleeProgramLoaded()'), 'installed Enyo pin rejected'
run('set $client = &svs.clients[0]')
run('set $p = EDICT_NUM(1)')
for field in ('active', 'spawned'):
    run('set $client->%s = 1' % field)
run('set $client->edict = $p')
run('set $client->protocol_qsvr = 1')
run('set $p->v.health = 100')
run('set $p->v.deadflag = 0')
run('set $p->v.weapon = 4096')
run('set $p->v.weaponmodel = PR_SetEngineString("progs/ee_v_sword.mdl")')
run('set $p->v.think = 506')
run('set $p->v.nextthink = qcvm->time + 1')
vector('$p->v.v_angle', (5, 17, -12))
run('set $client->cmd.vr_active = 1')
run('set $client->cmd.vr_handpos_relative = 1')
vector('$client->cmd.vr_handrot', (0, 90, 0))
run('set $cooldown = GetEdictFieldValueByName($p,"attack_finished")')
run('set $switchblock = GetEdictFieldValueByName($p,"switchblock_finished")')
run('set $hostile = GetEdictFieldValueByName($p,"show_hostile")')
run('set $cooldown->_float = qcvm->time')
run('set $switchblock->_float = 0')
run('set $hostile->_float = 0')
run('set $subtype = (int *)Mem_Alloc(sizeof(int))')
run('set $deadline = (float *)Mem_Alloc(sizeof(float))')
run('call (void)Cvar_SetQuick(&sv_immersive_melee,"1")')
assert integer('SV_VREnyoMeleeEnabled()')
assert outcome(contact='0'), 'physical whiff rejected'
assert abs(number('$cooldown->_float - (float)qcvm->time') - .4) < .0001
assert number('$switchblock->_float') == number('$cooldown->_float')
assert number('*$deadline') == number('$cooldown->_float')
assert integer('*$subtype == SV_VR_DIRECT_MELEE_ENYO_SWORD')
assert number('$hostile->_float') == 0
assert integer('$p->v.think') == 506, 'whiff installed native attack animation'
assert not outcome(contact='0'), 'cooldown admitted a second first outcome'

run('set $cooldown->_float = qcvm->time')
for think in range(551, 560):
    run('set $p->v.think = %d' % think)
    assert not outcome(contact='0'), 'pending native attack was stolen'
run('set $p->v.think = 506')

run('set $noop = ED_FindFunction("SUB_Null")')
for target in ('$target', '$second'):
    run('set %s = ED_Alloc()' % target)
    run('set %s->v.health = 1000' % target)
    run('set %s->v.takedamage = 1' % target)
    run('set %s->v.solid = 2' % target)
    run('set %s->v.flags = 32' % target)  # FL_MONSTER: native hit animation branch
    run('set %s->v.classname = PR_SetEngineString("monster_ogre")' % target)
    for field in ('th_pain', 'th_die'):
        run('set $callback = GetEdictFieldValueByName(%s,"%s")' % (target, field))
        run('set $callback->function = $noop - qcvm->functions')
run('set $trace = (trace_t *)Mem_Alloc(sizeof(trace_t))')
run('call (void)memset($trace,0,sizeof(trace_t))')
run('set $trace->ent = $target')
run('set $trace->fraction = 0.5')
vector('$trace->endpos', (16, 0, 22))
vector('$trace->plane.normal', (1, 0, 0))
run('set $trace->plane.dist = 16')
assert outcome(), 'native hitsword rejected its own hit aftermath'
assert number('$target->v.health') < 1000, 'native hitsword dealt no damage'
assert integer('$p->v.think') in range(568, 573), 'native hit aftermath missing'
assert number('$hostile->_float') > number('(float)qcvm->time')
assert [number('$p->v.v_angle[%d]' % i) for i in range(3)] == [5, 17, -12]
deadline = number('*$deadline')
switchblock = number('$switchblock->_float')
run('set $trace->ent = $second')
assert outcome(first=False), 'second native leaf rejected harmless aftermath'
assert number('$second->v.health') < 1000
assert number('$cooldown->_float') == deadline
assert number('$switchblock->_float') == switchblock, 'follow-up replayed prelude'

run('set $p->v.weaponmodel = PR_SetEngineString("progs/ee_v_smg.mdl")')
before = number('$second->v.health')
assert not outcome(first=False), 'changed live weapon retained sword admission'
assert number('$second->v.health') == before
run('set $p->v.weaponmodel = PR_SetEngineString("progs/ee_v_sword.mdl")')
run('set $client->cmd.vr_active = 0')
run('set $cooldown->_float = qcvm->time')
assert not outcome(), 'desktop input entered physical sword callback'

# A failing native callback may still mutate its owner. Preserve caller
# context without publishing successful recovery or accepting another hit.
class KillOwnerAtLeafReturn(gdb.Breakpoint):
    def __init__(self):
        super().__init__('SV_VRAxeTraceLeaveFunction', internal=True)
        self.injected = False

    def stop(self):
        if not self.injected and integer('qcvm->xfunction == &qcvm->functions[347]'):
            run('set $p->v.health = 0')
            run('set $p->v.deadflag = 2')
            self.injected = True
        return False

run('set $client->cmd.vr_active = 1')
run('set $cooldown->_float = qcvm->time')
run('set *$deadline = -1234')
fault = KillOwnerAtLeafReturn()
try:
    assert not outcome(), 'death accepted a successful physical outcome'
    assert fault.injected, 'native leaf-return fault did not run'
    assert number('*$deadline') == -1234
    assert [number('$p->v.v_angle[%d]' % i) for i in range(3)] == [5, 17, -12]
    before = number('$second->v.health')
    assert not outcome(first=False)
    assert number('$second->v.health') == before
finally:
    fault.delete()
print('ENYO_MELEE_NATIVE_OUTCOME_PASSED')
end
quit
