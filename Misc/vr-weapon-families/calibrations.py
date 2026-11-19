"""Calibration sources for geometry weapon families.

Coordinates are MDL model space of the reference model's first frame:
x forward, y left, z up. A grip is where the controller holds the mesh; a tip is
the barrel end (or the blade/head for melee). The generator converts a grip to
the renderer's held offset with held = -scale*grip - (1-scale)*origin.

Tuned rows are existing hand calibrations (built-in presets) re-expressed as
families. Picks were chosen from ready-frame wireframes in side/top/rear views:
grips on a modelled hand or handle when present, otherwise the stock
convention on the gun body; tips on the barrel end centre.
"""
from collections import namedtuple

Tuned = namedtuple('Tuned', 'label mod path held scale muzzle')
Pick = namedtuple('Pick', 'mod path grip held scale tip muzzle kind', defaults=(None,) * 8)


def _t(label, mod, rows, muzzles=False):
    out = []
    for row in rows:
        if muzzles:
            path, held, scale, muzzle = row
        else:
            (path, held, scale), muzzle = row, None
        out.append(Tuned(label, mod, path if path.startswith('progs/') else f'progs/{path}.mdl', held, scale, muzzle))
    return out


# Stock meshes keep the user-selectable path presets; only their barrel tips are
# used (tip-only families).
STOCK = (
    _t('stock', 'id1', [('v_axe', (-4, 24, 37), .33), ('v_shot', (1.5, 1, 10), .5), ('v_shot2', (-3.5, 1, 8.5), .8),
                        ('v_nail', (-5, 3, 15), .5), ('v_nail2', (0, 3, 19), .5), ('v_rock', (10, 1.5, 13), .5),
                        ('v_rock2', (10, 7, 19), .5), ('v_light', (3, 4, 13), .5)]) +
    _t('stock', 'hipnotic', [('v_hammer', (-4, 17.5, 36), .33), ('v_laserg', (65, 3.7, 15), .33), ('v_prox', (10, 1.5, 13), .5)]) +
    _t('stock', 'rogue', [('v_lava', (-5, 3, 15), .5), ('v_lava2', (0, 3, 19), .5), ('v_multi', (10, 1.5, 13), .5),
                          ('v_multi2', (10, 7, 19), .5), ('v_plasma', (3, 4, 13), .5)]))
STOCK_TIP_OVERRIDE = {'progs/v_rock2.mdl': (49.0, 0.0, -15.0)}  # barrel face centre, not its lower corner

# Earlier rows win when two calibrations share a mesh.
TUNED = (
    _t('AD', 'ad', [('v_shadaxe0', (-1.5, 43.1, 41), .25), ('v_shot3', (-3.5, .4, 8.5), .8), ('v_shot', (1.5, 1.7, 17.5), .33),
                    ('v_shot2', (-3.5, .4, 8.5), .8), ('v_nail', (-9.5, 3, 17), .5), ('v_nail2', (-6, 3.5, 20), .4),
                    ('v_rock', (-3, 1.25, 17), .5), ('v_rock2', (0, 5.55, 22.5), .45), ('v_light', (-4, 3.1, 13), .5),
                    ('v_plasma', (2.8, 1.8, 22.5), .5)]) +
    _t('alk', 'alk', [('v_saw', (-6.5, 27.5, 42), .25), ('v_plasma', (16, 6, 16), .4), ('v_shot40fps', (1.5, 1.8, 15.8), .33),
                      ('v_shot2_40fps', (-3, 1.7, 12), .5), ('v_nail_alk40fps', (-6, 3.8, 17), .38), ('v_nail3', (-4, 3.5, 19), .35),
                      ('v_rock_40fps', (-3, 1.25, 17), .5), ('v_rock2_40fps', (20, 8.3, 21), .38), ('v_laserg40fps', (45, 3, 15), .22),
                      ('v_mine_40fps', (-3, 1.3, 15), .5)]) +
    _t('alkaxe', 'alk', [('v_alkaxe20fps', (12, 54, 39.5), .25)]) +
    _t('copper', 'bonkjam', [('v_axe2', (-3.5, 34, 41.5), .33)]) +
    _t('enyo', 'enyo', [('ee_v_sword', (25, 49, 60), .2), ('ee_v_pistol', (12, 24, 29), .2), ('ee_v_sgun', (-2.3, 21.3, 35.3), .2),
                        ('ee_v_smgs', (3.5, 24.6, 29.8), .2), ('ee_v_plasma', (-1.5, 21.8, 36), .2), ('ee_v_glaunch', (-3.8, 24, 35.5), .2),
                        ('ee_v_rlaunch', (4, 28.5, 40.5), .2), ('ee_v_railgun', (-1, 22.5, 34.5), .2), ('ee_v_av72', (.5, 24, 38.5), .2),
                        ('ee_v_legal', (0, 55, 29), .2)]) +
    _t('snack', 'snack3', [('v_nail', (-11.88, 32.99599, 113.865081), .11, (-2.493333, 4.4, 7.773333)),
                           ('v_nail2', (0.721246, 74.173819, 139.216208), .11, (0.0, 2.493333, 17.16))], True) +
    _t('qbj3', 'qbj3', [('v_wrench', (-5.090864, 45.71518, 64.70464), .2, (0, 0, 0)),
                        ('v_pistol', (3.388845, 37.75988, 56.43581), .2, (-9.11632, 9.013277, -45.533)),
                        ('v_flakshotgun', (11.55282, 16.95288, 38.90591), .2, (.1453177, 2.258818, -31.45429)),
                        ('v_tnailgun', (-3.596274, 22.3977, 49.35181), .2, (-4.46999, 4.069127, -25.89618)),
                        ('v_rebar', (7.11163, 33.89061, 52.88078), .2, (.4432641, 12.53425, 4.832447)),
                        ('v_grenlauncher', (3.906817, 19.53125, 46.88503), .2, (-.9725167, 6.330487, 6.072494)),
                        ('v_mmml', (8.254588, 17.25331, 53.56533), .2, (-.387794, 15.84945, -4.354416)),
                        ('v_invoker', (-.4811821, 24.50682, 24.40611), .2, (0, 0, 0)),
                        ('v_berserk', (3.20228348, 45.25361633, 34.16704407), .2, (0, 0, 0))], True) +
    _t('peril', 'peril3.0', [
        ('v_shadaxe0', (-2.875082, 42.074010, 37.998579), .25, (0, 0, 0)), ('v_shadaxe3', (26.605468, 74.755644, 42.675916), .25, (0, 0, 0)),
        ('v_ghook', (0.398149, 3.974375, 20.716595), .5, (0, .466667, 9.333333)), ('v_shot', (-5.473787, 1.827324, 16.828860), .5, (.666667, 0, 5.266667)),
        ('v_shot2', (-1.245300, 5.547600, 18.678106), .5, (2.833333, 2.2, 7.666667)), ('v_shot3', (-3.5769, 7.645268, 20.651891), .5, (2.8, -.133333, 10.266667)),
        ('v_nail', (-10.759785, 16.344555, 22.411512), .5, (-1.992039, 6.550682, 11.076865)), ('v_nail2', (-2.792916, 12.398437, 17.963036), .5, (-5.573333, 1.466667, 8.266667)),
        ('v_rock', (12.130318, 4.749184, 14.611477), .5, (0, 1.066667, 8)), ('v_rock2', (-8.031301, 10.252375, 13.595063), .5, (-4.666667, .733333, 6.333333)),
        ('v_light', (-12, 9.519683, 20.873978), .5, (-1.666667, 6.666667, 8.666667)), ('v_plasma', (-11, 3.594348, 18.724479), .5, (0, 2.333333, 11.333333)),
        ('v_axe', (13.807513, 17.004711, 45.156413), .33, (0, 0, 0)), ('v_nail3', (-10.8, 13.374923, 20.290589), .4, (-2.666667, 3.2, 17.066667)),
        ('v_rock3', (-4.330201, 4.048523, 17.036097), .5, (0, 2, 7.333333)), ('v_zershot', (2.3527, 2.125, 17.102712), .5, (-.666667, 4, 4.666667))], True))
NO_TIP = {'progs/ee_v_legal.mdl'}  # not a barrelled mesh

# Landmark picks for meshes without any previous calibration. A pick with
# `held` keeps a mod's own saved vradjustweapon grip as the family grip.
PICKS = [
    Pick('Tershibboleth', 'progs/v_laserb.mdl', grip=(14, 0, -11), scale=0.5, tip=(26, 0, -12), kind='gun'),  # drake laserb/plasma
    Pick('Tershibboleth', 'progs/v_light2.mdl', grip=(13, 0, -10), scale=0.5, tip=(30, 0, -11), kind='gun'),  # drake light2
    Pick('Tershibboleth', 'progs/v_rocks.mdl', grip=(15.4, 0.1, -9.4), scale=0.5, tip=(23.8, 0, -10.6), kind='gun'),  # drake rocks (stock rock shape)
    Pick('Tershibboleth', 'progs/v_saw.mdl', grip=(19, -8, -16), scale=0.33, tip=(51, 0.5, -10), kind='melee'),  # zer/drake chainsaw
    Pick('Tershibboleth', 'progs/v_wand.mdl', grip=(3, 0, -10), scale=0.5, tip=(22, 0, -10), kind='gun'),  # drake wand
    Pick('Tershibboleth', 'progs/v_xbow.mdl', grip=(17, 0, -13.5), scale=0.5, tip=(39, 0, -12.5), kind='gun'),  # drake crossbow
    Pick('dwellv2p2', 'progs/v_shot2.mdl', held=(-1.398206, 2.104362, 13.50095), scale=0.5333333, tip=(23.5, 0, -13.2), muzzle=None, kind='gun'),  # dwell/ad171 shot2 (dwell vradjust)
    Pick('dwellv2p2', 'progs/v_shot3.mdl', held=(0.9671097, 2.64044, 16.54686), scale=0.5333333, tip=(37, 0, -16), muzzle=None, kind='gun'),  # dwell/snack shot3 (dwell vradjust)
    Pick('immortal', 'progs/v_bomb.mdl', grip=(37, -11, -21), scale=0.33, tip=(38, 0, -17), kind='melee'),  # quoth bomb (right hand)
    Pick('lq1', 'progs/v_nail.mdl', grip=(11.5, 0, -19), scale=0.5, tip=(36, 0, -14), kind='gun'),  # lq1 nail
    Pick('lq1', 'progs/v_shot2.mdl', grip=(-0.5, 0, -10.5), scale=0.75, tip=(16.5, 0, -8.3), kind='gun'),  # lq1 shot2
    Pick('lq1', 'progs/v_axe.mdl', grip=(21, -11, -30), scale=0.33, tip=(45, -1, -18), kind='melee'),  # lq1 axe
    Pick('lq1', 'progs/v_rock.mdl', grip=(3.5, 0, -16.5), scale=0.5, tip=(28.5, 0, -14), kind='gun'),  # lq1 rock
    Pick('lq1', 'progs/v_nail2.mdl', grip=(-1.5, 0, -21), scale=0.5, tip=(42, 0, -16.3), kind='gun'),  # lq1 nail2
    Pick('lq1', 'progs/v_rock2.mdl', grip=(4.5, 0, -23), scale=0.5, tip=(44.5, 0, -16), kind='gun'),  # lq1 rock2
    Pick('lq1', 'progs/v_light.mdl', grip=(1, 0, -15.5), scale=0.5, tip=(29.5, 0, -11.8), kind='gun'),  # lq1 light
    Pick('lq1', 'progs/v_shot.mdl', grip=(0.5, 0, -11), scale=0.5, tip=(22, 0, -8.7), kind='gun'),  # lq1 shot
    Pick('mg3', 'progs/v_bloodshot.mdl', grip=(11.4, -0.2, -7.7), scale=0.5, tip=(21, 0, -9.8), kind='gun'),  # mg3 bloodshot (stock shot shape)
    Pick('mg3', 'progs/v_bloodshot2.mdl', grip=(8, 0, -7.5), scale=0.8, tip=(13.8, 0, -7.8), kind='gun'),  # mg3 bloodshot2
    Pick('mg3', 'progs/v_hammer.mdl', grip=(20, -11, -29.5), scale=0.33, tip=(38, -5, -12), kind='melee'),  # mg3 hammer
    Pick('mg3', 'progs/v_laserg.mdl', held=(56.79274, 4.62593, 17.5531), scale=0.33, tip=None, muzzle=(2.626634, 8.260719, 3.612768), kind='gun'),  # mg3 laserg (authored incl. muzzle)
    Pick('mjolnir', 'progs/v_lava2.mdl', grip=(16, 0, -16), scale=0.5, tip=(36, 0, -16.6), kind='gun'),  # mjolnir lava2
    Pick('mjolnir', 'progs/v_axe.mdl', grip=(21.5, -12.3, -25.7), scale=0.33, tip=(40, -2, -14), kind='melee'),  # mjolnir axe (stock-like)
    Pick('mjolnir', 'progs/v_lava.mdl', grip=(19, -0.1, -14.7), scale=0.5, tip=(30.5, 0, -13.7), kind='gun'),  # mjolnir lava (AD nail shape)
    Pick('mjolnir', 'progs/v_multi.mdl', grip=(12.3, 0, -11.7), scale=0.5, tip=(23.5, 0, -11), kind='gun'),  # mjolnir multi/prox (AD rock shape)
    Pick('mjolnir', 'progs/v_multi2.mdl', grip=(19, 0, -14.2), scale=0.45, tip=(49, 0, -15), kind='gun'),  # mjolnir multi2 (AD rock2 shape)
    Pick('mjolnir', 'progs/v_plasma2.mdl', grip=(12, 0, -12.5), scale=0.5, tip=(27.5, 0, -11.3), kind='gun'),  # mjolnir plasma2
    Pick('mjolnir', 'progs/airfist/v_airgun.mdl', grip=(2, 0, -20), scale=0.5, tip=(23, 0, -20), kind='gun'),  # airgun
    Pick('mjolnir', 'progs/aoa/v_flak.mdl', grip=(9, -6, -18.5), scale=0.5, tip=(34, -6, -12.2), kind='gun'),  # aoa flak
    Pick('mjolnir', 'progs/aoa/v_gungnir.mdl', grip=(0, -5.7, -11.3), scale=0.4, tip=(25, -4, -3), kind='melee'),  # aoa gungnir spear
    Pick('mjolnir', 'progs/aoa/v_mace.mdl', grip=(28, -12, -31), scale=0.33, tip=(29, -11, 0), kind='melee'),  # aoa mace
    Pick('mjolnir', 'progs/aoa/v_rapier.mdl', grip=(26, -4.7, -19.5), scale=0.4, tip=(26, -4.5, 7), kind='melee'),  # aoa rapier
    Pick('mjolnir', 'progs/aoa/v_sanguinators.mdl', grip=(17, -14, -15), scale=0.33, tip=(24, -13, -11), kind='melee'),  # aoa sanguinators (right claw)
    Pick('mjolnir', 'progs/aoa/v_scimitar.mdl', grip=(8, -15.5, -14), scale=0.4, tip=(27, -14, 8), kind='melee'),  # aoa scimitar
    Pick('mjolnir', 'progs/aoa/v_voidstaff.mdl', grip=(14, -10, -14.5), scale=0.5, tip=(29, -10, -10), kind='gun'),  # aoa voidstaff
    Pick('mjolnir', 'progs/its/v_crossbow1.mdl', grip=(-6, 0, -10.5), scale=0.5, tip=(25, 0, -11), kind='gun'),  # its crossbow
    Pick('mjolnir', 'progs/violentrumble/v_hammer.mdl', grip=(20, -12, -30), scale=0.33, tip=(35, -5, -12), kind='melee'),  # VR hammer
    Pick('mjolnir', 'progs/violentrumble/v_hammerpw.mdl', grip=(20, -12, -30), scale=0.33, tip=(35, -5, -12), kind='melee'),  # VR hammer powered
    Pick('mjolnir', 'progs/q15/v_saw.mdl', grip=(33, -4, -26), scale=0.25, tip=(92, -11, -20), kind='melee'),  # q15 saw
    Pick('ravenkeep', 'progs/v_light.mdl', held=(4.148245, 5.491152, 13.58594), scale=0.5, tip=(32, 0, -12), muzzle=None, kind='gun'),  # redfield light (ravenkeep vradjust)
    Pick('mjolnir', 'progs/madfox/mf_v_rock.mdl', grip=(8, 0, -12.5), scale=0.5, tip=(31, 0, -12.6), kind='gun'),  # madfox rock
    Pick('ravenkeep', 'progs/v_nail2.mdl', held=(-6.271667, 2.77875, 16.83761), scale=0.5, tip=(41.5, 0, -16.5), muzzle=None, kind='gun'),  # redfield nail2
    Pick('ravenkeep', 'progs/v_shot.mdl', held=(0.09599209, 1.578824, 14.47469), scale=0.5, tip=(33, 0, -15.3), muzzle=None, kind='gun'),  # redfield shot
    Pick('ravenkeep', 'progs/v_shot2.mdl', held=(-7.368657, 0.06993484, 6.730382), scale=1.0, tip=(14, 0, -8), muzzle=None, kind='gun'),  # redfield shot2
    Pick('reliq', 'progs/v_shot.mdl', grip=(16, -6, -15), scale=0.5, tip=(27.5, -5.2, -8.2), kind='gun'),  # reliq shot
    Pick('tombofthunder', 'progs/v_chain.mdl', grip=(14, 0, -15), scale=0.5, tip=(29.5, 0, -15.5), kind='gun'),  # tot chain
    Pick('tombofthunder', 'progs/v_rail.mdl', grip=(2, -2, -18), scale=0.5, tip=(49, 0, -16), kind='gun'),  # tot rail
    Pick('tombofthunder', 'progs/v_shotcl.mdl', grip=(14, 0, -20.5), scale=0.5, tip=(35.5, 0, -16.4), kind='gun'),  # tot shotcl
    Pick('mjolnir', 'progs/violentrumble/v_grpple0.mdl', grip=(18, 0, -17), scale=0.5, tip=(37, 0, -16.5), kind='gun'),  # VR grapple
    Pick('mjolnir', 'progs/violentrumble/v_laserr.mdl', grip=(14, 0, -15), scale=0.5, tip=(35.5, 0, -14.2), kind='gun'),  # VR laserr
    Pick('mjolnir', 'progs/violentrumble/v_multiprox.mdl', grip=(14, 0, -10), scale=0.5, tip=(23, 0, -10.7), kind='gun'),  # VR multiprox
    Pick('mjolnir', 'progs/violentrumble/v_railg.mdl', grip=(14, 0, -15), scale=0.5, tip=(35.5, 0, -14.2), kind='gun'),  # VR railg
    Pick('warpspasm', 'progs/v_ham.mdl', grip=(20, -11, -27), scale=0.33, tip=(29, -14, 24), kind='melee'),  # warpspasm sword (v_ham)
    Pick('mjolnir', 'progs/zerstorer/v_cgun.mdl', grip=(9, 0, -7.5), scale=0.8, tip=(16, 0, -7.6), kind='gun'),  # zer cgun
    Pick('mjolnir', 'progs/travail/v_dshot.mdl', grip=(6, 0, -9.5), scale=0.7, tip=(19.5, 0, -9), kind='gun'),  # zer dshot
    Pick('mjolnir', 'progs/zerstorer/v_light2.mdl', grip=(10, 0, -12.5), scale=0.5, tip=(28, 0, -12), kind='gun'),  # zer light2
    Pick('mjolnir', 'progs/zerstorer/v_stun.mdl', grip=(12, -2, -11), scale=0.7, tip=(20.5, 0, -7.5), kind='gun'),  # zer stun
    Pick('dwellv2p2', 'progs/v_axeb.mdl', grip=(26, -13, -32), scale=0.4, tip=(42, -14, -15), kind='melee'),  # dwell v_axeb (right axe of the pair)
    Pick('dwellv2p2', 'progs/v_rail.mdl', grip=(17, 0, -17), scale=0.65, tip=(38, 0, -12), kind='gun'),  # dwell v_rail
]
