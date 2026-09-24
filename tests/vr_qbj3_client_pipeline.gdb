# Live QBJ3 twin-nailgun path under a tracked OpenXR session. Run with the
# isolated Monado qwerty runtime and the QBJ3 content, as described in README.
# The debugger grants the weapon because start does not spawn one immediately.
set pagination off
set confirm off
set debuginfod enabled off
set $granted = 0
set $ticks = 0
set $left_draws = 0
set $right_draws = 0
set $wire_left_x = 0
set $wire_right_x = 0

break V_PrepareAkimboPair if cl.vr_qbj3_akimbo_supported && cl.viewent.model != 0
commands
  silent
  if (int)strcmp(cl.viewent.model->name, "progs/v_tnailgun.mdl") == 0
    finish
    if !akimbo_pair_prepared || !(int)V_AkimboPairReady()
      printf "QBJ3_PIPELINE_FAIL pair not ready\n"
      quit 1
    end
    printf "QBJ3_PIPELINE_READY left=%s right=%s\n", akimbo_pair_models[0]->name, akimbo_pair_models[1]->name
    disable 1
    enable 2
    continue
  end
  if !$granted
    set $granted = 1
    set svs.clients[0].edict->v.items = (int)svs.clients[0].edict->v.items | 4
    set svs.clients[0].edict->v.ammo_nails = 100
    set svs.clients[0].edict->v.weapon = 4
    call (void)PR_SwitchQCVM(&sv.qcvm)
    set svs.clients[0].edict->v.weaponmodel = PR_SetEngineString("progs/v_tnailgun.mdl")
    call (void)PR_SwitchQCVM(0)
    set in_impulse = 4
  end
  set $ticks = $ticks + 1
  if $ticks >= 30
    printf "QBJ3_PIPELINE_FAIL weapon equip timed out\n"
    quit 1
  end
  continue
end

break R_DrawAliasModel if e == &akimbo_pair_entities[0] || e == &akimbo_pair_entities[1]
commands
  silent
  if e->frame != cl.viewent.frame
    printf "QBJ3_PIPELINE_FAIL pair frame mismatch\n"
    quit 1
  end
  if e == &akimbo_pair_entities[0]
    set $left_draws = $left_draws + 1
  else
    set $right_draws = $right_draws + 1
  end
  if $left_draws > 0 && $right_draws > 0
    printf "QBJ3_PIPELINE_DRAW left=%d right=%d frame=%d\n", $left_draws, $right_draws, e->frame
    disable 2
    enable 3
  end
  continue
end
disable 2

break CL_WritePrivateUsercmd if cmd->vr_akimbo_active
commands
  silent
  if !cmd->vr_active || !cmd->vr_handpos_relative || cmd->vr_akimbo_berserk
    printf "QBJ3_PIPELINE_FAIL invalid private pair command\n"
    quit 1
  end
  if cmd->vr_akimbo_muzzle[0][0] == cmd->vr_akimbo_muzzle[1][0] || cmd->vr_handpos[0] != cmd->vr_akimbo_muzzle[1][0]
    printf "QBJ3_PIPELINE_FAIL muzzles collapsed or base not dominant\n"
    quit 1
  end
  set $wire_left_x = cmd->vr_akimbo_muzzle[0][0]
  set $wire_right_x = cmd->vr_akimbo_muzzle[1][0]
  printf "QBJ3_PIPELINE_WIRE left_x=%f right_x=%f\n", $wire_left_x, $wire_right_x
  set in_attack.state = 3
  disable 3
  enable 4
  continue
end
disable 3

break sv_phys.c:3124 if sv_vr_weapon_pose_scope && sv_vr_weapon_pose_scope->akimbo_pose_valid
commands
  silent
  if !svs.clients[0].cmd.vr_akimbo_active || (int)svs.clients[0].edict->v.weapon != 4
    printf "QBJ3_PIPELINE_FAIL server rejected pair\n"
    quit 1
  end
  if sv_vr_weapon_pose_scope->akimbo_muzzle[0][0] != $wire_left_x || sv_vr_weapon_pose_scope->akimbo_muzzle[1][0] != $wire_right_x
    printf "QBJ3_PIPELINE_FAIL server muzzle changed\n"
    quit 1
  end
  printf "QBJ3_PIPELINE_SERVER pair accepted\n"
  disable 4
  enable 5
  continue
end
disable 4

break sv_phys.c:3162
commands
  silent
  if !sv_vr_weapon_pose_scope || !sv_vr_weapon_pose_scope->akimbo_pose_valid || !svs.clients[0].cmd.vr_akimbo_active
    printf "QBJ3_PIPELINE_FAIL shot without pair pose\n"
    quit 1
  end
  printf "QBJ3_PIPELINE_PASSED weaponframe=%d\n", (int)svs.clients[0].edict->v.weaponframe
  quit 0
end
disable 5
run
