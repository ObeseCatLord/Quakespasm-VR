"""Alicia-only visual spike. Run with Blender --background --factory-startup --python.
Arguments after --: source.vrm new-package-directory. No source assets bundled.
"""
import bpy, numpy as np, json, struct, sys, pathlib, hashlib
from mathutils import Matrix, Vector
source, output = map(pathlib.Path, sys.argv[sys.argv.index('--')+1:])
EXPECTED='237bb02efadf8c13a114af91dd8e860173081457dee87017e51011c448d05dc2'
raw=source.read_bytes(); assert hashlib.sha256(raw).hexdigest()==EXPECTED, 'This spike only accepts the inspected Alicia file'
assert not output.exists(), 'Use a new output directory'
output.mkdir(parents=True)
n=struct.unpack_from('<I',raw,12)[0];d=json.loads(raw[20:20+n]);buf=raw[n+28:]
def acc(i):
 a=d['accessors'][i];v=d['bufferViews'][a['bufferView']];assert not a.get('sparse');k={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']];dt={5126:'<f4',5123:'<u2',5125:'<u4'}[a['componentType']];s=np.dtype(dt).itemsize
 return np.ndarray((a['count'],k),dtype=dt,buffer=buf,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',k*s),s)).copy()
parents={c:i for i,node in enumerate(d['nodes']) for c in node.get('children',[])};world={}
def glob(i):
 if i not in world:
  node=d['nodes'][i];assert node.get('scale',[1,1,1])==[1,1,1]
  from mathutils import Quaternion
  x,y,z,w=node.get('rotation',[0,0,0,1]);m=Quaternion((w,x,y,z)).to_matrix().to_4x4();m.translation=node.get('translation',[0,0,0]);world[i]=glob(parents[i])@m if i in parents else m
 return world[i]
# One rigid axis conversion, preserving original joint-local axes.
Q=Matrix(((0,0,-1,0),(-1,0,0,0),(0,1,0,0),(0,0,0,1)))
binds=[]
for i in range(len(d['nodes'])):
 m=Q@glob(i);m.translation*=32;binds.append(m)
assert all(parents.get(i,-1)<i for i in range(len(binds)))
# Fixed atlas layout: opaque and blended uses get separate tiles even when
# they share an image. Opaque tiles force alpha=1; blended tiles retain it.
slots={(2,False):(0,0,1024),(0,False):(1024,0,512),(4,False):(1536,0,512),(5,False):(1024,512,512),(6,True):(1536,512,512),(3,False):(0,1024,256),(4,True):(512,1024,512),(5,True):(1024,1024,512)}
atlas=np.zeros((2048,2048,4),dtype=np.uint8);atlas[:,:,:3]=255
for (tex,blend),(x,y,size) in slots.items():
 im=d['images'][d['textures'][tex]['source']];v=d['bufferViews'][im['bufferView']];p=output/('temp_%d.png'%tex);p.write_bytes(buf[v.get('byteOffset',0):][:v['byteLength']]);image=bpy.data.images.load(str(p),check_existing=False);image.colorspace_settings.name='Non-Color';repeat=2 if tex in (0,2,3) else 1;image.scale(size-8,(size-8)//repeat)
 pixels=np.array(image.pixels[:],dtype=np.float32).reshape((size-8)//repeat,size-8,4)[::-1];pixels=np.clip(np.rint(pixels*255),0,255).astype(np.uint8)
 pixels=np.tile(pixels,(repeat,1,1))
 if not blend:pixels[:,:,3]=255
 atlas[y:y+size,x:x+size]=np.pad(pixels,((4,4),(4,4),(0,0)),mode='edge');bpy.data.images.remove(image);p.unlink()
header=struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,2048,2048,32,0x28)
(output/'skin.tga').write_bytes(header+atlas[:,:,[2,1,0,3]].tobytes())
points=[];uvs=[];weights=[];faces=[]
for node in d['nodes']:
 if 'mesh' not in node:continue
 skin=d['skins'][node['skin']]
 for pr in d['meshes'][node['mesh']]['primitives']:
  a=pr['attributes'];pos=acc(a['POSITION']);uv=acc(a['TEXCOORD_0']);ji=acc(a['JOINTS_0']);ww=acc(a['WEIGHTS_0']);mat=d['materials'][pr['material']];key=(mat['pbrMetallicRoughness']['baseColorTexture']['index'],mat.get('alphaMode')=='BLEND');x,y,size=slots[key]
  remap={}
  for tri in acc(pr['indices']).reshape(-1,3):
   ff=[]
   for ii in tri:
    ii=int(ii)
    if ii not in remap:
     remap[ii]=len(points);points.append(Q.to_3x3()@Vector(pos[ii]*32));uvs.append(((x+4+float(uv[ii,0])*(size-8))/2048,(y+4+float(uv[ii,1])*(size-8)/(2 if key[0] in (0,2,3) else 1))/2048));weights.append([(skin['joints'][int(j)],float(w)) for j,w in zip(ji[ii],ww[ii]) if w>0])
    ff.append(remap[ii])
   faces.append(ff)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
mesh=bpy.data.meshes.new('Alicia');mesh.from_pydata(points,[],faces);mesh.update();obj=bpy.data.objects.new('Alicia',mesh);bpy.context.collection.objects.link(obj);bpy.context.view_layer.objects.active=obj;obj.select_set(True)
layer=mesh.uv_layers.new()
for loop in mesh.loops:layer.data[loop.index].uv=uvs[loop.vertex_index]
for i in range(len(binds)):obj.vertex_groups.new(name='b%d'%i)
for i,entries in enumerate(weights):
 for j,w in entries:obj.vertex_groups[j].add([i],w,'REPLACE')
mod=obj.modifiers.new('bounded visual-spike reduction','DECIMATE');mod.ratio=.55;mod.use_collapse_triangulate=True;bpy.ops.object.modifier_apply(modifier=mod.name)
mesh=obj.data;mesh.calc_loop_triangles();verts=[];tris=[];lookup={};outweights=[]
for tri in mesh.loop_triangles:
 ids=[]
 for loopid in tri.loops:
  li=mesh.loops[loopid];v=mesh.vertices[li.vertex_index];uv=mesh.uv_layers.active.data[loopid].uv;key=(li.vertex_index,round(uv.x,7),round(uv.y,7))
  if key not in lookup:
   lookup[key]=len(verts);entries=sorted(((g.group,g.weight) for g in v.groups if g.weight>1e-8),key=lambda a:-a[1])[:4];total=sum(w for j,w in entries);assert total>0
   start=len(outweights)
   for j,w in entries:outweights.append((j,w/total,binds[j].inverted()@v.co))
   verts.append((tuple(uv),start,len(entries)))
  ids.append(lookup[key])
 # glTF CCW -> engine's GL_CW alias front face.
 tris.append((ids[0],ids[2],ids[1]))
assert len(verts)<=16384 and len(tris)<=32768
lines=['MD5Version 10','commandline ""','numJoints %d'%len(binds),'numMeshes 1','joints {']
for i,m in enumerate(binds):
 q=m.to_quaternion();q.negate() if q.w>0 else None;t=m.translation
 lines.append(' "b%d" %d ( %.9g %.9g %.9g ) ( %.9g %.9g %.9g )'%(i,parents.get(i,-1),*t,q.x,q.y,q.z))
lines+=['}','mesh {',' shader "skin"',' numverts %d'%len(verts)]
for i,(uv,start,count) in enumerate(verts):lines.append(' vert %d ( %.9g %.9g ) %d %d'%(i,*uv,start,count))
lines+=[' numtris %d'%len(tris)]+[' tri %d %d %d %d'%(i,*tri) for i,tri in enumerate(tris)]+[' numweights %d'%len(outweights)]
for i,(j,w,p) in enumerate(outweights):lines.append(' weight %d %d %.9g ( %.9g %.9g %.9g )'%(i,j,w,*p))
lines+=['}'];data=('\n'.join(lines)+'\n').encode();assert len(data)<=8*1024*1024;(output/'model.md5mesh').write_bytes(data)
sem=['hips','spine','chest','neck','head','leftShoulder','leftUpperArm','leftLowerArm','leftHand','rightShoulder','rightUpperArm','rightLowerArm','rightHand','leftUpperLeg','leftLowerLeg','leftFoot','rightUpperLeg','rightLowerLeg','rightFoot'];names=['Hip','Spine1','Spine2','Neck','Head','Shoulder_L','UpperArm_L','LowerArm_L','Hand_L','Shoulder_R','UpperArm_R','LowerArm_R','Hand_R','UpperLeg_L','LowerLeg_L','Foot_L','UpperLeg_R','LowerLeg_R','Foot_R'];human={b['bone']:b['node'] for b in d['extensions']['VRM']['humanoid']['humanBones']}
(output/'avatar.cfg').write_text('version 1\nname "Alicia MD5 spike"\nscale 1\n'+''.join('bone %s b%d\n'%(name,human[s]) for name,s in zip(names,sem)))
report={'source_sha256':EXPECTED,'vertices':len(verts),'triangles':len(tris),'joints':len(binds),'mesh_bytes':len(data),'texture_rgba_bytes':atlas.nbytes,'units_per_meter':32,'blender':bpy.app.version_string,'decimate_ratio':.55,'notes':['Single 2048 atlas with padded tiles','BLEND approximated by alpha test','MToon, sphere effects and expressions omitted','Original full skeleton retained']};(output/'conversion.json').write_text(json.dumps(report,indent=2));print('ALICIA_CONVERSION',json.dumps(report))
