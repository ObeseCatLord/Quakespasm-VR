import json, math, os, time


class ConnectedLifecycle:
    def __init__(self, scope):
        self.s, self.g = scope, scope['gdb']
        self.role = os.environ.get('QSVR_LIFECYCLE_ROLE', '')
        self.root = os.environ.get('QSVR_LIFECYCLE_ROOT', '')
        if self.role not in ('desktop', 'vr', 'replacement'):
            raise RuntimeError('invalid QSVR_LIFECYCLE_ROLE')
        if not os.path.isabs(self.root) or not os.path.isdir(self.root):
            raise RuntimeError('QSVR_LIFECYCLE_ROOT must be an absolute control directory')
        self.control = os.path.join(self.root, 'control.json')
        self.path = os.path.join(self.root, self.role + '-lifecycle.json')
        self.deadline = time.monotonic() + 90.0
        self.expected = {'desktop':'Desktop', 'vr':'VR', 'replacement':'Replacement'}[self.role]
        self.peer = {'desktop':'VR', 'vr':'Desktop', 'replacement':'VR'}[self.role]
        self.upstream = bool(scope.get('upstream_peer', False) or
                             os.environ.get('QSVR_LOCAL_UPSTREAM') == '1')
        self.result = dict(status='running', stage='preparing', samples=[], reset_events=[])
        self.slot = self.desktop_slot = None
        self.exercise_active = False
        self.exercise_wire = []
        self.exit_code = None
        self.g.events.exited.connect(self.on_exit)
        self.save()

    def on_exit(self, event):
        self.exit_code = getattr(event, 'exit_code', None)

    def iv(self, expr): return int(self.g.parse_and_eval(expr))
    def fv(self, expr): return float(self.g.parse_and_eval(expr))
    def need(self, ok, why):
        if not ok: raise RuntimeError(why)

    def phase(self):
        try:
            with open(self.control) as f: value = json.load(f).get('phase')
        except FileNotFoundError:
            return None
        self.need(value in ('initial', 'map', 'drop', 'replacement', 'exercise', 'observe', 'finish'),
                  'invalid lifecycle control phase: %r' % value)
        return value

    def save(self):
        tmp = self.path + '.tmp.' + str(os.getpid())
        with open(tmp, 'w') as f:
            json.dump(self.result, f, indent=2, sort_keys=True); f.write('\n')
        os.replace(tmp, self.path)

    def stage(self, value):
        self.result['stage'] = value; self.save()

    def world(self):
        return os.path.basename(self.g.parse_and_eval('cl.worldmodel->name').string())

    def scoreboard(self):
        count = self.iv('cl.maxclients')
        self.need(count > 1 and self.iv('(unsigned long)cl.scores') != 0,
                  'scoreboard is unavailable')
        names = [self.g.parse_and_eval('cl.scores[%d].name' % i).string() for i in range(count)]
        colors = [self.iv('cl.scores[%d].colors' % i) for i in range(count)]
        return names, colors

    def sample(self, label, need_peer=True):
        owner = self.iv('cl.viewentity'); slot = owner - 1
        names, colors = self.scoreboard()
        self.need(0 <= slot < len(names) and names[slot] == self.expected,
                  'viewentity does not own the %s scoreboard slot' % self.expected)
        peers = [i for i, name in enumerate(names) if i != slot and name]
        self.need(bool(peers) or not need_peer, 'named peer is absent')
        origin = [self.fv('cl.entities[%d].netstate.origin[%d]' % (owner, i)) for i in range(3)]
        peer_origin = ([self.fv('cl.entities[%d].netstate.origin[%d]' % (peers[0]+1, i))
                        for i in range(3)] if peers else None)
        record = dict(label=label, world=self.world(), owner=owner, slot=slot,
                      names=names, colors=colors, origin=origin, peer_origin=peer_origin,
                      ack=self.iv('cl.ackedmovemessages'), sent=self.iv('cl.movemessages'),
                      shells=self.iv('cl.stats[6]'),
                      private=False if self.upstream else bool(self.iv('cl.protocol_qsvr')))
        if self.role == 'vr' and label in ('replacement', 'observed'):
            selected = self.s['prediction_sample']()
            self.s['require_selected_prediction_state'](selected)
            record['selected_prediction'] = selected
        if not self.upstream:
            record['unreliable_send_sequence'] = self.iv('cls.netcon->unreliableSendSequence')
            record['unreliable_receive_sequence'] = self.iv('cls.netcon->unreliableReceiveSequence')
        finite = lambda v: v is None or (len(v) == 3 and all(math.isfinite(x) for x in v))
        self.need(finite(origin) and finite(peer_origin), 'nonfinite authoritative pose')
        return record

    def append(self, label, need_peer=True):
        record = self.sample(label, need_peer); self.result['samples'].append(record); self.save()
        return record

    def check_pair(self, record, names):
        self.need(sorted(n for n in record['names'] if n) == sorted(names),
                  'expected named peers %r, got %r' % (names, record['names']))
        self.need(record['private'] == (not self.upstream), 'protocol privacy does not match profile')

    def setup(self):
        for breakpoint in self.g.breakpoints() or ():
            breakpoint.delete()
        probe = self
        class Frame(probe.g.Breakpoint):
            def stop(self):
                if probe.role == 'vr' and probe.s.get('phase') != 'fire':
                    probe.s['phase'] = 'neutral'
                return True
        class Fatal(probe.g.Breakpoint):
            def stop(self): return True
        class ClearFinish(probe.g.FinishBreakpoint):
            def stop(self):
                try: probe.reset_event()
                except Exception as exc:
                    probe.result['reset_events'].append(dict(passed=False, error=str(exc)))
                    probe.save()
                return False
        class ClearEntry(probe.g.Breakpoint):
            def stop(self):
                try:
                    if (probe.role in ('desktop', 'vr') and probe.result['stage'] == 'ready' and
                            not probe.result['reset_events'] and probe.world() == 'e1m1.bsp'):
                        ClearFinish(probe.g.newest_frame(), internal=True)
                except Exception as exc:
                    probe.result['reset_events'].append(dict(passed=False, error=str(exc)))
                    probe.save()
                return False
        self.frame_breakpoint = Frame('Host_Frame', internal=True)
        Fatal('Host_Error', internal=True); Fatal('Sys_Error', internal=True)
        ClearEntry('CL_ClearState', internal=True)
        if self.role == 'vr':
            self.s['lifecycle_hook'] = self
            self.s['Actions']('VR_InputCommands', internal=True)
            self.s['PrivateWire']('CL_WritePrivateUsercmd', internal=True)

    def reset_event(self):
        count = self.iv('sizeof(cl.movecmds) / sizeof(cl.movecmds[0])')
        ring = []
        for i in range(count):
            item = dict(sequence=self.iv('cl.movecmds[%d].sequence' % i),
                        seconds=self.fv('cl.movecmds[%d].seconds' % i))
            if not self.upstream: item['msec'] = self.iv('cl.movecmds[%d].msec' % i)
            ring.append(item)
        event = dict(movemessages=self.iv('cl.movemessages'),
                     ackedmovemessages=self.iv('cl.ackedmovemessages'),
                     pendingcmdseconds=self.fv('cl.pendingcmd.seconds'),
                     movecmds_cleared=all(not x['sequence'] and not x['seconds'] and
                                          x.get('msec', 0) == 0 for x in ring))
        if not self.upstream:
            event['move_snapshot_valid'] = bool(self.iv('cl.move_snapshot_valid'))
            event['passed'] = (event['movemessages'] == event['ackedmovemessages'] == 0 and
                               event['pendingcmdseconds'] == 0 and event['movecmds_cleared'] and
                               not event['move_snapshot_valid'])
        else:
            event['passed'] = (event['movemessages'] == event['ackedmovemessages'] == 0 and
                               event['pendingcmdseconds'] == 0 and event['movecmds_cleared'])
        self.result['reset_events'].append(event); self.save()

    def skip_input(self):
        if self.role != 'vr': return False
        try:
            return (not self.iv('(unsigned long)frame') or self.iv('cls.signon') != 4 or
                    not self.iv('vulkan_globals.stereo_active') or
                    not self.iv('openxr_frame.should_render') or
                    not self.iv('openxr_frame.focused') or
                    not self.iv('openxr_frame.devices[0].valid'))
        except Exception:
            return True

    def observe_wire(self, command):
        if self.exercise_active and command.get('attack') and command.get('forwardmove', 0) > 0:
            self.exercise_wire.append(dict(command))

    def cbuf(self, text, execute=True):
        self.g.execute('call (void)Cbuf_AddText(' + json.dumps(text) + ')', to_string=True)
        if execute: self.g.execute('call (void)Cbuf_Execute()', to_string=True)

    def pump(self):
        self.need(time.monotonic() <= self.deadline,
                  '90-second lifecycle deadline expired at stage %s' % self.result['stage'])
        self.g.execute('continue', to_string=True)
        self.need(self.g.selected_inferior().pid != 0, 'client exited before native lifecycle quit')
        frame = self.g.newest_frame(); name = frame.name() if frame else ''
        self.need(name == 'Host_Frame', 'inferior stopped in %s during lifecycle' % (name or 'unknown'))

    def until(self, predicate):
        while not predicate(): self.pump()

    def initial_ready(self):
        self.until(lambda: self.iv('cls.signon') == 4 and
                   sorted(n for n in self.scoreboard()[0] if n) ==
                   sorted((self.expected, self.peer)))
        r = self.sample('ready'); expected_map = 'e1m2.bsp' if self.role == 'replacement' else 'e1m1.bsp'
        self.need(r['world'] == expected_map, 'unexpected initial map'); self.check_pair(r, (self.expected, self.peer))
        self.need(r['ack'] > 0 and r['ack'] <= r['sent'], 'initial movement ACK is not coherent')
        self.slot = r['slot']
        if self.role == 'vr': self.desktop_slot = r['names'].index('Desktop')
        if self.role == 'replacement':
            with open(os.path.join(self.root, 'vr-lifecycle.json')) as f: vr = json.load(f)
            old_slot = vr['samples'][0]['names'].index('Desktop')
            self.need(r['slot'] == old_slot and r['names'][old_slot] == 'Replacement' and
                      'Desktop' not in r['names'], 'replacement did not reuse the retired slot')
        self.result['samples'].append(r); self.stage('ready')

    def wait_map(self):
        def ready():
            if self.iv('cls.signon') != 4: return False
            r = self.sample('mapped')
            if r['world'] != 'e1m2.bsp' or sorted(n for n in r['names'] if n) != ['Desktop', 'VR']:
                return False
            events = self.result['reset_events']
            self.need(events and events[-1].get('passed'), 'CL_ClearState did not clear movement state')
            if r['ack'] <= 0 or r['ack'] > r['sent']: return False
            if not self.upstream and not (self.iv('cl.move_snapshot_valid') and
                    self.iv('cl.move_snapshot_ack') == r['ack'] and
                    self.iv('cl.move_snapshot_owner') == r['owner']): return False
            self.check_pair(r, ('Desktop', 'VR'))
            self.result['samples'].append(r); self.stage('mapped'); return True
        self.until(ready)

    def wait_peer_retired(self):
        def retired():
            if self.iv('cls.signon') != 4: return False
            names, _ = self.scoreboard()
            if names[self.desktop_slot] or 'Desktop' in names: return False
            r = self.append('peer_retired', False)
            self.need(r['peer_origin'] is None, 'retired Desktop still has a named entity')
            self.stage('peer_retired'); return True
        self.until(retired)

    def wait_replacement(self):
        def replaced():
            if self.iv('cls.signon') != 4: return False
            names, _ = self.scoreboard()
            if names[self.desktop_slot] != 'Replacement' or 'Desktop' in names: return False
            r = self.sample('replacement')
            self.need(sorted(n for n in r['names'] if n) == ['Replacement', 'VR'],
                      'unexpected peers after slot reuse')
            self.result['samples'].append(r); self.stage('replacement'); return True
        self.until(replaced)

    def exercise(self):
        if self.role == 'vr':
            self.s['phase'] = 'neutral'; start = self.s['actions']
            self.until(lambda: self.s['actions'] - start >= 18)
        else: self.cbuf('-forward\n-attack\n')
        before = self.append('exercise_before')
        self.exercise_active = True
        if self.role == 'vr':
            self.s['lifecycle_stationary_fire'] = True; self.s['phase'] = 'fire'
        else: self.cbuf('+attack\n')
        end = time.monotonic() + 1.0
        lowest_shells = before['shells']
        while time.monotonic() < end:
            self.pump(); lowest_shells = min(lowest_shells, self.iv('cl.stats[6]'))
        fired = self.append('stationary_fired')
        self.result['stationary_min_shells'] = lowest_shells
        self.need(lowest_shells < before['shells'],
                  'stationary fire did not consume shotgun shells')
        self.need(fired['ack'] > before['ack'] and fired['ack'] <= fired['sent'],
                  'stationary fire did not advance movement ACK')
        if self.role == 'vr': self.s['lifecycle_stationary_fire'] = False
        else: self.cbuf('+forward\n')
        end = time.monotonic() + 1.0
        while time.monotonic() < end: self.pump()
        if self.role == 'vr': self.s['phase'] = 'neutral'
        else: self.cbuf('-forward\n-attack\n')
        end = time.monotonic() + 0.5
        while time.monotonic() < end: self.pump()
        self.exercise_active = False; after = self.append('exercised')
        self.need(after['ack'] > before['ack'] and after['ack'] <= after['sent'] and
                  after['sent'] > before['sent'], 'exercise did not advance movement ACK')
        moved = math.dist(before['origin'], after['origin'])
        self.need(math.isfinite(moved) and moved > 16, 'exercise did not move the authoritative owner')
        if self.role == 'vr':
            self.need(any(c['sequence'] >= before['sent'] and c['sequence'] <= after['ack']
                          for c in self.exercise_wire), 'no new acknowledged VR forward/attack command')
        self.stage('exercised')

    def run(self):
        try:
            self.setup()
            if self.role == 'vr': self.s['phase'] = 'neutral'
            self.initial_ready()
            if self.role in ('desktop', 'vr'):
                self.until(lambda: self.phase() == 'map'); self.wait_map()
                if self.role == 'desktop':
                    self.until(lambda: self.phase() == 'drop'); self.cbuf('disconnect\n')
                    self.until(lambda: self.iv('cls.signon') == 0 and
                               self.iv('cls.state') == self.iv('ca_disconnected'))
                    old = self.result['samples'][-1]
                    self.result['samples'].append(dict(label='disconnected', slot=self.slot,
                        names=list(old['names']), colors=list(old['colors']),
                        signon=self.iv('cls.signon'), state=self.iv('cls.state'),
                        netcon_null=not self.iv('(unsigned long)cls.netcon')))
                    self.stage('disconnected')
                else:
                    self.until(lambda: self.phase() == 'drop')
                    self.desktop_slot = self.result['samples'][0]['names'].index('Desktop')
                    self.wait_peer_retired(); self.until(lambda: self.phase() == 'replacement')
                    self.wait_replacement(); self.until(lambda: self.phase() == 'exercise')
                    self.exercise()
            else:
                self.until(lambda: self.phase() == 'exercise'); self.exercise()
            if self.role != 'desktop':
                self.until(lambda: self.phase() == 'observe')
                end = time.monotonic() + 0.5
                while time.monotonic() < end: self.pump()
                self.append('observed'); self.stage('observed')
            self.until(lambda: self.phase() == 'finish')
            self.s['phase'] = 'neutral'; self.cbuf('quit\n', False)
            self.frame_breakpoint.enabled = False
            self.g.execute('continue', to_string=True)
            self.result['quit_exit_code'] = self.exit_code
            self.result['quit_remaining_pid'] = self.g.selected_inferior().pid
            if self.result['quit_remaining_pid']:
                frame = self.g.newest_frame()
                self.result['quit_stop'] = frame.name() if frame else 'unknown'
            self.save()
            self.need(self.g.selected_inferior().pid == 0 and self.exit_code == 0,
                      'native quit did not produce a normal zero exit')
            self.result['status'] = 'passed'; self.save()
            self.g.write('QSVR_CONNECTED_LIFECYCLE_PASSED\n')
        except Exception as exc:
            self.exercise_active = False
            self.result.update(status='failed', error=str(exc)); self.save()
            self.g.write('QSVR_CONNECTED_LIFECYCLE_FAILED ' + str(exc) + '\n', self.g.STDERR)
            raise
