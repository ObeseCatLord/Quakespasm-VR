#!/usr/bin/env python3
"""Deterministic UDP userspace network impairment proxy for smoke tests.

The proxy listens on a local port and forwards datagrams to the backend server
while applying deterministic packet-level modifications:
- fixed delay
- jitter
- loss
- periodic reordering delay
- fixed packet gap bursts (1/2/3+ consecutive drops)
- stall hooks that pause forwarding for a fixed duration
"""

from __future__ import annotations

import argparse
import heapq
import random
import selectors
import signal
import socket
import sys
import time
from dataclasses import dataclass
from typing import Dict, Optional, Tuple


Address = Tuple[str, int]


def parse_host_port(value: str) -> Address:
	parts = value.rsplit(":", 1)
	if len(parts) != 2:
		raise argparse.ArgumentTypeError(f"expected host:port, got {value!r}")
	return parts[0], int(parts[1])


@dataclass
class DirectionState:
	name: str
	sequence: int = 0
	gap_remaining: int = 0


class UDPNetProxy:
	def __init__(
		self,
		listen_port: int,
		target: Address,
		seed: int,
		base_delay_ms: int,
		jitter_ms: int,
		loss_percent: float,
		reorder_every: int,
		reorder_delay_ms: int,
		gap_size: int,
		gap_interval: int,
		stall_ms: int,
		stall_interval: int,
		verbose: bool,
	):
		self.listen_port = listen_port
		self.target = target
		self.rng = random.Random(seed)
		self.base_delay_ms = max(0, base_delay_ms)
		self.jitter_ms = max(0, jitter_ms)
		self.loss_percent = max(0.0, min(100.0, loss_percent))
		self.reorder_every = max(0, reorder_every)
		self.reorder_delay_ms = max(0, reorder_delay_ms)
		self.gap_size = max(0, gap_size)
		self.gap_interval = max(0, gap_interval)
		self.stall_ms = max(0, stall_ms)
		self.stall_interval = max(0, stall_interval)
		self.verbose = verbose

		self.selector = selectors.DefaultSelector()
		self.running = True
		self.events = []
		self.event_seq = 0

		self.direction = {
			"cs": DirectionState("cs"),
			"sc": DirectionState("sc"),
		}
		self.stall_until = 0.0

		self.client_flows: Dict[Address, socket.socket] = {}

		self.listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		self.listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		self.listen_sock.bind(("127.0.0.1", self.listen_port))
		self.listen_sock.setblocking(False)
		self.selector.register(self.listen_sock, selectors.EVENT_READ, ("frontend", None))

	def log(self, text: str) -> None:
		if not self.verbose:
			return
		print(f"[udp_net_proxy] {text}", file=sys.stderr)

	def _cleanup(self) -> None:
		for s in self.client_flows.values():
			try:
				self.selector.unregister(s)
			except Exception:
				pass
			s.close()
		self.client_flows.clear()
		try:
			self.selector.unregister(self.listen_sock)
		except Exception:
			pass
		self.listen_sock.close()
		self.selector.close()

	def _direction_state(self, direction: str) -> DirectionState:
		return self.direction[direction]

	def _schedule(self, due_time: float, target: socket.socket, target_addr: Optional[Address], data: bytes) -> None:
		self.event_seq += 1
		heapq.heappush(self.events, (due_time, self.event_seq, target, target_addr, data))

	def _needs_drop_for_gap(self, state: DirectionState) -> bool:
		if self.gap_size <= 0:
			return False
		if state.gap_remaining > 0:
			state.gap_remaining -= 1
			return True
		if self.gap_interval > 0 and state.sequence % self.gap_interval == 0:
			state.gap_remaining = self.gap_size
			state.gap_remaining -= 1
			return True
		return False

	def _queue_delay(self, direction: str, state: DirectionState) -> float:
		delay_ms = self.base_delay_ms
		if self.jitter_ms > 0:
			delay_ms += self.rng.randint(-self.jitter_ms, self.jitter_ms)
		if delay_ms < 0:
			delay_ms = 0
		if self.loss_percent > 0.0 and self.rng.random() * 100.0 < self.loss_percent:
			self.log(f"{direction}:{state.sequence}: drop(loss)")
			return -1.0
		if self.reorder_every > 0 and state.sequence % self.reorder_every == 0:
			delay_ms += self.reorder_delay_ms
			self.log(f"{direction}:{state.sequence}: reorder")
		if self._needs_drop_for_gap(state):
			self.log(f"{direction}:{state.sequence}: drop(gap)")
			return -1.0
		now = time.time()
		if self.stall_interval > 0 and self.stall_ms > 0 and state.sequence > 0 and state.sequence % self.stall_interval == 0:
			self.stall_until = max(self.stall_until, now + self.stall_ms / 1000.0)
			self.log(f"{direction}:{state.sequence}: stall({self.stall_ms}ms)")
		due = now + delay_ms / 1000.0
		if self.stall_until > due:
			due = self.stall_until
		return due

	def _route_client_packet(self, client_addr: Address, data: bytes) -> None:
		state = self._direction_state("cs")
		state.sequence += 1
		upstream = self.client_flows.get(client_addr)
		if upstream is None:
			upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
			upstream.setblocking(False)
			upstream.connect(self.target)
			self.client_flows[client_addr] = upstream
			self.selector.register(upstream, selectors.EVENT_READ, ("upstream", client_addr))
			self.log(f"new flow {client_addr}")
		delay = self._queue_delay("cs", state)
		if delay < 0:
			return
		self._schedule(delay, upstream, None, data)

	def _route_server_packet(self, client_addr: Address, data: bytes) -> None:
		state = self._direction_state("sc")
		state.sequence += 1
		delay = self._queue_delay("sc", state)
		if delay < 0:
			return
		self._schedule(delay, self.listen_sock, client_addr, data)

	def _dispatch_due(self) -> None:
		now = time.time()
		while self.events and self.events[0][0] <= now:
			_, _, target, target_addr, data = heapq.heappop(self.events)
			try:
				if target_addr is None:
					target.send(data)
				else:
					target.sendto(data, target_addr)
			except OSError:
				pass

	def _handle_frontend(self) -> None:
		try:
			data, client_addr = self.listen_sock.recvfrom(65535)
		except OSError:
			return
		self._route_client_packet(client_addr, data)

	def _handle_upstream(self, client_addr: Address) -> None:
		sock = self.client_flows.get(client_addr)
		if sock is None:
			return
		try:
			data, _ = sock.recvfrom(65535)
		except OSError:
			return
		self._route_server_packet(client_addr, data)

	def run(self) -> None:
		print(
			f"[udp_net_proxy] listening 127.0.0.1:{self.listen_port} -> {self.target[0]}:{self.target[1]}",
			file=sys.stderr,
		)
		def stop(*_args: object) -> None:
			self.running = False

		signal.signal(signal.SIGINT, stop)
		signal.signal(signal.SIGTERM, stop)
		try:
			self.log(f"listening 127.0.0.1:{self.listen_port} -> {self.target[0]}:{self.target[1]}")
			while self.running:
				timeout = 0.05
				if self.events:
					timeout = max(0.0, self.events[0][0] - time.time())
				for key, _ in self.selector.select(timeout):
					kind, client_addr = key.data
					if kind == "frontend":
						self._handle_frontend()
					else:
						self._handle_upstream(client_addr)
				self._dispatch_due()
		finally:
			self._cleanup()


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description="deterministic UDP impairment proxy")
	parser.add_argument("--listen", type=int, required=True)
	parser.add_argument("--target", type=parse_host_port, required=True)
	parser.add_argument("--seed", type=int, default=12345)
	parser.add_argument("--base-delay", type=int, default=0)
	parser.add_argument("--jitter", type=int, default=0)
	parser.add_argument("--loss", type=float, default=0.0)
	parser.add_argument("--reorder-every", type=int, default=0)
	parser.add_argument("--reorder-delay", type=int, default=25)
	parser.add_argument("--gap-size", type=int, default=0)
	parser.add_argument("--gap-interval", type=int, default=0)
	parser.add_argument("--stall-ms", type=int, default=200)
	parser.add_argument("--stall-interval", type=int, default=0)
	parser.add_argument("--verbose", action="store_true")
	return parser.parse_args()


def main() -> int:
	args = parse_args()
	proxy = UDPNetProxy(
		listen_port=args.listen,
		target=args.target,
		seed=args.seed,
		base_delay_ms=args.base_delay,
		jitter_ms=args.jitter,
		loss_percent=args.loss,
		reorder_every=args.reorder_every,
		reorder_delay_ms=args.reorder_delay,
		gap_size=args.gap_size,
		gap_interval=args.gap_interval,
		stall_ms=args.stall_ms,
		stall_interval=args.stall_interval,
		verbose=args.verbose,
	)
	proxy.run()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
