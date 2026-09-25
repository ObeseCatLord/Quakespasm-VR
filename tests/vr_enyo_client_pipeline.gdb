# Live Enyo paired-SMG path under a tracked OpenXR session. Use the isolated
# Monado QWERTY runtime and licensed Enyo assets described in tests/README.md.
# GDB equips the local player because map start does not grant the SMGs.
set pagination off
set confirm off
set debuginfod enabled off
set $granted = 0
set $ticks = 0
set $left_draws = 0
set $right_draws = 0
set $draw_frame = -1
set $ammo_before = 0
set $shot_hand = -1
set $makevectors_ok = 0
set $aim_ok = 0

break V_PrepareAkimboPair if cl.vr_enyo_akimbo_supported && cl.viewent.model != 0
commands
  silent
  if (int)strcmp(cl.viewent.model->name, "progs/ee_v_smgs.mdl") == 0
    finish
    if !akimbo_pair_prepared || !(int)V_AkimboPairReady()
      printf "ENYO_PIPELINE_FAIL pair not ready\n"
      quit 1
    end
    if (int)strcmp(akimbo_pair_models[0]->name, "vr/enyo/progs/ee_v_smgs_vr_left.mdl") != 0
      printf "ENYO_PIPELINE_FAIL unexpected left pair model\n"
      quit 1
    end
    if (int)strcmp(akimbo_pair_models[1]->name, "vr/enyo/progs/ee_v_smgs_vr_right.mdl") != 0
      printf "ENYO_PIPELINE_FAIL unexpected paired models\n"
      quit 1
    end
    printf "ENYO_PIPELINE_READY left=%s right=%s\n", akimbo_pair_models[0]->name, akimbo_pair_models[1]->name
    disable 1
    enable 2
    continue
  end
  if !$granted
    set $granted = 1
    set svs.clients[0].edict->v.items = (int)svs.clients[0].edict->v.items | 4
    set svs.clients[0].edict->v.ammo_nails = 100
    set $ammo_before = svs.clients[0].edict->v.ammo_nails
    set svs.clients[0].edict->v.weapon = 4
    call (void)PR_SwitchQCVM(&sv.qcvm)
    set svs.clients[0].edict->v.weaponmodel = PR_SetEngineString("progs/ee_v_smgs.mdl")
    call (void)PR_SwitchQCVM(0)
    set in_impulse = 4
  end
  set $ticks = $ticks + 1
  if $ticks > 35
    printf "ENYO_PIPELINE_FAIL weapon equip timed out\n"
    quit 1
  end
  continue
end

break R_DrawAliasModel if e == &akimbo_pair_entities[0] || e == &akimbo_pair_entities[1]
commands
  silent
  if e->frame != cl.viewent.frame
    printf "ENYO_PIPELINE_FAIL pair frame mismatch\n"
    quit 1
  end
  if $draw_frame < 0
    set $draw_frame = e->frame
  else
    if e->frame != $draw_frame
      printf "ENYO_PIPELINE_FAIL hands drawn from different frames\n"
      quit 1
    end
  end
  if e == &akimbo_pair_entities[0]
    set $left_draws = $left_draws + 1
  else
    set $right_draws = $right_draws + 1
  end
  if $left_draws > 0 && $right_draws > 0
    printf "ENYO_PIPELINE_DRAW left=%d right=%d frame=%d\n", $left_draws, $right_draws, $draw_frame
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
    printf "ENYO_PIPELINE_FAIL invalid private pair command\n"
    quit 1
  end
  if cmd->vr_akimbo_muzzle[0][0] != cmd->vr_akimbo_muzzle[0][0]
    printf "ENYO_PIPELINE_FAIL non-finite left muzzle\n"
    quit 1
  end
  if cmd->vr_akimbo_muzzle[0][1] != cmd->vr_akimbo_muzzle[0][1]
    printf "ENYO_PIPELINE_FAIL non-finite left muzzle\n"
    quit 1
  end
  if cmd->vr_akimbo_muzzle[0][2] != cmd->vr_akimbo_muzzle[0][2]
    printf "ENYO_PIPELINE_FAIL non-finite left muzzle\n"
    quit 1
  end
  if cmd->vr_akimbo_muzzle[1][0] != cmd->vr_akimbo_muzzle[1][0]
    printf "ENYO_PIPELINE_FAIL non-finite right muzzle\n"
    quit 1
  end
  if cmd->vr_akimbo_muzzle[1][1] != cmd->vr_akimbo_muzzle[1][1]
    printf "ENYO_PIPELINE_FAIL non-finite right muzzle\n"
    quit 1
  end
  if cmd->vr_akimbo_muzzle[1][2] != cmd->vr_akimbo_muzzle[1][2]
    printf "ENYO_PIPELINE_FAIL non-finite right muzzle\n"
    quit 1
  end
  set $pair_dx = cmd->vr_akimbo_muzzle[0][0] - cmd->vr_akimbo_muzzle[1][0]
  set $pair_dy = cmd->vr_akimbo_muzzle[0][1] - cmd->vr_akimbo_muzzle[1][1]
  set $pair_dz = cmd->vr_akimbo_muzzle[0][2] - cmd->vr_akimbo_muzzle[1][2]
  if $pair_dx * $pair_dx + $pair_dy * $pair_dy + $pair_dz * $pair_dz < 1.0
    printf "ENYO_PIPELINE_FAIL paired muzzles collapsed\n"
    quit 1
  end
  printf "ENYO_PIPELINE_WIRE private pair separated\n"
  set in_attack.state = 3
  disable 3
  enable 4
  continue
end
disable 3

# Enyo's licensed progs.dat is identified in SV_EnyoAkimboProgramLoaded;
# these statement indices pin the three native hooks inside W_FireSMG.
break sv_phys.c:3424 if qcvm && qcvm->xfunction && qcvm->xfunction->first_statement == 15323 && qcvm->xstatement == 15324 && sv_vr_weapon_pose_scope && sv_vr_weapon_pose_scope->akimbo_pose_valid
commands
  silent
  if !svs.clients[0].cmd.vr_akimbo_active || (int)svs.clients[0].edict->v.weapon != 4
    printf "ENYO_PIPELINE_FAIL server rejected pair\n"
    quit 1
  end
  set $left_dx = sv_vr_weapon_pose_scope->akimbo_muzzle[0][0] - (sv_vr_weapon_pose_scope->body_origin[0] + svs.clients[0].cmd.vr_akimbo_muzzle[0][0])
  set $left_dy = sv_vr_weapon_pose_scope->akimbo_muzzle[0][1] - (sv_vr_weapon_pose_scope->body_origin[1] + svs.clients[0].cmd.vr_akimbo_muzzle[0][1])
  set $left_dz = sv_vr_weapon_pose_scope->akimbo_muzzle[0][2] - (sv_vr_weapon_pose_scope->body_origin[2] + svs.clients[0].cmd.vr_akimbo_muzzle[0][2])
  set $right_dx = sv_vr_weapon_pose_scope->akimbo_muzzle[1][0] - (sv_vr_weapon_pose_scope->body_origin[0] + svs.clients[0].cmd.vr_akimbo_muzzle[1][0])
  set $right_dy = sv_vr_weapon_pose_scope->akimbo_muzzle[1][1] - (sv_vr_weapon_pose_scope->body_origin[1] + svs.clients[0].cmd.vr_akimbo_muzzle[1][1])
  set $right_dz = sv_vr_weapon_pose_scope->akimbo_muzzle[1][2] - (sv_vr_weapon_pose_scope->body_origin[2] + svs.clients[0].cmd.vr_akimbo_muzzle[1][2])
  set $left_error = $left_dx * $left_dx + $left_dy * $left_dy + $left_dz * $left_dz
  set $right_error = $right_dx * $right_dx + $right_dy * $right_dy + $right_dz * $right_dz
  if $left_error > 0.0001 || $right_error > 0.0001
    printf "ENYO_PIPELINE_FAIL world muzzle differs from body plus relative wire\n"
    quit 1
  end
  set $pair_dx = sv_vr_weapon_pose_scope->akimbo_muzzle[0][0] - sv_vr_weapon_pose_scope->akimbo_muzzle[1][0]
  set $pair_dy = sv_vr_weapon_pose_scope->akimbo_muzzle[0][1] - sv_vr_weapon_pose_scope->akimbo_muzzle[1][1]
  set $pair_dz = sv_vr_weapon_pose_scope->akimbo_muzzle[0][2] - sv_vr_weapon_pose_scope->akimbo_muzzle[1][2]
  if $pair_dx * $pair_dx + $pair_dy * $pair_dy + $pair_dz * $pair_dz < 1.0
    printf "ENYO_PIPELINE_FAIL server pair muzzles collapsed\n"
    quit 1
  end
  set $shot_hand = qcvm->globals[7335] == 0.0 ? 1 : 0
  printf "ENYO_PIPELINE_SERVER pair accepted with 3D tolerance\n"
  set $makevectors_ok = 1
  printf "ENYO_PIPELINE_MAKEVECTORS hand=%d\n", $shot_hand
  disable 4
  enable 5
  continue
end
disable 4

break sv_phys.c:3440 if qcvm && qcvm->xfunction && qcvm->xfunction->first_statement == 15323 && qcvm->xstatement == 15344
commands
  silent
  if !$makevectors_ok
    printf "ENYO_PIPELINE_FAIL aim without accepted makevectors\n"
    quit 1
  end
  set $aim_ok = 1
  printf "ENYO_PIPELINE_AIM accepted\n"
  disable 5
  enable 6
  continue
end
disable 5

break sv_phys.c:3470 if qcvm && qcvm->xfunction && qcvm->xfunction->first_statement == 15323 && qcvm->xstatement == 15352
commands
  silent
  if !$aim_ok
    printf "ENYO_PIPELINE_FAIL trace without accepted aim\n"
    quit 1
  end
  if svs.clients[0].edict->v.ammo_nails != $ammo_before - 1
    printf "ENYO_PIPELINE_FAIL trace return ammo delta (before=%f after=%f)\n", $ammo_before, svs.clients[0].edict->v.ammo_nails
    quit 1
  end
  printf "ENYO_PIPELINE_PASSED stage=trace-return hand=%d ammo_before=%f ammo_after=%f\n", $shot_hand, $ammo_before, svs.clients[0].edict->v.ammo_nails
  quit 0
end
disable 6
run
