import sys
from PIL import Image

src = sys.argv[1]
dst = sys.argv[2]
with open(src, "rb") as f:
    assert f.readline().strip() == b"P6"
    w, h = map(int, f.readline().split())
    assert f.readline().strip() == b"255"
    data = f.read()
im = Image.frombytes("RGB", (w, h), data)
im.save(dst)
print(dst, w, h)
