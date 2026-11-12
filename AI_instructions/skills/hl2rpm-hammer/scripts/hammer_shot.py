"""Picture of the Hammer++ window itself (what the user sees: 2D views, 3D view, dialogs) - PrintWindow of that one
window only (never the whole screen: other windows must not end up in the picture), also when it is covered.
usage: python hammer_shot.py <out.png> [--title Hammer] [--list]
At start Hammer++ may ask about an update (a small "Hammer++ Autoupdater" box): the main window appears only after
the user answers it - do not click it for the user."""
import ctypes
import ctypes.wintypes as wt
import sys

from PIL import Image

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # per monitor v2: real pixel sizes
except Exception:
    pass


def find_window(part):
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            if n:
                buf = ctypes.create_unicode_buffer(n + 1)
                user32.GetWindowTextW(hwnd, buf, n + 1)
                if part.lower() in buf.value.lower():
                    found.append((hwnd, buf.value))
        return True
    user32.EnumWindows(cb, 0)
    return found


def capture(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    hdc = user32.GetWindowDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(hwnd, mdc, 2)          # PW_RENDERFULLCONTENT
    class BMI(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]
    bmi = BMI(ctypes.sizeof(BMI), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    return Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    title = argv[argv.index("--title") + 1] if "--title" in argv else "Hammer"
    wins = [w for w in find_window(title) if "visual studio" not in w[1].lower()]
    if not wins:
        print("no window with %r in the title (is Hammer++ running?)" % title)
        return 2
    # the biggest one is the main window (not the update prompt or a dialog)
    def area(w):
        r = wt.RECT()
        user32.GetWindowRect(w[0], ctypes.byref(r))
        return (r.right - r.left) * (r.bottom - r.top)
    wins.sort(key=area, reverse=True)
    if "--list" in argv:
        for w in wins:
            print(area(w), w[1])
    hwnd, name = wins[0]
    capture(hwnd).save(argv[1])
    print("captured %r -> %s" % (name, argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
