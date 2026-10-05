set pagination off
set confirm off
set debuginfod enabled off
set python print-stack full
break SV_Physics
run
python
import gdb, json, re

# The integrated binary need not have -g3 macro debug information. Sizes come
# from the compiled types; protocol IDs below are pinned in protocol.h and
# quakedef.h, not additional wire fields.
constants = {
    'MAX_CL_STATS': int(gdb.parse_and_eval('sizeof(cl.stats)/sizeof(cl.stats[0])')),
    'MAX_MODELS': int(gdb.parse_and_eval('sizeof(cl.model_precache)/sizeof(cl.model_precache[0])')),
    'SIGNONS': 4, 'PEXT2_REPLACEMENTDELTAS': 8, 'PEXT2_PREDINFO': 32,
    'STAT_VR_WEAPONS': 224, 'STAT_VR_MODITEMS': 231,
    'STAT_VR_MAX_SHELLS': 234, 'STAT_VR_MAX_NAILS': 235,
    'STAT_VR_MAX_ROCKETS': 236, 'STAT_VR_MAX_CELLS': 237,
    'VR_WEAPON_SCHEMA_MAX_ENTRIES': 64,
}
def expand(s):
    return re.sub(r'\b[A-Z][A-Z0-9_]*\b', lambda m: str(constants.get(m[0],m[0])), s)

def ev(s): return gdb.parse_and_eval(expand(s))
def cmd(s): return gdb.execute(expand(s), to_string=True)
def put(s, v): cmd('set '+s+' = '+str(v))
def field(name): return 'GetEdictFieldValueByName($p,%s)->_float'%json.dumps(name)
def glob(name): return 'qcvm->globals[ED_FindGlobal(%s)->ofs]'%json.dumps(name)
def qc(name):
    put('pr_global_struct->self', '((int)((byte *)$p - (byte *)qcvm->edicts))')
    put('pr_global_struct->time', 'qcvm->time')
    put('*(int *)&qcvm->globals[4]', '((int)((byte *)$p - (byte *)qcvm->edicts))')
    cmd('call PR_ExecuteProgram(ED_FindFunction(%s) - qcvm->functions)'%json.dumps(name))

assert int(ev('SV_PerilAkimboProgramLoaded()')) == 1, 'wrong effective installed QC'
put('$client', '&svs.clients[0]'); put('$p', 'EDICT_NUM(1)')
for name, value in [('active',1),('spawned',1),('knowntoqc',1),('edict','$p'),
                    ('message.data','$client->msgbuf'),('message.maxsize','sizeof($client->msgbuf)'),
                    ('datagram.data','$client->datagram_buf'),('datagram.maxsize','sizeof($client->datagram_buf)'),
                    ('limit_models','MAX_MODELS'),('protocol_pext2','PEXT2_REPLACEMENTDELTAS | PEXT2_PREDINFO')]:
    put('$client->'+name,value)
qc('PutClientInServer')
for name in ['intermission_running','cinematic_running']: put(glob(name),0)
put('$p->v.button0',0); put('$p->v.health',100)
put('cls.state','ca_connected'); put('cls.signon','SIGNONS')
put('key_dest','key_game'); put('con_forcedup',0)
put('cl.worldmodel','sv.models[1]'); put('cl.viewentity',1); put('cl.intermission',0)
put('vid.width',1280); put('glwidth',1280); put('vid.height',720); put('glheight',720)
put('vulkan_globals.stereo_active',0)
put('cl.protocol_pext2','PEXT2_REPLACEMENTDELTAS | PEXT2_PREDINFO')
cmd('call (void)strcpy(cl.mapname,"start")')
for i in range(int(ev('MAX_MODELS'))):
    if not int(ev('sv.model_precache[%d]'%i)): break
    put('cl.model_precache[%d]'%i, 'sv.models[%d]'%i)
cmd('call VR_WeaponMenu_ReloadGame()')
assert int(ev('vr_weapon_menu_wwheel_catalog.count')) == 8
assert int(ev('vr_weapon_menu_has_schema')) == 0, 'loose calibration copy became wheel authoring'

# Actual native producer -> existing FTE stat writer -> actual client parser.
# This is an isolated in-process wire roundtrip, not a live connected peer.
put('$delta','(struct deltaframe_s *)calloc(1,sizeof(struct deltaframe_s))')
put('$wire','(byte *)calloc(1,65536)')
put('$msg','(sizebuf_t *)calloc(1,sizeof(sizebuf_t))')
put('$si','(int *)calloc(MAX_CL_STATS,sizeof(int))')
put('$sf','(float *)calloc(MAX_CL_STATS,sizeof(float))')
put('$ss','(const char **)calloc(MAX_CL_STATS,sizeof(char *))')
put('$msg->data','$wire'); put('$msg->maxsize',65536)
def transport():
    cmd('call SV_CalcStats($client,$si,$sf,$ss)')
    assert int(ev('$si[STAT_VR_MODITEMS]')) == int(ev(field('moditems')))
    assert [int(ev('$si[%s]'%n)) for n in ['STAT_VR_MAX_SHELLS','STAT_VR_MAX_NAILS','STAT_VR_MAX_ROCKETS','STAT_VR_MAX_CELLS']] == [200,200,100,100]
    assert int(ev('$si[STAT_VR_WEAPONS]')) == 0, 'invented weapons field'
    put('$msg->cursize',0)
    cmd('call SVFTE_WriteStats($client,$msg,$delta)')
    put('net_message','*$msg')
    cmd('call CL_ParseServerMessage()')
    for n in ['STAT_ITEMS','STAT_VR_MODITEMS','STAT_VR_MAX_SHELLS','STAT_VR_MAX_NAILS','STAT_VR_MAX_ROCKETS','STAT_VR_MAX_CELLS']:
        assert int(ev('cl.stats[%s]'%n)) == int(ev('$si[%s]'%n)), 'transport mismatch '+n
    for n, f in [('STAT_ACTIVEWEAPON','weapon'),('STAT_SHELLS','ammo_shells'),('STAT_NAILS','ammo_nails'),('STAT_ROCKETS','ammo_rockets'),('STAT_CELLS','ammo_cells')]:
        assert int(ev('cl.stats[%s]'%n)) == int(ev('$p->v.'+f)), 'client stat mismatch '+n
    assert int(ev('cl.items')) == int(ev('cl.stats[STAT_ITEMS]'))
    assert int(ev('cl.stats[STAT_WEAPON]')) == int(ev('$si[STAT_WEAPON]')) > 0

ALL = 4096 | 127
def state(selector=4096, modifiers=0, items=ALL, shells=20, nails=20):
    for f,v in [('weapon',selector),('items',items),('ammo_shells',shells),('ammo_nails',nails),('ammo_rockets',20),('ammo_cells',20),('impulse',0),('button0',0)]: put('$p->v.'+f,v)
    put(field('moditems'),modifiers)
    qc('W_SetCurrentAmmo')
    transport()

def row(selector):
    for i in range(int(ev('vr_weapon_menu_wwheel_catalog.count'))):
        path='vr_weapon_menu_wwheel_entries[%d]'%i
        if int(ev(path+'.selector')) == selector and int(ev(path+'.game_profile')):
            return path
    raise AssertionError('missing parent slot '+str(selector))

def release(selector):
    cmd('call VR_WeaponMenu_Open()')
    assert int(ev('VR_WeaponMenu_IsOpen()')) == 1
    # First update prepares the real visible boxes; choose the requested box,
    # then run the same absolute pointer setter again to resolve real hover.
    cmd('call VR_WeaponMenu_SetDesktopPointer(1,640,360)')
    box=None
    for i in range(int(ev('vr_weapon_menu_frame.count'))):
        r='vr_weapon_menu_frame.visible[%d]'%i
        if int(ev(r+'.entry->selector')) == selector:
            box=(int(ev(r+'.center_x')),int(ev(r+'.center_y')))
            break
    if box:
        cmd('call VR_WeaponMenu_SetDesktopPointer(1,%d,%d)'%box)
    # Dedicated SDL has no game window. Preserve this policy fixture's supplied
    # pointer; public Release's actual SDL sampling belongs to the desktop test.
    impulse=int(ev('VR_WeaponMenu_ReleaseCatalog(VR_WeaponMenu_CurrentCatalog(),cl.stats,MAX_CL_STATS,cl.items)'))
    assert int(ev('VR_WeaponMenu_IsOpen()')) == 0
    return impulse

def select(selector, impulse, model):
    before=[float(ev('$p->v.'+n)) for n in ['ammo_shells','ammo_nails','ammo_rockets','ammo_cells']]
    got=release(selector); assert got == impulse, ('wheel release',selector,got,impulse)
    put('$p->v.impulse',got)
    qc('W_WeaponFrame') # Native ImpulseCommands -> W_ChangeWeapon -> W_SetCurrentAmmo.
    assert int(ev('$p->v.weapon')) == selector
    assert ev('PR_GetString($p->v.weaponmodel)').string() == model
    assert int(ev('$p->v.impulse')) == 0
    assert before == [float(ev('$p->v.'+n)) for n in ['ammo_shells','ammo_nails','ammo_rockets','ammo_cells']]
    transport()
    cmd('call VR_WeaponMenu_ObserveActive()')
    assert int(ev('vr_weapon_menu_wwheel_catalog.count')) == 8
    assert int(ev('VR_WeaponMenu_EntryActive(&%s,cl.stats,MAX_CL_STATS)'%row(selector))) == 1

base=[(4096,1,'v_shadaxe0'),(1,2,'v_shot'),(2,3,'v_shot2'),(4,4,'v_nail'),
      (8,5,'v_nail2'),(16,6,'v_rock'),(32,7,'v_rock2'),(64,8,'v_light')]
for selector,impulse,model in base:
    state(1 if selector==4096 else 4096)
    select(selector,impulse,'progs/'+model+'.mdl')
print('PERIL_WHEEL_NATIVE_BASE_PASS all eight releases select native QC held models; transport/ownership/capacities/ammo unchanged')

for selector,mask,impulse,base_model,up_model in [(4096,4096,1,'v_shadaxe0','v_shadaxe3'),
        (4096,128,1,'v_shadaxe0','v_ghook'),(2,2,3,'v_shot2','v_shot3'),(64,64,8,'v_light','v_plasma')]:
    for modifiers,model in [(0,base_model),(mask,up_model),(0,base_model)]:
        state(1 if selector==4096 else 4096,modifiers)
        select(selector,impulse,'progs/'+model+'.mdl')
state(1,4096|128); select(4096,1,'progs/v_ghook.mdl')
print('PERIL_WHEEL_NATIVE_UPGRADES_PASS Shadow/Grapple/Widowmaker/Plasma base->upgrade->base; hook priority; no extra selectors')

for selector,mask,impulse in [(4096,4096,1),(4096,128,1),(2,2,3),(64,64,8)]:
    initial=1
    state(initial,mask,ALL & ~selector)
    assert release(selector)==0, 'modifier alone created wheel ownership'
    put('$p->v.impulse',impulse); qc('W_WeaponFrame')
    assert int(ev('$p->v.weapon')) == initial, 'native parent ownership gate bypassed'
for selector,impulse,model,fieldname in [(2,3,'v_shot2','ammo_shells'),(8,5,'v_nail2','ammo_nails')]:
    for ammo in [0,1,2]:
        state(4096,0,shells=ammo if selector==2 else 20,nails=ammo if selector==8 else 20)
        if ammo<2:
            assert release(selector)==0
            put('$p->v.impulse',impulse); qc('W_WeaponFrame')
            assert int(ev('$p->v.weapon'))==4096
        else: select(selector,impulse,'progs/'+model+'.mdl')
state(4096,2,shells=2); select(2,3,'progs/v_shot3.mdl')
print('PERIL_WHEEL_NATIVE_GATES_PASS parent items required; SSG/SNG min=2, Widowmaker two-shell admission')

# Explicit real schema overrides retain priority. A native alias impulse is
# chosen so both the declared command and native result can be qualified.
state(4096,2)
schema='{bitmask 2 impulse 8 model progs/g_plasma.mdl viewmodel progs/v_plasma.mdl ammo_stat 9 ammo_max 17}'
put('$rows','(vr_weapon_schema_entry_t *)calloc(VR_WEAPON_SCHEMA_MAX_ENTRIES,sizeof(vr_weapon_schema_entry_t))')
put('$count','(size_t *)calloc(1,sizeof(size_t))')
assert int(ev('VR_WeaponSchemaParse(%s,$rows,VR_WEAPON_SCHEMA_MAX_ENTRIES,$count)'%json.dumps(schema)))==1
cmd('call VR_WeaponMenu_ApplySchema($rows,*$count)')
assert int(ev(row(2)+'.impulse'))==8
assert ev('VR_WeaponMenu_EntryPreviewPath(&%s,cl.stats,MAX_CL_STATS)'%row(2)).string()=='progs/g_plasma.mdl'
assert ev('VR_WeaponMenu_EntryViewmodel(&%s,cl.stats,MAX_CL_STATS)'%row(2)).string()=='progs/v_plasma.mdl'
assert int(ev('VR_WeaponMenu_EntryIcon(&%s,cl.stats,MAX_CL_STATS)'%row(2)))==0
assert release(2)==8
put('$p->v.impulse',8); qc('W_WeaponFrame')
assert int(ev('$p->v.weapon'))==64, 'declared impulse must reach native QC unchanged'
assert ev('PR_GetString($p->v.weaponmodel)').string()=='progs/v_light.mdl', 'wheel preview must not replace native held-model policy'
transport()
print('PERIL_WHEEL_NATIVE_OVERRIDES_PASS explicit impulse/model/viewmodel/ammo override precedence and native command result')
for pointer in ['$rows','$count','$delta','$wire','$msg','$si','$sf','$ss']: cmd('call (void)free('+pointer+')')
print('PERIL_WHEEL_NATIVE_PASS actual effective QC selection and producer/writer/client-parser stat transport')
end
quit
