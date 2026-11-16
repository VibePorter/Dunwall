# qrenderdoc.exe --python tools\win\rdc_texture.py ; env RDC_IN (.rdc), RDC_DIR (folder), RDC_EID, RDC_TEX (resource id
# number), RDC_LAYERS (';'-separated array layers / depth slices, default 0)
# Dishonored (2026-11-08): saves one texture as it is at an event, layer by layer, as PNG (<dir>/tex<id>_l<layer>.png)
# and writes the raw RGBA8 values of each layer's first row to <dir>/tex<id>.txt.
import os, traceback
import renderdoc as rd

folder = os.environ["RDC_DIR"]
out = open(os.path.join(folder, "tex%s.txt" % os.environ["RDC_TEX"]), "w")


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
        want = int(os.environ["RDC_TEX"])
        tex = next((t for t in ctl.GetTextures() if int(t.resourceId) == want), None)
        if tex is None:
            log("no texture", want)
        else:
            log("texture", tex.resourceId, tex.type, tex.width, tex.height, tex.depth, tex.arraysize, tex.format.Name())
            for layer in [int(x) for x in os.environ.get("RDC_LAYERS", "0").split(";") if x]:
                s = rd.TextureSave()
                s.resourceId = tex.resourceId
                s.destType = rd.FileType.PNG
                s.mip = 0
                s.slice.sliceIndex = layer
                s.alpha = rd.AlphaMapping.Discard
                path = os.path.join(folder, "tex%d_l%d.png" % (want, layer))
                ctl.SaveTexture(s, path)
                sub = rd.Subresource(0, layer, 0)
                data = ctl.GetTextureData(tex.resourceId, sub)
                row = [tuple(data[i * 4:i * 4 + 4]) for i in range(min(tex.width, 16))]
                log("layer", layer, "row 0:", row)
                col = [tuple(data[(y * tex.width) * 4:(y * tex.width) * 4 + 4]) for y in range(min(tex.height, 16))]
                log("layer", layer, "column 0:", col)
        ctl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()
except Exception:
    log(traceback.format_exc())
out.close()
os._exit(0)
