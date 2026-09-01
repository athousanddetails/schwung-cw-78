/*
 * CW-78 ui_chain.js — a thin binding over Schwung's shared param_pages library
 * (host 0.12.1+), keeping every gesture the stock hierarchy editor does not
 * have. Ported from 6W6 with an 808 roster: the three kits must feel
 * identical under the hands, so the gestures, the hint and the focus gate are
 * the same code with different tables.
 *
 * CW-78 fills the pad block where 6W6 half-filled it — SIXTEEN drums, one per
 * cr78 SynthDef — so the tables below are the only real difference, plus a
 * mute mask that needs sixteen bits rather than eight. There is no Master pad
 * to handle, because there is no pad left for it.
 *
 * Division of labour:
 *   param_pages (stock)          this file (CW-78)
 *   ------------------          ---------------------------------
 *   knob grid, Movy layout      pads pass through to Move + page-follow
 *   viz graphics (faders...)    Shift+Pad silent select (white pad follows)
 *   jog page / Shift+Jog        Mute+Pad per-lane CW-78 mutes
 *   section picker (jog click)  focus gate (never steal another slot's pads)
 *   hold-knob name strip        pad_block lifecycle + self-heal
 *   Shift reveal + fine mode
 *   Mute+knob reset-to-default
 *
 * Why this file still exists at all: the host's enterComponentEdit prefers a
 * module's ui_hierarchy and would never load ui_chain.js if we served one — so
 * the DSP serves the hierarchy under "ui_pages" instead, and the injected
 * getParam below rewrites the controller's "ui_hierarchy" read to it. Pads are
 * not part of the stock grid's input model, so pad behaviour stays ours.
 *
 * GPL-3.0. param_pages © Schwung contributors; its level walk derives from
 * schwung-movy (MIT, megadake).
 */

import { createController } from '/data/UserData/schwung/shared/param_pages/page_controller.mjs';
import { decodeInput, applyInput } from '/data/UserData/schwung/shared/param_pages/page_input.mjs';
import { PAGE_KNOBS } from '/data/UserData/schwung/shared/param_pages/page_plan.mjs';
import { LAYOUT_MOVY } from '/data/UserData/schwung/shared/param_pages/render_page_movy.mjs';
import { setLED } from '/data/UserData/schwung/shared/input_filter.mjs';

(function () {
    "use strict";

    /*
     * raw pad note -> page key (page-follow). The left 4x4 block, bottom row
     * first: BD SD RS HH / CY MA CL HB / LB LC CB TB / GU MB MAIN FX. These
     * must match pad_to_voice() in cr78_plugin.cpp — the DSP decides what
     * sounds, this decides what the screen shows, and them disagreeing is a
     * bug nobody would spot quickly.
     *
     * WHICH IS EXACTLY THE BUG THAT SHIPPED. This file arrived from 8W8 with
     * its sixteen-drum table (lt, mt, cp, oh...) — page ids CW-78 does not
     * have — so pads 3-14 played their drum and the screen sat still, and it
     * looked like the pads were broken when it was the map. If the roster
     * ever changes again, change pad_to_voice() and THIS TABLE in one commit.
     *
     * Pad 15 (note 94) is MAIN. Pad 16 (note 95) is the FX pad: first press
     * shows Reverb, next Delay, and it alternates — two bus pages, one spare
     * pad, no contest. Neither plays a sound; the DSP is silent on both.
     */
    var PAD2LEVEL = { 68: "bd", 69: "sd", 70: "rs", 71: "hh",
                      76: "cy", 77: "ma", 78: "cl", 79: "hb",
                      84: "lb", 85: "lc", 86: "cb", 87: "tb",
                      92: "gu", 93: "mb", 94: "root", 95: "__fx__" };

    /* raw pad note -> CW-78 lane (Mute+Pad). Same order as the DSP's enum.
     * 94 and 95 are page pads, not drums: no lane, nothing to mute. */
    var PAD2LANE = { 68: 0, 69: 1, 70: 2, 71: 3, 76: 4, 77: 5, 78: 6, 79: 7,
                     84: 8, 85: 9, 86: 10, 87: 11, 92: 12, 93: 13 };

    /* page key -> lane whose mute the title indicator shows (-1 = none) */
    var LEVEL2LANE = { bd: 0, sd: 1, rs: 2, hh: 3, cy: 4, ma: 5, cl: 6, hb: 7,
                       lb: 8, lc: 9, cb: 10, tb: 11, gu: 12, mb: 13,
                       root: -1,
                       /* buses and the rhythm page have no lane, no mute */
                       rev: -1, dly: -1, rhy: -1 };

    /* The FX pad alternates between the two bus pages. On globalThis for the
     * same reason as the main lock: the host re-evaluates this file on every
     * editor open, and a module-level flag would forget which bus was next. */
    function nextFxLevel() {
        var lvl = globalThis.__cw78_fx_next === "dly" ? "dly" : "rev";
        globalThis.__cw78_fx_next = (lvl === "rev") ? "dly" : "rev";
        return lvl;
    }

    /*
     * MAIN-PAGE LOCK. A jog click while already ON the Main page toggles it.
     * Locked, the pads still play and still record but no longer switch the
     * page — so the master knobs stay under your hands while you jam the
     * kit. Shift+Pad still selects: that gesture IS an explicit "take me
     * there". The title shows [L].
     *
     * It lives on globalThis deliberately. The host re-evaluates this file
     * every time the editor is opened, so module-level state would reset and
     * the lock would look like it had dropped itself.
     */
    function mainLocked() { return !!globalThis.__78w_main_lock; }

    function onMainPage() {
        var page = controller && controller.page;
        return !!page && (page.level === "root" || page.level == null);
    }

    /* The lane index that means "Master" to the DSP's ui_focus. Fourteen
     * drums, so Master is 14 — and it HAS a pad: note 94, unlike 8W8 whose
     * sixteen drums filled the block. */
    var MASTER_LANE = 14;

    /* Fourteen lanes, fourteen bits. */
    var LANE_MASK = 0x3FFF;

    /*
     * The two page pads glow while the editor is active — Main white, FX
     * azure — because Move will not light them: its own pad LEDs come from
     * the selected track's rack cells, and cells 15/16 hold no drum, so the
     * two most useful pads on the block sat dark and read as dead.
     *
     * setLED (input_filter.mjs) writes a note-on toward the SURFACE through
     * the shadow UI's MIDI-out ring; its cache suppresses repeat writes, but
     * Move repaints the grid behind our back (track switches, mode changes),
     * so the colours are re-asserted with force on a slow tick. Deactivation
     * paints them off rather than snapshot-restoring: the rack cells under
     * them are empty, so "off" IS Move's own colour for those pads — and on
     * the paths where our tick never gets to run again (slot switch to a
     * module with its own editor) the shim's own LED replay cleans up.
     */
    var PAGE_PAD_COLORS = { 94: 120 /* white: Main */, 95: 16 /* azure: FX */ };
    var pagePadsLit = false;
    var pagePadTick = 0;

    function paintPagePads(on, force) {
        for (var n in PAGE_PAD_COLORS)
            setLED(+n, on ? PAGE_PAD_COLORS[n] : 0, !!force);
    }

    var mySlot = -1;
    var padBlocked = false;
    var muteHeld = false;
    var mutesMask = 0;
    var controller = null;

    /* First-run gesture hint: once per shadow_ui session, dismissed by any
     * input -- including pads, which are our layer -- or on its own after
     * HINT_MS. A hint nobody can wave away feels stuck. The "shown" flag lives
     * on globalThis: the host re-evaluates this file on every editor open, so
     * module-level state would reset each time; the shadow_ui process's global
     * object is what actually lives for the session. */
    var HINT_MS = 4000;
    var HINT_FLAG = "__78w_hint_shown";
    var hintUntil = 0;

    function dismissHint() {
        hintUntil = 0;
        if (controller && controller.dismissHint) controller.dismissHint();
    }

    function has(fn) { return typeof globalThis[fn] === "function"; }

    function uiSlot() {
        return has("shadow_get_ui_slot") ? shadow_get_ui_slot() : 0;
    }

    function isFocused() {
        return mySlot >= 0 && uiSlot() === mySlot;
    }

    function setPadBlock(on) {
        if (padBlocked === on) return;
        if (has("host_pad_block")) { host_pad_block(on ? 1 : 0); padBlocked = on; }
    }

    function shiftHeld() {
        return has("shadow_get_shift_held") && !!shadow_get_shift_held();
    }

    /* The controller's device I/O. One special case: its "ui_hierarchy" read
     * is rewritten to "ui_pages" — the key the DSP actually serves, because
     * serving ui_hierarchy itself would stop this file from ever loading. */
    function ctlGetParam(key) {
        if (!has("shadow_get_param")) return null;
        if (key === "synth:ui_hierarchy") key = "synth:ui_pages";
        return shadow_get_param(mySlot, key);
    }

    function ctlSetParam(key, value) {
        if (has("shadow_set_param")) shadow_set_param(mySlot, key, String(value));
    }

    function announce(text) {
        if (has("host_announce_screenreader")) host_announce_screenreader(text);
    }

    function refreshMutes() {
        var m = parseInt(ctlGetParam("synth:mutes"), 10);
        mutesMask = isNaN(m) ? 0 : m;
    }

    function toggleLaneMute(lane) {
        mutesMask = (mutesMask ^ (1 << lane)) & LANE_MASK;
        ctlSetParam("synth:mutes", String(mutesMask));
    }

    /* Jump the grid to the first page of a hierarchy level, and publish the
     * page id so the remote panel follows. A set_param, not a read: the shim
     * only tells schwung-manager about WRITES. */
    var lastFocus = -1;
    function goToLevel(levelKey) {
        if (!controller) return;
        var lane = LEVEL2LANE[levelKey];
        if (lane === undefined || lane < 0) lane = MASTER_LANE;
        /* Only on a CHANGE. Every pad press calls this, and a param write is a
         * ~2.8 ms round-trip on the shared channel — more than a whole screen
         * redraw costs — so re-announcing the page you are already on is pure
         * waste on the path the editor needs for its own reads. */
        if (lane !== lastFocus) {
            lastFocus = lane;
            ctlSetParam("synth:ui_focus", String(lane));
        }
        var pages = controller.pages;
        for (var i = 0; i < pages.length; i++) {
            if (pages[i].level === levelKey ||
                (levelKey === "root" && pages[i].level === null)) {
                controller.goToPage(i);
                return;
            }
        }
        if (levelKey === "root") controller.goToPage(0);
    }

    /* Give the pad back to Move as a real press: plays the Move-side kit,
     * records while REC is on, and updates Move's pad selection. */
    function injectToMove(data) {
        if (!has("move_midi_inject_to_move")) return;
        var type = (data[0] & 0xF0) === 0x90 ? 0x09
                 : (data[0] & 0xF0) === 0x80 ? 0x08
                 : (data[0] & 0xF0) === 0xA0 ? 0x0A : 0;
        if (!type) return;
        move_midi_inject_to_move([type, data[0], data[1], data[2]]);
    }

    /* ---------------- chain_ui hooks ---------------- */

    function init() {
        mySlot = uiSlot();
        setPadBlock(true);
        refreshMutes();

        controller = createController({
            getParam: ctlGetParam,
            setParam: ctlSetParam,
            announce: announce
        });
        controller.load({ slot: mySlot, component: "synth", prefix: "synth" });
        controller.setLayout(LAYOUT_MOVY);
        if (!globalThis[HINT_FLAG]) {
            globalThis[HINT_FLAG] = true;
            controller.showHint([
                "Pad: play + select",
                "Pad15 Main  Pad16 FX",
                "Sh+Pad: select only",
                "Mute+Pad: mute drum",
                "Jog: page  Click: list",
                "Click on Main: lock",
                "Shift: fine + values",
                "Mute+knob: default"
            ], "CW-78");
            hintUntil = Date.now() + HINT_MS;
        }
        announce("CW-78");
    }

    /* Title-bar text. The stock grid prints the page's own name on the right
     * of the bar, so this must NOT repeat it ("CW-78 > BASS DRUM  BASS DRUM"):
     * just the module name plus the mute flag for the drum on screen. */
    function title() {
        var t = "CW-78";
        if (mainLocked()) t += " [L]";
        var page = controller && controller.page;
        var lane = page ? LEVEL2LANE[page.level] : -1;
        if (lane !== undefined && lane >= 0 && (mutesMask & (1 << lane)))
            t += " [M]";
        return t;
    }

    function tick() {
        var shown = !has("shadow_get_display_mode") || shadow_get_display_mode() === 1;
        var active = shown && isFocused();
        setPadBlock(active);
        if (active !== pagePadsLit) {
            pagePadsLit = active;
            paintPagePads(active, true);
        } else if (active && (++pagePadTick % 60) === 0) {
            /* Re-assert ~every two seconds: survives Move's own grid
             * repaints, costs two packets. */
            paintPagePads(true, true);
        }
        if (!active || !controller) return;

        if (hintUntil && Date.now() >= hintUntil) dismissHint();
        controller.setReveal(shiftHeld());
        controller.tick();

        /* The grid paces its own redraws; draw every tick like the stock
         * binding does (a full page render is ~1.6 ms, measured upstream). */
        clear_screen();
        var page = controller.page;
        if (controller.pickerOpen || (page && page.kind === PAGE_KNOBS)) {
            controller.render(
                {
                    fillRect: fill_rect, print: print, textWidth: text_width,
                    line: typeof draw_line === "function" ? draw_line : undefined,
                    fillCircle: typeof fill_circle === "function" ? fill_circle : undefined,
                    drawCircle: typeof draw_circle === "function" ? draw_circle : undefined,
                    drawArc: typeof draw_arc === "function" ? draw_arc : undefined
                },
                { title: title() }
            );
            /*
             * THE SECOND HALF OF THE DRAW, and it is not optional.
             *
             * render() paints a page into a rect the CALLER owns; nothing in
             * param_pages clears the screen, which is what lets a consumer
             * host a page inside its own chrome. So anything FULL-SCREEN is
             * handed back to the frame owner -- and that is us.
             *
             * Today that means the enum peek: turn a multi-option enum and its
             * option list rises over the grid for ~700ms. Without this call
             * the controller still tracks the peek and applyInput still
             * swallows the Back that dismisses it; it is simply painted
             * nowhere. That is how CW-78 shipped, silently -- reported from
             * the device as "Mode and Style do not peek on the Rhythm page",
             * which was true of every enum on every page, RHY Style's
             * seventeen rhythms most of all.
             *
             * Guarded because renderOverlays landed in a later host than this
             * file's min_host_version, and an older host simply has no
             * overlays to draw.
             */
            if (typeof controller.renderOverlays === "function") {
                controller.renderOverlays(
                    { fillRect: fill_rect, print: print, textWidth: text_width },
                    { clearScreen: clear_screen }
                );
            }
        } else {
            /* Non-grid page kinds do not occur in CW-78's hierarchy; if one ever
             * does, show something honest instead of a stale frame. */
            print(2, 28, "CW-78: unsupported page", 1);
        }
    }

    function onMidiMessageInternal(data) {
        var status = data[0] & 0xF0;
        var d1 = data[1];
        var d2 = data[2];

        /* Another slot is focused: never react; keep the surface alive. */
        if (!isFocused()) {
            if ((status === 0x90 || status === 0x80 || status === 0xA0) &&
                d1 >= 68 && d1 <= 99)
                injectToMove(data);
            return;
        }

        /* Mute button held-state (CC 88): ours for Mute+Pad, the library's
         * for Mute+knob reset — tracked here, passed to decodeInput below. */
        if (status === 0xB0 && d1 === 88) {
            muteHeld = (d2 > 0);
            return;
        }

        /* ---- Pads: CW-78's own layer ---- */
        if ((status === 0x90 || status === 0x80 || status === 0xA0) &&
            d1 >= 68 && d1 <= 99) {

            if (status === 0x90 && d2 > 0) dismissHint();

            /* Mute + Pad: toggle that lane's CW-78 mute; press still reaches
             * Move so its native state stays in step. */
            if (muteHeld) {
                var lane = PAD2LANE[d1];
                if (status === 0x90 && d2 > 0 && lane !== undefined)
                    toggleLaneMute(lane);
                injectToMove(data);
                return;
            }

            var level = PAD2LEVEL[d1];
            if (level === "__fx__") level = (status === 0x90 && d2 > 0)
                                          ? nextFxLevel() : undefined;
            var pagePad = (d1 === 94 || d1 === 95);

            /* Page pads: navigate and nothing else. They navigate even under
             * the main lock — pad 15 IS the way back to Main, and locking
             * yourself out of the lock would be silly. They still reach Move
             * so its pad selection stays honest; the DSP is silent on both
             * notes. */
            if (pagePad) {
                if (status === 0x90 && d2 > 0 && level !== undefined)
                    goToLevel(level);
                injectToMove(data);
                return;
            }

            /* Locked to Main: the pad plays, the page stays. */
            if (mainLocked() && !shiftHeld()) {
                injectToMove(data);
                return;
            }

            if (shiftHeld()) {
                /* Silent select: page follows AND Move's white pad follows —
                 * the DSP swallows exactly the one note routed back (60 ms
                 * window). Accepted trade: with REC armed and playing, this
                 * press would be recorded; Gus does not use REC. */
                if (status === 0x90 && d2 > 0) {
                    if (level !== undefined) goToLevel(level);
                    ctlSetParam("synth:mute_ms", "60");
                    injectToMove(data);
                } else {
                    injectToMove(data);        /* matching release */
                }
                return;
            }

            /* Plain pad: page follows what you play; Move plays/records.
             * Every pad in the block is a drum now, so every one of them
             * sounds and every one of them reaches Move. */
            if (status === 0x90 && d2 > 0 && level !== undefined)
                goToLevel(level);
            injectToMove(data);
            return;
        }

        /* ---- Everything else: the stock grid's input model ---- */
        if (!controller) return;
        var intent = decodeInput(data, { shift: shiftHeld(), mute: muteHeld });
        if (!intent) return;
        /* Before applyInput, or the section picker consumes the click. */
        if (intent.type === "click" && !controller.pickerOpen && onMainPage()) {
            globalThis.__78w_main_lock = !globalThis.__78w_main_lock;
            return;                        /* click = lock toggle, not picker */
        }
        var todo = applyInput(controller, intent, { nowMs: Date.now(), reveal: false });
        if (todo && todo.action === "exit") {
            /* Back never reaches us (the host consumes it); any other exit
             * intent just closes the picker. */
            if (controller.pickerOpen) controller.closePicker();
        }
        /* 'open' (opaque param editors) cannot occur: every CW-78 param is an
         * int or an enum. Ignored if a future param ever produces one. */
    }

    function onMidiMessageExternal(data) { }

    function handleBack() {
        if (controller && controller.pickerOpen) {
            controller.closePicker();
            return true;                       /* consumed: close the list */
        }
        setPadBlock(false);
        /* The host stops ticking us after this, so the deactivate path in
         * tick() never runs — go dark now or stay lit forever. */
        pagePadsLit = false;
        paintPagePads(false, true);
        return false;                          /* host exits the editor */
    }

    globalThis.chain_ui = {
        init: init,
        tick: tick,
        onMidiMessageInternal: onMidiMessageInternal,
        onMidiMessageExternal: onMidiMessageExternal,
        handleBack: handleBack
    };
})();
