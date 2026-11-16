# qrenderdoc.exe --python tools\win\rdc_pixels.py ; env RDC_IN (.rdc), RDC_OUT (text), RDC_EID, RDC_TEXS (';'-separated
# resource id numbers), RDC_PIXELS (';'-separated x,y)
# Dishonored (2026-11-08): the values of a few pixels of each texture as they are after an event (PickPixel: floats,
# whatever the format), to compare a pass's input and output.
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
        textures = {int(t.resourceId): t for t in ctl.GetTextures()}
        pixels = [tuple(int(v) for v in p.split(",")) for p in os.environ["RDC_PIXELS"].split(";") if p]
        for tid in [int(x) for x in os.environ["RDC_TEXS"].split(";") if x]:
            t = textures.get(tid)
            if t is None:
                log("no texture", tid)
                continue
            log("texture", tid, "%dx%d" % (t.width, t.height), t.format.Name())
            for (x, y) in pixels:
                v = ctl.PickPixel(t.resourceId, x, y, rd.Subresource(0, 0, 0), rd.CompType.Typeless)
                log("  (%d,%d)" % (x, y), " ".join("%.4f" % c for c in v.floatValue))
        ctl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()
except Exception:
    log(traceback.format_exc())
out.close()
os._exit(0)
