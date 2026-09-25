# Live QBJ3 Flak wrist-roll path with simulated tracked OpenXR. The installed
# vr_weapons.txt must be mounted so the non-stock Flak has a calibrated muzzle.
# This checks the physical spread basis while QC's camera roll stays zero;
# projectile impact and damage are separate qualification work.
set pagination off
set confirm off
set debuginfod enabled off
set $granted = 0
set $commands = 0
break V_PrepareAkimboPair if cl.viewent.model != 0
commands
  silent
  if (int)strcmp(cl.viewent.model->name, "progs/v_flakshotgun.mdl") == 0
    printf "FLAK_READY\n"
    disable 1
    enable 2
    continue
  end
  if !$granted
    set $granted = 1
    set svs.clients[0].edict->v.items = (int)svs.clients[0].edict->v.items | 2
    set svs.clients[0].edict->v.ammo_shells = 100
    set svs.clients[0].edict->v.weapon = 2
    call (void)PR_SwitchQCVM(&sv.qcvm)
    set svs.clients[0].edict->v.weaponmodel = PR_SetEngineString("progs/v_flakshotgun.mdl")
    call (void)PR_SwitchQCVM(0)
    set in_impulse = 3
  end
  continue
end
break CL_WritePrivateUsercmd if cmd->vr_active && cl.viewent.model && (int)strcmp(cl.viewent.model->name, "progs/v_flakshotgun.mdl") == 0
commands
  silent
  set cmd->vr_handrot[0] = 0
  set cmd->vr_handrot[1] = 0
  set cmd->vr_handrot[2] = 45
  set in_attack.state = 3
  set $commands = $commands + 1
  if $commands == 1
    printf "FLAK_WIRE roll=%f\n", cmd->vr_handrot[2]
    enable 3
  end
  continue
end
disable 2
break sv_phys.c:3329
commands
  silent
  if (int)strcmp(PR_GetString(qcvm->xfunction->s_name), "W_FireFlakShotgun") != 0
    printf "FLAK_FAIL wrong QuakeC firing function\n"
    quit 1
  end
  printf "FLAK_BASIS roll=%f qc_roll=%f right_z=%f up_z=%f weapon=%d\n", sv_vr_weapon_pose_scope->qbj3_shotgun_roll, svs.clients[0].edict->v.v_angle[2], pr_global_struct->v_right[2], pr_global_struct->v_up[2], (int)svs.clients[0].edict->v.weapon
  if sv_vr_weapon_pose_scope->qbj3_shotgun_roll != 45 || svs.clients[0].edict->v.v_angle[2] != 0 || pr_global_struct->v_right[2] > -0.6 || pr_global_struct->v_right[2] < -0.8
    printf "FLAK_FAIL incorrect physical roll basis\n"
    quit 1
  end
  printf "FLAK_ROLL_PASSED\n"
  quit 0
end
disable 3
run
