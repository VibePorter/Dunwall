# qrenderdoc.exe --python tools\win\rdc_bindings.py ; env RDC_IN (.rdc), RDC_OUT (text), RDC_EIDS (';'-separated)
# Dishonored (2026-11-08): every read-only binding the pixel shader of each event uses, including null descriptors
# (rdc_shader.py lists only the ones that are textures), with each texture's type, size, depth / array size and format.
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
        textures = {t.resourceId: t for t in ctl.GetTextures()}
        names = {r.resourceId: r.name for r in ctl.GetResources()}
        for eid in [int(x) for x in os.environ["RDC_EIDS"].split(";") if x]:
            ctl.SetFrameEvent(eid, True)
            st = ctl.GetPipelineState()
            log("==== eid", eid)
            for ro in st.GetReadOnlyResources(rd.ShaderStage.Pixel, False):
                rid = ro.descriptor.resource
                t = textures.get(rid)
                desc = ("%s %dx%dx%d arrays %d mips %d %s" % (t.type, t.width, t.height, t.depth, t.arraysize,
                                                               t.mips, t.format.Name())) if t else "(not a texture)"
                log("  set/index", ro.access.index, "element", ro.access.arrayElement, "type", ro.access.type,
                    rid, names.get(rid, ""), desc)
        ctl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()
except Exception:
    log(traceback.format_exc())
out.close()
os._exit(0)
