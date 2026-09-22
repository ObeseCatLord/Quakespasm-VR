# Isolated desktop peer proof. Caller supplies binary, asset profile, and connection args.
# Uses held keyboard state through the real input/sender, not synthetic movement packets.
# QSVR_PEER_RESULT: output JSON. Optional QSVR_PEER_CHANGELEVEL: readiness JSON path.
set pagination off
set confirm off
set debuginfod enabled off
set breakpoint pending on
python
import gdb, time, json, os
started = time.monotonic()
phase = 0
phase_time = started
samples = []
solver_hits = 0
partial_frames = 0
previous_motion = None

def number(expr):
    return int(gdb.parse_and_eval(expr))
def vector(expr):
    return [float(gdb.parse_and_eval('%s[%d]' % (expr, i))) for i in range(3)]
def sample(label):
    owner = number('cl.viewentity')
    return dict(label=label, signon=number('cls.signon'), dialect=number('cl.protocol_qsvr'),
                world=gdb.parse_and_eval('cl.worldmodel->name').string(), legacy=number('cls.legacy_qsvr'),
                origin=vector('cl.entities[%d].netstate.origin' % owner),
                rendered_origin=vector('cl.entities[%d].origin' % owner),
                shells=number('cl.stats[6]'), ack=number('cl.ackedmovemessages'),
                sent=number('cl.movemessages'), association=number('cl.move_snapshot_valid'),
                permission=number('cl.move_ack_prediction_allowed'), solver_hits=solver_hits)
class Solver(gdb.Breakpoint):
    def stop(self):
        global solver_hits
        solver_hits += 1
        return False
class Frame(gdb.Breakpoint):
    def stop(self):
        global phase, phase_time, partial_frames, previous_motion
        now = time.monotonic()
        if now - started > 45:
            samples.append({'failure':'signon/gameplay timeout','phase':phase})
            return True
        if number('cls.signon') != 4:
            return False
        if phase == 2:
            # Same sent history and authoritative state, but a new rendered
            # position: proof that partial presentation progressed between sends.
            owner=number('cl.viewentity')
            current=(number('cl.movemessages'),number('cl.ackedmovemessages'),
                     vector('cl.entities[%d].netstate.origin' % owner),
                     vector('cl.entities[%d].origin' % owner),solver_hits)
            if (previous_motion and current[:3] == previous_motion[:3] and
                current[4] > previous_motion[4] and number('cl.move_snapshot_valid') and
                number('cl.move_ack_prediction_allowed')):
                if sum((a-b)**2 for a,b in zip(current[3],previous_motion[3])) > .001**2:
                    partial_frames += 1
            previous_motion=current
        if phase == 0:
            phase = 1
            phase_time = now
            return False
        if phase == 1 and now-phase_time >= 1:
            samples.append(sample('before'))
            gdb.execute('set in_forward.state = 1', to_string=True)
            gdb.execute('set in_attack.state = 1', to_string=True)
            phase, phase_time = 2, now
        elif phase == 2 and now-phase_time >= 2:
            samples.append(sample('moving'))
            gdb.execute('set in_forward.state = 0', to_string=True)
            gdb.execute('set in_attack.state = 0', to_string=True)
            phase, phase_time = 3, now
        elif phase == 3 and now-phase_time >= 1:
            samples.append(sample('settled'))
            if os.environ.get('QSVR_PEER_CHANGELEVEL'):
                phase, phase_time = 4, now
                with open(os.environ['QSVR_PEER_CHANGELEVEL'], 'w') as f: json.dump(samples, f)
            else:
                return True
        elif phase == 4 and gdb.parse_and_eval('cl.worldmodel->name').string() != samples[0]['world']:
            phase, phase_time = 5, now
        elif phase == 5 and now-phase_time >= 1:
            samples.append(sample('after_changelevel'))
            return True
        return False
Solver('PM_PlayerMove', internal=True)
Frame('Host_Frame', internal=True)
gdb.Breakpoint('Host_Error', internal=True)
gdb.Breakpoint('Sys_Error', internal=True)
end
run
python
result={'phase':phase,'samples':samples,'solver_hits':solver_hits,'partial_frames':partial_frames,'stop':gdb.newest_frame().name()}
if result['stop'] in ('Host_Error','Sys_Error'):
    result['signon'] = number('cls.signon')
    result['dialect'] = number('cl.protocol_qsvr')
    result['readcount'] = number('msg_readcount')
    result['packet_size'] = number('net_message.cursize')
    result['stack'] = gdb.execute('bt 6', to_string=True)
    begin = max(0, result['readcount'] - 20)
    length = min(result['packet_size'] - begin, 64)
    result['wire_near_read'] = bytes(gdb.selected_inferior().read_memory(int(gdb.parse_and_eval('net_message.data')) + begin, length)).hex()
with open(os.environ['QSVR_PEER_RESULT'],'w') as f: json.dump(result,f,indent=2)
assert result['stop'] == 'Host_Frame', result['stop']
assert len(samples) == (4 if os.environ.get('QSVR_PEER_CHANGELEVEL') else 3), samples
before, moving, settled = samples[:3]
assert all(s['signon'] == 4 and s['dialect'] == 1 and s['legacy'] == 1 for s in samples)
assert sum((a-b)**2 for a,b in zip(before['origin'],moving['origin'])) > 100**2
assert moving['shells'] < before['shells'], 'authoritative firing was not observed'
assert moving['solver_hits'] > before['solver_hits'], 'no actual PM replay observed'
assert settled['permission'] and settled['association']
assert sum((a-b)**2 for a,b in zip(settled['origin'],settled['rendered_origin'])) < .01**2
if os.environ.get('QSVR_PEER_CHANGELEVEL'):
    assert samples[-1]['world'] != before['world']
    assert samples[-1]['permission'] and samples[-1]['association']
if os.environ.get('QSVR_PEER_EXPECT_PARTIAL'):
    assert partial_frames >= 5, 'no smooth progression between network sends'
print('QSVR_PEER_GAMEPLAY_PASSED '+json.dumps(result))
end
quit
