#!/usr/bin/env python3
"""Sign a staged mod folder and zip it as an HMG-API package, on the Mac (the key never goes to the car).

    tools/sign.py <mod dir> <dist dir> <key> <hmg-api checkout>
        writes <id>-v<version>.hmgapi.zip and, for a flash drive, <id>-v<version>-hmgapi-usb.zip
        (HMGAPI/updates/<id>-v<version>.zip inside) and <id>-v<version>-nemesis-usb.zip, and for the
        NEMESIS Package Store <id>-v<version>-nemesis-package.zip (both made by the checkout's
        tools/mknemesismod.sh, needs unzip and zip)

Same files and manifest as `hmgapi.engine.Signing sign`: SHA256withECDSA over
META-HMGAPI/manifest.json, the uuid derived from the public key. Needs openssl.
"""
import base64, hashlib, json, os, shutil, subprocess, sys, tempfile, zipfile

# what NEMESIS's store shows as compatible: only what the mod was tested on. A glob of
# ro.build.product, and the unit's /system/onebin folder name.
TESTED_OS = "CVEV.USA.*.260507"
TESTED_MODELS = "cvev_us"

META = "META-HMGAPI"


def openssl(*args, data=None):
    return subprocess.run(["openssl", *args], input=data, capture_output=True, check=True).stdout


def der_of(path):
    with open(path) as f:
        return base64.b64decode(f.read().strip())


def uuid_of(pub_der):
    d = bytearray(hashlib.sha256(pub_der).digest())
    d[6] = (d[6] & 15) | 128
    d[8] = (d[8] & 63) | 128
    h = d[:16].hex()
    return f"{h[:8]}-{h[8:12]}-{h[12:16]}-{h[16:20]}-{h[20:32]}"


def files_of(root):
    out = []
    for dirpath, dirs, names in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        dirs[:] = [d for d in dirs if not (rel == "." and d == META)]
        for n in names:
            if n == ".DS_Store" or (rel == "." and n == ".disabled"):
                continue
            out.append(n if rel == "." else f"{rel}/{n}")
    return sorted(out)


def swap_in_zip(path, name, change):
    """Rewrite one member of a zip, keeping the others and every member's attributes."""
    with zipfile.ZipFile(path) as z:
        items = [(i, z.read(i)) for i in z.infolist()]
    with zipfile.ZipFile(path, "w") as z:
        for i, d in items:
            z.writestr(i, change(d) if i.filename == name else d)


def tested_on(descriptor):
    """The tool writes os and models as "any"; a mod with a "system" would need this changed."""
    for k, v in (("os", TESTED_OS), ("models", TESTED_MODELS)):
        any_ = f'{k}="any"'.encode()
        if descriptor.count(any_) != 1:
            sys.exit(f"NEMESIS.data has no {k}=\"any\" to replace")
        descriptor = descriptor.replace(any_, f'{k}="{v}"'.encode())
    return descriptor


def nemesis_usb(signed, mod, hmgapi, dist):
    """HMG-API's own tool wraps the signed zip; it names the results <id>-<version>-nemesis[-package].zip."""
    tool = os.path.join(os.path.abspath(hmgapi), "tools", "mknemesismod.sh")
    with tempfile.TemporaryDirectory() as t:
        subprocess.run(["sh", tool, os.path.abspath(signed), t], check=True, capture_output=True)
        out = os.path.join(dist, f"{mod['id']}-v{mod['version']}-nemesis-usb.zip")
        store = os.path.join(dist, f"{mod['id']}-v{mod['version']}-nemesis-package.zip")
        shutil.move(os.path.join(t, f"{mod['id']}-{mod['version']}-nemesis.zip"), out)
        shutil.move(os.path.join(t, f"{mod['id']}-{mod['version']}-nemesis-package.zip"), store)
    swap_in_zip(store, "NEMESIS.data", tested_on)
    packed = open(store, "rb").read()
    swap_in_zip(out, f"packages/hmgapi.{mod['id']}.zip", lambda _: packed)
    return out, store


def build(mod_dir, dist, key, hmgapi):
    pub_path = key + ".pub"
    if not os.path.isfile(pub_path):
        sys.exit(f"no {pub_path}")
    pub = der_of(pub_path)
    mod = json.load(open(os.path.join(mod_dir, "mod.json")))
    files = {n: hashlib.sha256(open(os.path.join(mod_dir, n), "rb").read()).hexdigest() for n in files_of(mod_dir)}
    manifest = json.dumps({
        "format": 1, "id": mod["id"], "version": mod["version"],
        "uuid": uuid_of(pub), "key": base64.b64encode(pub).decode(), "files": files,
    }, indent=2).encode() + b"\n"

    with tempfile.TemporaryDirectory() as tmp:
        kpem = os.path.join(tmp, "k.pem")
        fd = os.open(kpem, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "wb") as f:
            f.write(openssl("pkey", "-inform", "DER", data=der_of(key)))
        sig = openssl("dgst", "-sha256", "-sign", kpem, data=manifest)
        # check against the public key the manifest carries, as the engine will
        ppem = os.path.join(tmp, "p.pem")
        sigf = os.path.join(tmp, "sig")
        open(ppem, "wb").write(openssl("pkey", "-pubin", "-inform", "DER", data=pub))
        open(sigf, "wb").write(sig)
        openssl("dgst", "-sha256", "-verify", ppem, "-signature", sigf, data=manifest)

    os.makedirs(dist, exist_ok=True)
    out = os.path.join(dist, f"{mod['id']}-v{mod['version']}.hmgapi.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{META}/manifest.json", manifest)
        z.writestr(f"{META}/signature", base64.b64encode(sig).decode() + "\n")
        for n in files:
            info = zipfile.ZipInfo.from_file(os.path.join(mod_dir, n), n)
            if n.endswith(".sh"):
                info.external_attr = 0o755 << 16
            z.writestr(info, open(os.path.join(mod_dir, n), "rb").read(), zipfile.ZIP_DEFLATED)
    usb = os.path.join(dist, f"{mod['id']}-v{mod['version']}-hmgapi-usb.zip")
    with zipfile.ZipFile(usb, "w", zipfile.ZIP_STORED) as z:
        z.write(out, f"HMGAPI/updates/{mod['id']}-v{mod['version']}.zip")
    print(f"signed {mod['id']} {mod['version']}: {len(files)} files, uuid {uuid_of(pub)}")
    print(out)
    print(usb)
    print(*nemesis_usb(out, mod, hmgapi, dist), sep="\n")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    build(*sys.argv[1:])
