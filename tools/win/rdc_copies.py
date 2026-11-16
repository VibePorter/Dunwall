# qrenderdoc.exe --python tools\win\rdc_copies.py ; env RDC_IN (.rdc), RDC_OUT (text), RDC_FROM, RDC_TO (eid range)
# Dishonored (2026-11-08): every copy / resolve action in the range with its source and destination resource, the
# destination's type and size, and the subresources (mip, array layer / slice) RenderDoc records for it.
import os, traceback
import renderdoc as rd

out = open(os.environ["RDC_OUT"], "w")


def log(*a):
    out.write(" ".join(str(x) for x in a) + "\n")
    out.flush()


def walk(actions):
    for a in actions:
        yield a
        yield from walk(a.children)


try:
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile()
    cap.OpenFile(os.environ["RDC_IN"], "", None)
    status, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    if ctl is None:
        log("open failed", status)
    else:
        textures = {t.resourceId: t for t in ctl.GetTextures()}
        lo, hi = int(os.environ["RDC_FROM"]), int(os.environ["RDC_TO"])

        def desc(rid):
            t = textures.get(rid)
            return "%s %s %dx%dx%d arrays %d" % (rid, t.type, t.width, t.height, t.depth, t.arraysize) if t else str(rid)

        for a in walk(ctl.GetRootActions()):
            if lo <= a.eventId <= hi and (a.flags & (rd.ActionFlags.Copy | rd.ActionFlags.Resolve)):
                log(a.eventId, a.customName or a.GetName(ctl.GetStructuredFile()),
                    "src", desc(a.copySource), "sub", a.copySourceSubresource.mip, a.copySourceSubresource.slice,
                    "-> dst", desc(a.copyDestination), "sub", a.copyDestinationSubresource.mip,
                    a.copyDestinationSubresource.slice)
        ctl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()
except Exception:
    log(traceback.format_exc())
out.close()
os._exit(0)
