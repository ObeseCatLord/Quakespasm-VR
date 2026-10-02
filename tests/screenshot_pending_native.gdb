# Native desktop command/capture proof; controlled acquire/name availability.
# Run with an assertion-enabled current client in a private Quoth/mfxsp17 profile.
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set $frames=0
set $commands=0
set $writes=0
set $hold_capture=0
set $failed_acquires=0
set $exhausted_names=0
python
import gdb,json,os
from pathlib import Path
observations={'writes':[],'refusals':[]}
def capture_snapshot():
 return {'name':gdb.parse_and_eval('screenshot_imagename').string(),
         'format':gdb.parse_and_eval('screenshot_ext').string(),
         'quality':int(gdb.parse_and_eval('screenshot_quality')),
         'pending':int(gdb.parse_and_eval('take_screenshot'))}
def check_preserved(label):
 assert capture_snapshot()==observations['pending'], 'refusal changed pending capture: '+label
 observations['refusals'].append(label)
end
break GL_EndXRFrame
commands
 silent
 if cls.signon == 4 && sv.active && !cls.demoplayback && $_streq(sv.name, "mfxsp17")
  set $frames=$frames+1
  if $frames == 12
   set scheduler-locking on
   set $scratch=(char*)malloc(384)
   python
commands=[b'screenshot jpg 90\n\0',b'screenshot bogus\n\0',b'screenshot png 101\n\0',b'screenshot png 77\n\0',b'screenshot png\n\0',b'quit\n\0']
for index,command in enumerate(commands): gdb.selected_inferior().write_memory(int(gdb.parse_and_eval('$scratch'))+index*64,command)
   end
   set scheduler-locking off
   enable 4
   set $hold_capture=1
   set scheduler-locking on
   call (void)Cbuf_AddText($scratch+0)
   set scheduler-locking off
  end
  if $frames == 14
   if $commands != 1 || $writes != 0 || !take_screenshot || $failed_acquires == 0
    quit 1
   end
   python
observations['pending']=capture_snapshot()
assert observations['pending']['format']=='jpg' and observations['pending']['quality']==90
   end
   set scheduler-locking on
   call (void)Cbuf_AddText($scratch+64)
   set scheduler-locking off
  end
  if $frames == 16
   if $commands != 2 || $writes != 0
    quit 1
   end
   python check_preserved('invalid-format')
   set scheduler-locking on
   call (void)Cbuf_AddText($scratch+128)
   set scheduler-locking off
  end
  if $frames == 18
   if $commands != 3 || $writes != 0
    quit 1
   end
   python check_preserved('invalid-quality')
   enable 5
   set scheduler-locking on
   call (void)Cbuf_AddText($scratch+192)
   set scheduler-locking off
  end
  if $frames == 20
   disable 5
   if $commands != 4 || $writes != 0 || $exhausted_names != 100
    quit 1
   end
   python check_preserved('filename-exhaustion')
   set $hold_capture=0
  end
  if $frames == 32
   if $writes != 1 || take_screenshot
    quit 1
   end
   set scheduler-locking on
   call (void)Cbuf_AddText($scratch+256)
   set scheduler-locking off
  end
  if $frames == 60
   if $writes != 2 || $commands != 5 || take_screenshot
    quit 1
   end
   python
assert [x['format'] for x in observations['writes']]==['jpg','png']
assert observations['writes'][0]['name']==observations['pending']['name']
observations.update(passed=True,frames=60,failed_acquires=int(gdb.parse_and_eval('$failed_acquires')),exhausted_names=100)
Path(os.environ['QSVR_SCREENSHOT_RESULT']).write_text(json.dumps(observations,indent=2)+'\n')
   end
   printf "SCREENSHOT_PENDING_NATIVE_PASSED refused metadata intact, acquire recovery, JPEG then PNG\n"
   set scheduler-locking on
   call (void)Cbuf_AddText($scratch+320)
   set scheduler-locking off
   disable 1
  end
 end
 continue
end
break SCR_ScreenShot_f
commands
 silent
 set $commands=$commands+1
 continue
end
break WriteScreenshot
commands
 silent
 set $writes=$writes+1
 python observations['writes'].append(capture_snapshot())
 continue
end
break GL_AcquireNextSwapChainImage
commands
 silent
 if $hold_capture
  set $failed_acquires=$failed_acquires+1
  return (qboolean)0
 end
 continue
end
break Sys_FileType
commands
 silent
 set $exhausted_names=$exhausted_names+1
 # FS_ENT_FILE is the preprocessor value (1 << 0), not a DWARF symbol.
 return (int)1
 continue
end
disable 4
disable 5
run
