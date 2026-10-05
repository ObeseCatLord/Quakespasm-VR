set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
set python print-stack full
break SV_Physics
# The original map needs its two settling frames before player creation.
ignore 1 2
run
# Native load also settles a map. Do not interrupt inferior calls there.
disable 1
python
import os
import traceback
try:
    fixture = os.environ['BONK_SKINS_RUNTIME']
    with open(fixture, encoding='utf-8') as source:
        exec(compile(source.read(), fixture, 'exec', optimize=0))
except BaseException:
    traceback.print_exc()
    gdb.execute('quit 1')
end
quit 0
