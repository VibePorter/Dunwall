# qrenderdoc.exe --python tools\win\rdc_debug_pixel.py ; env RDC_IN (.rdc), RDC_OUT (text), RDC_EID, RDC_X, RDC_Y
# Dishonored (2026-11-08): RenderDoc's shader debugger on one pixel of one draw: every step's changed variables (name
# and up to 4 float components), so the term that gives a wrong colour can be read off.
import os, traceback
import renderdoc as rd

out = open(os.environ["RDC_OUT"], "w")


def log(*a):
    out.write(" ".join(str(x) for x in a) + "\n")
    out.flush()


def value(v):
    n = max(1, min(4, v.columns))
    return " ".join("%.4g" % v.value.f32v[i] for i in range(n))


try:
    rd.InitialiseReplay(rd.GlobalEnvironment(), [])
    cap = rd.OpenCaptureFile()
    cap.OpenFile(os.environ["RDC_IN"], "", None)
    status, ctl = cap.OpenCapture(rd.ReplayOptions(), None)
    if ctl is None:
        log("open failed", status)
    else:
        ctl.SetFrameEvent(int(os.environ["RDC_EID"]), True)
        inputs = rd.DebugPixelInputs()
        trace = ctl.DebugPixel(int(os.environ["RDC_X"]), int(os.environ["RDC_Y"]), inputs)
        if trace is None or trace.debugger is None:
            log("no trace (shader debugging unsupported for this draw?)")
        else:
            steps = 0
            while True:
                states = ctl.ContinueDebug(trace.debugger)
                if not states:
                    break
                for s in states:
                    steps += 1
                    for ch in s.changes:
                        v = ch.after
                        log(s.nextInstruction, v.name, value(v))
            log("steps", steps)
            ctl.FreeTrace(trace)
        ctl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()
except Exception:
    log(traceback.format_exc())
out.close()
os._exit(0)
