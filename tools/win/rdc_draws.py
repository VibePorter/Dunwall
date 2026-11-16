# qrenderdoc.exe --python tools\win\rdc_draws.py ; env RDC_IN (.rdc), RDC_OUT (text)
# Dishonored (2026-11-08): every draw of the capture in order: its event, index / vertex count, pixel shader resource
# and first colour target, one line each, to line the capture up with the game's draw log ([dh-draw]).
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
        n = 0
        for a in walk(ctl.GetRootActions()):
            if not (a.flags & rd.ActionFlags.Drawcall):
                continue
            ctl.SetFrameEvent(a.eventId, False)
            st = ctl.GetPipelineState()
            ps = st.GetShader(rd.ShaderStage.Pixel)
            targets = st.GetOutputTargets()
            rt = targets[0].resource if targets else None
            log(n, a.eventId, a.numIndices, int(ps), int(rt) if rt else 0)
            n += 1
        ctl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()
except Exception:
    log(traceback.format_exc())
out.close()
os._exit(0)
