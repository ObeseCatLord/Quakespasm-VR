import bpy, json, sys, os, glob
here = os.environ['WORKSHOP_WORK']
names = sys.argv[sys.argv.index('--') + 1:]
for name in names:
    d = json.load(open(os.path.join(here, name + '.mesh.json')))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    base = d['frames'][0][1]
    me = bpy.data.meshes.new(name)
    me.from_pydata(base, [], [[c[0] for c in t] for t in d['tris']])
    uvl = me.uv_layers.new(name='UVMap')
    for pi, t in enumerate(d['tris']):
        for k in range(3):
            uvl.data[me.polygons[pi].loop_start + k].uv = d['uvs'][t[k][1]]
    for p in me.polygons: p.use_smooth = False
    me.update()
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    ob.shape_key_add(name='Basis')
    for fname, pos in d['frames']:
        kb = ob.shape_key_add(name=fname)
        for i, p in enumerate(pos): kb.data[i].co = p
    blend = os.path.join(d['dir'], name + '.blend')
    bpy.ops.wm.save_as_mainfile(filepath=blend)
    img = bpy.data.images.load(os.path.join(d['dir'], d['skin'])); img.filepath = '//' + d['skin']
    mat = bpy.data.materials.new(name + '_skin'); mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    tex = nt.nodes.new('ShaderNodeTexImage'); tex.image = img; tex.interpolation = 'Closest'
    nt.links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])
    try: bsdf.inputs['Roughness'].default_value = 1.0
    except Exception: pass
    me.materials.append(mat)
    bpy.context.view_layer.objects.active = ob; ob.select_set(True)
    for fname, _ in d['frames']:
        for kb in me.shape_keys.key_blocks: kb.value = 1.0 if kb.name == fname else 0.0
        bpy.context.view_layer.update()
        bpy.ops.wm.obj_export(filepath=os.path.join(d['dir'], 'frame_%s.obj' % fname), forward_axis='Y', up_axis='Z',
                              export_selected_objects=True, export_uv=True, export_normals=False, export_materials=False,
                              export_triangulated_mesh=True, apply_modifiers=True)
    for kb in me.shape_keys.key_blocks: kb.value = 1.0 if kb.name == d['frames'][0][0] else 0.0
    bpy.ops.wm.save_mainfile()
    for f in glob.glob(os.path.join(d['dir'], '*.blend1')): os.remove(f)
    print('BUILT', name, len(me.polygons), 'tris', len(me.vertices), 'verts')
