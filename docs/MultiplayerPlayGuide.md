# Multiplayer play guide

This describes the multiplayer development build on `develop` (0.6.0). The
`v0.5.0` tag contains the accepted local modes and does not include networking.
Use the same multiplayer build and content package on every device.

## Create and join a room

1. Connect the devices to the same local network. On the first device choose
   **Multiplayer**, enter a name and select Duel, Free for all or Coalition.
   Select Quick or Classic Rune Game and the number of human players. Duel has
   two humans; the four-seat modes fill the remaining seats with AI.
2. Leave the port at **19782** unless another room already uses it. Choose
   **Create room**. The lobby shows the host's local IPv4 address. If several
   interfaces are available, tap the address to choose the network shared with
   the other players.
3. Choose **Copy invitation** and give it to the other players. On their
   devices choose **Multiplayer → Paste invitation**, enter a name and choose
   **Join room**. Address, port and room code can also be entered separately.
4. When every human seat is connected, the host chooses **Start match**.
   Each player controls one hand and one clan. Finish each summary page to
   indicate readiness; the next phase starts when everyone has continued.

This version assigns guardians and clans by seat order. The host controls
Nucrus/Red; the next seats use Lakkho/Yellow, Ziag/Aqua and Dayla/Purple. Duel
uses the first two seats and splits the island between them.

Windows may ask whether to allow the game through its firewall. Allow the game
on the network used for the match. Guest Wi-Fi networks that isolate devices
will prevent direct connections even when both devices use the same router.

## Direct Internet rooms

Rooms use a direct TCP connection to the host. There is no public lobby,
account service, relay or automatic router configuration. An Internet match
requires a reachable host IPv4 address and TCP port: for example, a host with a
public IPv4 address and an explicit router port forward, or a private VPN shared
by the players. The host remains an ordinary game client.

The invitation format is `fourwinds://IPv4:port/#room-secret`. The copied
invitation initially contains a local interface address. For a public route,
replace only its address/port with the reachable endpoint, retaining the room
secret. A shared VPN can use its interface address directly. A local address
such as `192.168.x.x` cannot be reached from a separate Internet connection.

Connections use TLS 1.3 with an ephemeral key exchange and a random 128-bit room
secret. Share the complete invitation privately; it grants room admission.
The host receives all authoritative state, while each client receives only its
assigned private hand and permitted observations. This is a trusted-host model.

## Disconnects and Android backgrounding

A disconnected human pauses the match. A client whose process remains alive
automatically reconnects with its private seat token and restores its current
prompt. Accepted commands are deduplicated if their acknowledgement was lost.
On Android, return to the existing game after backgrounding and wait for the
connection to recover; the other players keep their game open.

Network sessions currently live in memory. Closing/killing a client process,
choosing **Leave room**, or terminating the host does not produce a network
Continue save. Ordinary single-player saves are kept separate and are never
overwritten by network snapshots. Full process-restart recovery and host
migration are not implemented.

## Acceptance checks

- Complete Quick Duel on two devices through runes, island movement, a battle
  and the final score. Verify that both final standings agree.
- In Coalition, verify team ownership, allied-action restrictions and the same
  team victory on every device. Test four humans and a room with AI seats.
- Disconnect one client during a rune claim or battle choice; reconnect without
  restarting its process. Verify the pending choice returns and no command
  executes twice.
- On Android, background and resume during a hand, summary and battle dialog.
  Test the invitation clipboard and touch controls at the device's normal scale.

Automated evidence and outstanding device checks are tracked in
[MultiplayerImplementation.md](MultiplayerImplementation.md).
