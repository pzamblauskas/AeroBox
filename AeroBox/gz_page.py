# PlatformIO scenarijus (extra_scripts = pre:gz_page.py). Prieš KIEKVIENĄ kompiliavimą suspaudžia valdymo
# puslapį: components/net/www/index.html → index.html.gz. Taip į programą visada patenka tas pats puslapis
# kaip nesuspaustas. (Anksčiau .gz buvo kuriamas CMake konfigūravimo metu, ir kartais patekdavo senas.)
import gzip
import os

Import("env")  # noqa: F821 – funkciją pateikia PlatformIO

www = os.path.join(env.subst("$PROJECT_DIR"), "components", "net", "www")  # noqa: F821
src, dst = os.path.join(www, "index.html"), os.path.join(www, "index.html.gz")
data = open(src, "rb").read()
gz = gzip.compress(data, compresslevel=9, mtime=0)       # mtime=0: tas pats puslapis visada duoda tą patį failą
old = open(dst, "rb").read() if os.path.exists(dst) else b""
if old != gz:
    with open(dst, "wb") as f:
        f.write(gz)
    print(f"gz_page: index.html -> index.html.gz ({len(data)} -> {len(gz)} B)")
