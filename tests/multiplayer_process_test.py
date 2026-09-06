"""Complete a Quick match with independent processes and isolated GameData."""

import argparse
from collections import deque
import json
import os
from pathlib import Path
import queue
import select
import socket
import subprocess
import tempfile
import threading
import time


class ReconnectingRelay:
    """Forward encrypted bytes unchanged and cut exactly one live connection."""

    def __init__(self, upstream_port):
        self.upstream_port = upstream_port
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(0.1)
        self.port = self.listener.getsockname()[1]
        self.stopping = threading.Event()
        self.cut = threading.Event()
        self.dropped = threading.Event()
        self.connections = 0
        self.error = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stopping.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError as error:
                if not self.stopping.is_set():
                    self.error = str(error)
                return
            try:
                with client, socket.create_connection(("127.0.0.1", self.upstream_port), timeout=2) as host:
                    client.settimeout(2)
                    self.connections += 1
                    while not self.stopping.is_set():
                        if self.cut.is_set() and not self.dropped.is_set():
                            self.dropped.set()
                            break
                        readable, _, _ = select.select((client, host), (), (), 0.05)
                        closed = False
                        for source in readable:
                            data = source.recv(65536)
                            if not data:
                                closed = True
                                break
                            (host if source is client else client).sendall(data)
                        if closed:
                            break
            except OSError as error:
                if not self.stopping.is_set():
                    self.error = str(error)
                    return

    def close(self):
        self.stopping.set()
        self.listener.close()
        self.thread.join(timeout=3)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=240)
    parser.add_argument("--mode", choices=("duel", "classic-ffa", "coalition"), default="duel")
    parser.add_argument("--humans", choices=(2, 3, 4), type=int, default=2)
    parser.add_argument("--combat", action="store_true", help="start a deterministic host-only border battle fixture")
    parser.add_argument("--reconnect", action="store_true", help="cut one client's encrypted stream during a Rune hand")
    args = parser.parse_args()
    if args.mode == "duel" and args.humans != 2:
        parser.error("Duel requires exactly two humans")
    if args.combat and args.mode != "duel":
        parser.error("The combat fixture uses Duel")
    binary = args.executable.resolve(strict=True)
    messages = queue.Queue()
    processes = {}
    readers = []
    peer_names = ["host"] + [f"client{index}" for index in range(1, args.humans)]
    tails = {peer: deque(maxlen=50) for peer in peer_names}
    results = {}
    relay = None
    deadline = time.monotonic() + args.timeout

    def consume(peer, stream, channel):
        for line in stream:
            line = line.rstrip()
            tails[peer].append(f"{channel}: {line[:2000]}")
            if channel == "stdout" and line.startswith("FWR_MP "):
                try:
                    messages.put((peer, json.loads(line[len("FWR_MP "):])))
                except (ValueError, TypeError) as error:
                    messages.put((peer, {"kind": "error", "message": f"invalid result JSON: {error}"}))

    with tempfile.TemporaryDirectory(prefix="four-winds-multiplayer-process-") as temporary:
        root = Path(temporary)

        def spawn(peer, role, *parameters):
            profile = root / peer
            profile.mkdir()
            environment = os.environ.copy()
            environment.update({
                "SDL_VIDEODRIVER": "dummy",
                "SDL_AUDIODRIVER": "dummy",
                "SDL_RENDER_DRIVER": "software",
                "FOUR_WINDS_SETTINGS_FILE": str(profile / "settings.json"),
                "FOUR_WINDS_RECOVERY_DIR": str(profile / "recovery"),
                "FOUR_WINDS_SAVE_DIR": str(profile / "saves"),
                "FOUR_WINDS_DIAGNOSTICS_DIR": str(profile / "diagnostics"),
            })
            process = subprocess.Popen(
                [str(binary), "--multiplayer-process-peer", role, *map(str, parameters)],
                cwd=profile, env=environment, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, encoding="utf-8", errors="replace", bufsize=1,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            processes[peer] = process
            for channel in ("stdout", "stderr"):
                reader = threading.Thread(target=consume, args=(peer, getattr(process, channel), channel), daemon=True)
                reader.start()
                readers.append(reader)

        try:
            combat_arguments = ["combat"] if args.combat else []
            spawn("host", "host", args.mode, args.humans, *combat_arguments)
            while time.monotonic() < deadline and len(results) != args.humans:
                if relay and relay.error:
                    raise AssertionError(f"reconnect relay failed: {relay.error}")
                try:
                    peer, message = messages.get(timeout=0.1)
                except queue.Empty:
                    for name, process in processes.items():
                        if process.poll() is not None and name not in results:
                            raise AssertionError(f"{name} exited before final score (code {process.returncode})")
                    continue
                kind = message.get("kind")
                if kind == "room":
                    assert peer == "host" and len(processes) == 1, "duplicate or unexpected room announcement"
                    port = message.get("port")
                    code = message.get("code")
                    assert type(port) is int and 0 < port <= 65535 and isinstance(code, str) and len(code) == 32
                    assert message.get("mode") == args.mode and message.get("humans") == args.humans, "wrong room configuration"
                    if args.reconnect:
                        relay = ReconnectingRelay(port)
                    for name in peer_names[1:]:
                        peer_port = relay.port if relay and name == "client1" else port
                        spawn(name, "client", peer_port, code, *combat_arguments)
                elif kind == "error":
                    raise AssertionError(f"{peer}: {message.get('message')}")
                elif kind == "progress" and relay and peer == "client1" and message.get("phase") == 3:
                    if message.get("discards", 0) >= 1:
                        relay.cut.set()
                elif kind == "result":
                    assert peer not in results, "duplicate terminal result"
                    results[peer] = message
            assert len(results) == args.humans, f"{args.humans}-process Quick {args.mode} timed out"
            expected_phases = {2, 3, 4, 5, 6, 7}
            for peer, result in results.items():
                assert set(result["visited"]) == expected_phases, f"{peer} skipped a match phase: {result['visited']}"
                assert result["phase"] == 7 and len(result["scores"]) == (2 if args.mode == "duel" else 4)
                assert result["discards"] >= 2 and result["passes"] >= 2 and result["mapTurns"] >= 2, \
                    f"{peer} did not participate in both rune and map phases"
            assert len({result["avatar"] for result in results.values()}) == args.humans, "processes controlled duplicate avatars"
            for name in peer_names[1:]:
                assert results["host"]["scores"] == results[name]["scores"], f"{name} terminal standings differ from host"
                assert results["host"]["revision"] == results[name]["revision"], f"{name} terminal revision differs from host"
            if relay:
                assert relay.error is None, f"reconnect relay failed: {relay.error}"
                assert relay.dropped.is_set() and relay.connections >= 2, "active client never reconnected after the socket cut"
            if args.combat:
                assert sum(result["summons"] for result in results.values()) >= 1, "no real summon command completed"
                assert sum(result["moves"] for result in results.values()) >= 1, "no real movement command completed"
                assert sum(result["battleChoices"] for result in results.values()) >= 1, "no private battle choice arrived"
                assert all(result["combats"] >= 1 for result in results.values()), "combat did not reach every peer"
                assert results["host"]["combats"] == results["client1"]["combats"], "public combat event counts diverged"
            for peer, process in processes.items():
                remaining = max(0.1, deadline - time.monotonic())
                assert process.wait(timeout=min(5, remaining)) == 0, f"{peer} failed after final state"
            print(f"multiplayer process test: complete Quick {args.mode}, {args.humans} humans; "
                  "all six phases, distinct players and identical final standings")
            if args.combat:
                print("network combat: real summons, invasion movement, private battle choices and shared battle results verified")
            if relay:
                print("network reconnect: interrupted encrypted stream resumed the same player and reached identical final standings")
            return 0
        except (AssertionError, subprocess.TimeoutExpired, OSError) as error:
            print(f"FAIL: {error}")
            for peer in processes:
                print(f"{peer} recent output:")
                print("\n".join(tails[peer]))
            return 1
        finally:
            for process in processes.values():
                if process.poll() is None:
                    process.terminate()
            for process in processes.values():
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
            for reader in readers:
                reader.join(timeout=1)
            if relay:
                relay.close()


if __name__ == "__main__":
    raise SystemExit(main())
