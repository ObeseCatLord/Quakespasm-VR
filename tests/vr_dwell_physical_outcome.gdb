# Exercise the pinned Dwell 2.2 physical W_FireAxe leaf and shared cooldown.
# Run: tests/vr_dwell_runtime.sh tests/vr_dwell_physical_outcome.gdb
# This bypasses contact admission and does not prove a headset gesture.
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
set $p->v.weaponmodel = PR_SetEngineString("progs/v_axeb.mdl")
set $p->v.think = 549
set $p->v.nextthink = qcvm->time + 1
set $p->v.view_ofs[2] = 22
set $client->cmd.vr_active = 1
set $client->cmd.vr_handpos_relative = 1
set $client->cmd.vr_akimbo_active = 1
set $client->cmd.vr_akimbo_berserk = 1
set $client->private_vr_contact_cursor_valid = 1
set $client->private_vr_contact_last_sequence = 1
set $berserk = GetEdictFieldValueByName($p,"berserk_finished")
set $berserk->_float = qcvm->time + 10
set $customflags = GetEdictFieldValueByName($p,"customflags")
set $customflags->_float = 0
set $cooldown = GetEdictFieldValueByName($p,"attack_finished")
set $cooldown->_float = qcvm->time
set $before = $cooldown->_float
set $recognized = SV_DwellBerserkAkimboWeaponSelected($p)
if !$recognized
  error Dwell weapon was not recognized
end
set $result = SV_VRDwellBerserkPhysicalOutcome($client,$p,&$client->cmd,0,0)
if !$result
  error Dwell physical whiff callback rejected
end
if $cooldown->_float <= $before
  error Dwell physical whiff did not advance cooldown
end
set $accepted_cooldown = $cooldown->_float
set $again = SV_VRDwellBerserkPhysicalOutcome($client,$p,&$client->cmd,1,0)
if $again || $cooldown->_float != $accepted_cooldown
  error Dwell cooldown failed to reject a second hand
end
printf "VR_DWELL_WHIFF_PASSED cooldown=%f\n", $cooldown->_float

# A pending native weapon callback excludes an extra physical attack.
set $cooldown->_float = qcvm->time
set $p->v.think = 438
set $pending = SV_VRDwellBerserkPhysicalOutcome($client,$p,&$client->cmd,0,0)
if $pending || $cooldown->_float != (float)qcvm->time
  error Pending Dwell native attack admitted a physical outcome
end
set $p->v.think = 549

# Use a damageable QC entity with valid authored no-op pain/death callbacks.
# The synthetic trace tests native damage and trace interception, not sweeping.
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
set $hit = SV_VRDwellBerserkPhysicalOutcome($client,$p,&$client->cmd,1,$trace)
if !$hit || $target->v.health >= 1000 || $target->v.health <= 0
  error Dwell physical hit did not damage the target as expected
end
set $accepted_health = $target->v.health
set $accepted_cooldown = $cooldown->_float
set $again = SV_VRDwellBerserkPhysicalOutcome($client,$p,&$client->cmd,0,$trace)
if $again || $cooldown->_float != $accepted_cooldown || $target->v.health != $accepted_health
  error Dwell second-hand cooldown allowed another hit
end
printf "VR_DWELL_PHYSICAL_OUTCOME_PASSED health=%f cooldown=%f\n", $target->v.health, $cooldown->_float

# Native has_haste reads the Dwell haste item and its expiry together.
set $items_dwell = GetEdictFieldValueByName($p,"items_dwell")
set $haste_finished = GetEdictFieldValueByName($p,"haste_finished")
set $items_dwell->_float = 1
set $haste_finished->_float = qcvm->time + 10
set $cooldown->_float = qcvm->time
set $hasted = SV_VRDwellBerserkPhysicalOutcome($client,$p,&$client->cmd,0,0)
set $haste_delta = $cooldown->_float - (float)qcvm->time
if !$hasted || $haste_delta < 0.29399 || $haste_delta > 0.29401
  error Dwell native haste did not scale physical cooldown
end
set $items_dwell->_float = 0
set $haste_finished->_float = 0

# The shared co-op shield protects an actual second client slot and restores
# takedamage afterward. The monster damage above remains unaffected by policy.
set $teammate = EDICT_NUM(2)
set svs.clients[1].active = 1
set svs.clients[1].spawned = 1
set svs.clients[1].edict = $teammate
set $teammate->v.health = 1000
set $teammate->v.takedamage = 1
set $teammate->v.solid = 2
set $teammate->v.classname = PR_SetEngineString("player")
set $pain = GetEdictFieldValueByName($teammate,"th_pain")
set $die = GetEdictFieldValueByName($teammate,"th_die")
set $pain->function = $noop - qcvm->functions
set $die->function = $noop - qcvm->functions
set $trace->ent = $teammate
set $cooldown->_float = qcvm->time
set $shielded = SV_VRDwellBerserkPhysicalOutcome($client,$p,&$client->cmd,1,$trace)
if !$shielded || $teammate->v.health != 1000 || $teammate->v.takedamage != 1
  error Dwell co-op shield failed or leaked takedamage state
end
set $trace->ent = $target
printf "VR_DWELL_HASTE_AND_TEAMMATE_PASSED\n"

# The engine clock is double, but QuakeC receives float time. Expiry admission
# must agree with QC when a slightly earlier double rounds to the expiry.
set qcvm->time = 99.999999
set $berserk->_float = 100
set $cooldown->_float = 0
set $selected = SV_DwellBerserkAkimboWeaponSelected($p)
set $expired = SV_VRDwellBerserkPhysicalOutcome($client,$p,&$client->cmd,0,$trace)
if $selected || $expired || $cooldown->_float != 0 || $target->v.health != $accepted_health
  error Rounded QC expiry admitted a physical strike
end
printf "VR_DWELL_EXPIRY_CLOCK_PASSED\n"
quit 0
