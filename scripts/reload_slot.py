#!/usr/bin/env python3
"""Force a Schwung chain slot to reload its synth module.

WHY THIS EXISTS. Deploying a new dsp.so does not change what the device is
running. The chain host dlopen()s the plugin into the shim (inside
MoveOriginal); the atomic mv in deploy.sh swaps the directory entry while the
running process keeps the old inode mapped. `kill shadow_ui` does not help —
shadow_ui is a different process. Worse, an on-device loadtest dlopens the
file itself, so it passes against code nobody is hearing.

Re-writing the slot's module key makes the chain host unload the position and
dlopen a fresh instance: exactly what re-picking the synth in the UI does.

    ./scripts/reload_slot.py <host> [slot] [module-id]

IT ONLY WORKS ON A SLOT THAT ALREADY HAS A SYNTH. `set_param synth:module` is
accepted by the manager on an empty slot and silently does nothing — the chain
has no position to re-point. Putting a module into an EMPTY slot is a
device-UI action (pick it on the Move, or in the manager's Modules page).
This script now says so instead of claiming success.

WHICH IT USED TO DO. The original fired three websocket messages and printed
"the new dsp.so is now the running one" unconditionally, without ever reading
a reply — so a reload that did nothing at all looked identical to one that
worked. It now subscribes, reads slot_info back, and verifies the slot really
holds the module before it says anything.

Standard library only (the Mac has no toolchain and no venv here).
"""
import base64, json, os, socket, struct, sys, time


def frames(sock, seconds):
    """Yield decoded websocket payloads for `seconds`. Server frames are
    unmasked, and the manager never fragments, so this stays small."""
    sock.settimeout(0.4)
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf += chunk
        while len(buf) >= 2:
            ln, off = buf[1] & 0x7F, 2
            if ln == 126:
                if len(buf) < 4:
                    break
                ln, off = struct.unpack(">H", buf[2:4])[0], 4
            elif ln == 127:
                if len(buf) < 10:
                    break
                ln, off = struct.unpack(">Q", buf[2:10])[0], 10
            if len(buf) < off + ln:
                break
            yield buf[off:off + ln]
            buf = buf[off + ln:]


def slot_synth(sock, slot, seconds=1.5):
    """The module id the manager reports for `slot`, or None if it said
    nothing. Empty string means the slot has no synth."""
    found = None
    for raw in frames(sock, seconds):
        try:
            m = json.loads(raw)
        except Exception:
            continue
        if m.get("type") == "slot_info" and m.get("slot") == slot:
            found = m.get("synth", "")
    return found

def send(sock, obj):
    payload = json.dumps(obj).encode()
    header = bytearray([0x81])
    n = len(payload)
    if n < 126:
        header.append(0x80 | n)
    elif n < 65536:
        header.append(0x80 | 126); header += struct.pack(">H", n)
    else:
        header.append(0x80 | 127); header += struct.pack(">Q", n)
    mask = os.urandom(4)
    header += mask
    sock.sendall(bytes(header) + bytes(c ^ mask[i % 4] for i, c in enumerate(payload)))

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "move.local"
    slot = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    module = sys.argv[3] if len(sys.argv) > 3 else "6w6"

    key = base64.b64encode(os.urandom(16)).decode()
    try:
        sock = socket.create_connection((host, 7700), timeout=6)
    except OSError as e:
        print("reload: cannot reach schwung-manager on %s:7700 (%s)" % (host, e))
        return 1
    sock.sendall((
        "GET /ws/remote-ui HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n" % (host, key)).encode())

    head = b""
    sock.settimeout(6)
    while b"\r\n\r\n" not in head:
        chunk = sock.recv(1)
        if not chunk:
            print("reload: websocket handshake closed early")
            return 1
        head += chunk
    if b" 101" not in head.split(b"\r\n")[0]:
        print("reload: websocket upgrade refused:", head.split(b"\r\n")[0].decode(errors="replace"))
        return 1

    send(sock, {"type": "subscribe", "slot": slot})
    before = slot_synth(sock, slot)
    if before == "":
        print("reload: slot %d is EMPTY. set_param synth:module does nothing on\n"
              "        an empty slot — pick '%s' on the Move (or in the manager's\n"
              "        Modules page) and it will load the file just deployed."
              % (slot, module))
        sock.close()
        return 2
    if before is not None:
        print("reload: slot %d currently holds '%s'" % (slot, before))
    #
    # BOUNCE, do not just set. Setting synth:module to the value it already
    # has is a no-op — the chain host sees no change and keeps the old
    # dlopen'd inode. That exact trap shipped a deploy where the UI files
    # were new and the DSP was yesterday's, and the md5 check in deploy.sh
    # could not catch it because the file ON DISK was correct; only the
    # RUNNING copy was stale. Loading a different module first forces the
    # unload — "linein" is the cheapest thing in the store (no DSP of its
    # own) and ships with every Schwung install.
    #
    send(sock, {"type": "set_param", "slot": slot, "key": "synth:module", "value": "linein"})
    time.sleep(2.5)   # let the intermediate module actually LOAD — a set that
                      # arrives mid-load is dropped, and 1.2 s was not enough
    send(sock, {"type": "set_param", "slot": slot, "key": "synth:module", "value": module})
    after = slot_synth(sock, slot, 2.5)
    #
    # RE-SUBSCRIBE IF NOTHING CAME BACK. The manager only pushes slot_info on
    # a CHANGE, and a bounce ends on the value the slot already had — so the
    # confirming broadcast never arrives and the reload looks like it failed
    # when it worked. Asking again is what tells the two apart.
    #
    if after is None:
        send(sock, {"type": "subscribe", "slot": slot})
        after = slot_synth(sock, slot, 2.0)
    sock.close()

    # VERIFY. The whole point of this rewrite: never report success we did not
    # observe. A silent no-op here is exactly the failure the module-store
    # deploy path is prone to, and it is invisible without this check.
    if after == module:
        print("reload: slot %d is now '%s' — the new dsp.so is the running one"
              % (slot, module))
        return 0
    if after is None:
        print("reload: slot %d — the manager never reported back. The bounce was\n"
              "        sent; check the device log for 'Loading synth: .../%s/'."
              % (slot, module))
        return 1
    print("reload: slot %d did NOT take '%s' (manager reports %r).\n"
          "        Re-pick the module in the slot on the device."
          % (slot, module, after))
    return 1

if __name__ == "__main__":
    sys.exit(main())
