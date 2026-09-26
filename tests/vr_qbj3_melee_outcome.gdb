# Direct native QBJ3 wrench/fist leaf test against the installed hash-pinned QC.
# Contact admission and headset gestures are intentionally outside this fixture.
# Run: tests/vr_qbj3_melee_outcome.sh
set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
break SV_Physics
run

call (void)Cvar_SetQuick(&sv_nofriendlyfire,"1")
set $client = &svs.clients[0]
set $p = EDICT_NUM(1)
set $client->active = 1
set $client->spawned = 1
set $client->edict = $p
set $client->protocol_qsvr = 1
set $p->v.health = 100
set $p->v.deadflag = 0
set $p->v.weapon = 4096
set $p->v.weaponmodel = PR_SetEngineString("progs/v_wrench.mdl")
set $p->v.think = 569
set $p->v.nextthink = qcvm->time + 1
set $p->v.view_ofs[2] = 22
set $p->v.v_angle[0] = 5
set $p->v.v_angle[1] = 17
set $client->cmd.vr_active = 1
set $client->cmd.vr_handpos_relative = 1
set $client->cmd.vr_handrot[0] = 0
set $client->cmd.vr_handrot[1] = 90
set $client->cmd.vr_handrot[2] = 0
set $client->cmd.vr_akimbo_active = 1
set $client->cmd.vr_akimbo_berserk = 1
set $client->cmd.vr_akimbo_angles[0][0] = 0
set $client->cmd.vr_akimbo_angles[0][1] = 180
set $client->cmd.vr_akimbo_angles[0][2] = 0
set $client->cmd.vr_akimbo_angles[1][0] = 0
set $client->cmd.vr_akimbo_angles[1][1] = 270
set $client->cmd.vr_akimbo_angles[1][2] = 0
set $items = GetEdictFieldValueByName($p,"items_qbj")
set $items->_float = 0
set $finished = GetEdictFieldValueByName($p,"berserk_finished")
set $finished->_float = 0
set $cooldown = GetEdictFieldValueByName($p,"attack_finished")
set $cooldown->_float = qcvm->time
set $hostile = GetEdictFieldValueByName($p,"show_hostile")
set $hostile->_float = 0
set $soundtimer = GetEdictFieldValueByName($p,"berserk_sound")
set $soundtimer->_float = -1
set $stroke = (int *)Mem_Alloc(sizeof(int))
set $deadline = (float *)Mem_Alloc(sizeof(float))

if !SV_QBJ3TwinNailgunProgramLoaded()
  error Installed QBJ3 program did not pass the shared exact gate
end
set $whiff = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,0,1,$stroke,$deadline)
set $delta = $cooldown->_float - (float)qcvm->time
if !$whiff || $delta < 0.7999 || $delta > 0.8001 || $hostile->_float != 0 || *$stroke != SV_VR_DIRECT_MELEE_QBJ3_WRENCH || *$deadline != $cooldown->_float
  error Wrench whiff failed to preserve native recovery or hostility
end
if $p->v.v_angle[0] != 5 || $p->v.v_angle[1] != 17
  error Wrench callback failed to restore the player's QC angle context
end
set $again = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,0,1,$stroke,$deadline)
if $again || $cooldown->_float - (float)qcvm->time != $delta
  error Wrench cooldown admitted a duplicate outcome
end
printf "QBJ3_WRENCH_WHIFF_COOLDOWN_PASSED delta=%f\n", $delta

set $cooldown->_float = qcvm->time
set $p->v.think = 479
set $pending = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,0,1,$stroke,$deadline)
if $pending || $cooldown->_float != (float)qcvm->time
  error Pending native wrench loop admitted a physical outcome
end
set $p->v.think = 575
set $p->v.weaponframe = 10
set $draw = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,0,1,$stroke,$deadline)
if !$draw
  error Terminal native draw loop was incorrectly excluded
end
set $p->v.think = 569
set $cooldown->_float = qcvm->time

set $target = ED_Alloc()
set $target->v.health = 1000
set $target->v.takedamage = 1
set $target->v.solid = 2
set $target->v.classname = PR_SetEngineString("monster_ogre")
set $noop = ED_FindFunction("SUB_Null")
set $pain = GetEdictFieldValueByName($target,"th_pain")
set $die = GetEdictFieldValueByName($target,"th_die")
set $pain->function = $noop - qcvm->functions
set $die->function = $noop - qcvm->functions
set $target2 = ED_Alloc()
set $target2->v.health = 1000
set $target2->v.takedamage = 1
set $target2->v.solid = 2
set $target2->v.classname = PR_SetEngineString("monster_ogre")
set $pain2 = GetEdictFieldValueByName($target2,"th_pain")
set $die2 = GetEdictFieldValueByName($target2,"th_die")
set $pain2->function = $noop - qcvm->functions
set $die2->function = $noop - qcvm->functions
set $trace = (trace_t *)Mem_Alloc(sizeof(trace_t))
set $trace->ent = $target
set $trace->fraction = 0.5
set $trace->startsolid = 0
set $trace->allsolid = 0
set $trace->endpos[0] = 16
set $trace->endpos[1] = 0
set $trace->endpos[2] = 22
set $trace->plane.normal[0] = 1
set $trace->plane.normal[1] = 0
set $trace->plane.normal[2] = 0
set $trace->plane.dist = 16
set $hit = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,$trace,1,$stroke,$deadline)
if !$hit || $target->v.health != 940
  error Native hitwrench leaf failed to damage the accepted target
end
set $wrench_health = $target->v.health
if $hostile->_float <= (float)qcvm->time
  error Native wrench hit did not mark player hostile
end
set $wrench_cooldown = $cooldown->_float
set $trace->ent = $target2
set $second = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,$trace,0,$stroke,0)
if !$second || $target2->v.health != 940 || $cooldown->_float != $wrench_cooldown || *$deadline != $wrench_cooldown
  error Distinct second wrench leaf failed or replayed its prelude
end
set $wrench_second_health = $target2->v.health
set $trace->ent = $target
printf "QBJ3_WRENCH_HIT_PASSED health=%f\n", $target->v.health

# The same callback chooses the berserk native leaf from live model and item.
set $p->v.weaponmodel = PR_SetEngineString("progs/v_berserk.mdl")
set $items->_float = 4
set $trace->ent = $target2
set $changed = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,$trace,0,$stroke,0)
if $changed || $target2->v.health != $wrench_second_health
  error Changed live subtype admitted a wrench stroke follow-up
end
set $trace->ent = $target
set $cooldown->_float = qcvm->time
set $fist = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,0,$trace,1,$stroke,$deadline)
set $delta = $cooldown->_float - (float)qcvm->time
if !$fist || $target->v.health != $wrench_health - 480 || $delta < 0.4999 || $delta > 0.5001 || *$stroke != SV_VR_DIRECT_MELEE_QBJ3_BERSERK
  error Native berserk punch leaf or cooldown failed
end
set $fist_health = $target->v.health
if $soundtimer->_float <= (float)qcvm->time
  error Berserk cue timer did not advance
end
set $fist_cooldown = $cooldown->_float
set $fist_soundtimer = $soundtimer->_float
set $trace->ent = $target2
set $second = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,$trace,0,$stroke,0)
if !$second || $target2->v.health != $wrench_second_health - 480 || $cooldown->_float != $fist_cooldown || $soundtimer->_float != $fist_soundtimer
  error Distinct second berserk leaf failed or replayed its prelude
end
set $trace->ent = $target
printf "QBJ3_BERSERK_HIT_PASSED health=%f delta=%f\n", $fist_health, $delta

set $cooldown->_float = qcvm->time
set $miss = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,0,1,$stroke,$deadline)
if !$miss || $target->v.health != $fist_health || $cooldown->_float <= (float)qcvm->time
  error Berserk whiff failed native recovery or damaged target
end
set $cooldown->_float = qcvm->time
set $p->v.think = 565
set $pending = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,0,$trace,1,$stroke,$deadline)
if $pending || $target->v.health != $fist_health
  error Pending native berserk loop admitted a physical outcome
end
set $p->v.think = 569
printf "QBJ3_BERSERK_WHIFF_PENDING_PASSED\n"

# QuakeC sees float time; this engine double rounds to the same float expiry.
set qcvm->time = 99.999999
set $items->_float = 0
set $finished->_float = 100
set $cooldown->_float = 0
set $expired = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,0,$trace,1,$stroke,$deadline)
if $expired || $cooldown->_float != 0 || $target->v.health != $fist_health
  error QBJ3 float expiry admitted an inactive berserk strike
end
set $p->v.weaponmodel = PR_SetEngineString("progs/v_wrench.mdl")
set $client->cmd.vr_active = 0
set $desktop = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,$trace,1,$stroke,$deadline)
if $desktop || $cooldown->_float != 0 || $target->v.health != $fist_health
  error Desktop input entered the VR-only physical callback
end
printf "QBJ3_EXPIRY_DESKTOP_GUARDS_PASSED\n"

# Inject death at the native leaf's VM-return boundary. This verifies cleanup
# after a terminal callback without claiming a particular native self-kill.
set $client->cmd.vr_active = 1
set $p->v.health = 100
set $p->v.deadflag = 0
set $p->v.v_angle[0] = 5
set $p->v.v_angle[1] = 17
set $p->v.v_angle[2] = -12
set $target->v.health = 1000
set *$deadline = -1234
python
import gdb

class KillOwnerAtLeafReturn(gdb.Breakpoint):
    def __init__(self):
        super().__init__('SV_VRAxeTraceLeaveFunction', internal=True)
        self.injected = False

    def stop(self):
        if not self.injected and int(gdb.parse_and_eval(
                'qcvm->xfunction == &qcvm->functions[485]')):
            gdb.execute('set $p->v.health = 0', to_string=True)
            gdb.execute('set $p->v.deadflag = 2', to_string=True)
            self.injected = True
        return False

probe = KillOwnerAtLeafReturn()
try:
    gdb.execute('set $died = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,$trace,1,$stroke,$deadline)', to_string=True)
    assert probe.injected, 'native leaf return was not exercised'
    assert int(gdb.parse_and_eval('$died')) == 0
    assert float(gdb.parse_and_eval('*$deadline')) == -1234
    assert float(gdb.parse_and_eval('$target->v.health')) == 940
    assert [float(gdb.parse_and_eval('$p->v.v_angle[%d]' % i))
            for i in range(3)] == [5, 17, -12], 'dead owner retained borrowed hand angles'
    gdb.execute('set $afterdeath = SV_VRDirectMeleeOutcome($client,$p,&$client->cmd,1,$trace,0,$stroke,0)', to_string=True)
    assert int(gdb.parse_and_eval('$afterdeath')) == 0
    assert float(gdb.parse_and_eval('$target->v.health')) == 940
    print('QBJ3_CALLBACK_DEATH_RESTORE_PASSED')
finally:
    probe.delete()
end
quit 0
