# qrenderdoc.exe --python tools\win\rdc_history.py ; env RDC_IN (.rdc), RDC_OUT (text), RDC_EID (event whose colour
# target is examined), RDC_PIXELS (';'-separated x,y)
# Dishonored (2026-11-08): RenderDoc's pixel history of a few pixels of the colour target bound at an event: every event
# that touched each pixel, whether it passed, and the colour after it.
import os, traceback
import renderdoc as rd

out = open(os.environ["RDC_OUT"], "w")


def log(*a):
    out.write(" ".join(str(x) for x in a) + "\n")
    out.flush()


try:
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile()
    cap.OpenFile(os.environ["RDC_IN"], "", None)
    status, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    if ctl is None:
        log("open failed", status)
    else:
        ctl.SetFrameEvent(int(os.environ["RDC_EID"]), True)
        target = ctl.GetPipelineState().GetOutputTargets()[0].resource
        log("target", target)
        for p in [p for p in os.environ["RDC_PIXELS"].split(";") if p]:
            x, y = (int(v) for v in p.split(","))
            log("== pixel", x, y)
            for m in ctl.PixelHistory(target, x, y, rd.Subresource(0, 0, 0), rd.CompType.Typeless):
                c = m.postMod.col.floatValue
                log("  eid", m.eventId, "passed" if m.Passed() else "failed",
                    "depth-fail" if m.depthTestFailed else "", "after %.3f %.3f %.3f %.3f" % (c[0], c[1], c[2], c[3]))
        ctl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()
except Exception:
    log(traceback.format_exc())
out.close()
os._exit(0)
