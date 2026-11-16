# qrenderdoc.exe --python tools\win\rdc_clears.py ; env RDC_IN (.rdc), RDC_OUT (text), RDC_EIDS (';'-separated eids)
# Dishonored (2026-11-09): the parameters of each listed vkCmdClearAttachments (attachments, values, rects) and the
# viewport / scissor of the draw right after it, from the structured file.
import os
import renderdoc as rd

out = open(os.environ["RDC_OUT"], "w")


def log(*a):
    out.write(" ".join(str(x) for x in a) + "\n")
    out.flush()


def dump(obj, depth=0, limit=60):
    lines = []

    def rec(o, d):
        if len(lines) > limit:
            return
        val = ""
        try:
            if o.type.basetype in (rd.SDBasic.UnsignedInteger,):
                val = str(o.AsInt())
            elif o.type.basetype in (rd.SDBasic.SignedInteger,):
                val = str(o.AsInt())
            elif o.type.basetype == rd.SDBasic.Float:
                val = "%g" % o.AsFloat()
            elif o.type.basetype == rd.SDBasic.Enum:
                val = o.AsString()
        except Exception:
            pass
        lines.append("  " * d + o.name + (" = " + val if val else ""))
        n = o.NumChildren() if hasattr(o, "NumChildren") else 0
        for i in range(n):
            rec(o.GetChild(i), d + 1)

    rec(obj, depth)
    return lines


try:
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile()
    cap.OpenFile(os.environ["RDC_IN"], "", None)
    status, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    if ctl is None:
        raise RuntimeError(str(status))
    sf = ctl.GetStructuredFile()
    wanted = [int(x) for x in os.environ["RDC_EIDS"].split(";") if x]

    def walk(actions):
        for x in actions:
            yield x
            yield from walk(x.children)

    by_eid = {a.eventId: a for a in walk(ctl.GetRootActions())}
    for eid in wanted:
        a = by_eid.get(eid)
        if a is None:
            log("eid", eid, "not an action")
            continue
        log("== eid", eid, a.GetName(sf))
        for ev in a.events:
            chunk = sf.chunks[ev.chunkIndex]
            for line in dump(chunk):
                log(line)
        # the next draw's viewport / scissor
        nxt = next((b for b in sorted(by_eid) if b > eid and by_eid[b].flags & rd.ActionFlags.Drawcall), None)
        if nxt:
            ctl.SetFrameEvent(nxt, True)
            st = ctl.GetPipelineState()
            vp = st.GetViewport(0)
            sc = st.GetScissor(0)
            log("  next draw", nxt, "viewport", vp.x, vp.y, vp.width, vp.height, "scissor", sc.x, sc.y, sc.width,
                sc.height)
except Exception as e:
    import traceback
    log("ERROR", e)
    log(traceback.format_exc())
out.close()
os._exit(0)
