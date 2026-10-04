set pagination off
set confirm off
set debuginfod enabled off
set print thread-events off
set python print-stack full
break SV_Physics
run
python
exec(open('tests/Bonk_runtime.py').read())
end
quit
