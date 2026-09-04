# PlatformIO pre-action: compress the web UI into the filesystem image.
#
# The UI source lives in ui/index.html and is edited directly -- there is still no build step in
# the sense of a bundler, Babel runs in the browser. What this does is gzip it into
# data/index.html.gz, which is what actually ships, because the filesystem partition is the
# tight budget on this device: 896KB total, and the uncompressed UI alone was ~216KB of it.
# Compressed it is roughly a quarter of that.
#
# Wired as a pre-action on buildfs/uploadfs rather than left as a manual command on purpose --
# a forgotten compression step would ship a stale UI, and that failure is silent.
Import("env")
import gzip, os, shutil

SRC = "ui/index.html"
DST = "data/index.html.gz"

def build_ui(source, target, env):
    if not os.path.exists(SRC):
        raise Exception("%s is missing -- that is the UI source" % SRC)
    if os.path.exists(DST) and os.path.getmtime(DST) >= os.path.getmtime(SRC):
        print("index.html.gz is current")
        return
    os.makedirs(os.path.dirname(DST), exist_ok=True)
    raw = os.path.getsize(SRC)
    # mtime=0 so the output is byte-identical for identical input; otherwise every build would
    # produce a different file and the filesystem image would look changed when it is not.
    with open(SRC, "rb") as fi, gzip.GzipFile(DST, "wb", compresslevel=9, mtime=0) as fo:
        shutil.copyfileobj(fi, fo)
    print("index.html %d -> %d bytes gzipped (%.0f%% saved)" % (raw, os.path.getsize(DST),
                                                                100.0 * (1 - os.path.getsize(DST) / raw)))

# Run it NOW, while the project is being configured, not as a pre-action. PlatformIO builds the
# list of files to pack into the filesystem image when this script is evaluated, so a file that a
# pre-action creates later is simply not in the image -- which happened on the first attempt:
# data/index.html.gz existed on disk, the build reported compressing it, and the device came up
# showing the "no UI installed" page because the image did not contain it.
build_ui(None, None, env)

# Kept as well, so an incremental `buildfs` after an edit still refreshes the file.
env.AddPreAction("buildfs", build_ui)
env.AddPreAction("uploadfs", build_ui)
