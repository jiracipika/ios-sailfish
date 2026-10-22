#!/usr/bin/env python3
"""Unpack an Android boot image (header v0-v4) into kernel/ramdisk/dtb + header.txt.

The Pixel 7 (gs201) ships v4 boot + v4 vendor_boot images: GKI kernel in boot,
DTB and vendor ramdisk in vendor_boot. Pass the vendor_boot image as the third
argument to also unpack it (vendor ramdisk fragments, dtb, ramdisk table,
bootconfig):

    tools/unpack_bootv2.py backup/boot.img dtb/boot_unpacked [backup/vendor_boot.img]

Layouts per AOSP system/tools/mkbootimg include/bootimg/bootimg.h. Self-test
(synthetic images, no device needed):
    tools/unpack_bootv2.py --selftest TMPDIR
"""
import struct, sys, os

BOOT_MAGIC = b'ANDROID!'
VNDR_MAGIC = b'VNDRBOOT'
RAMDISK_TYPE = {0: 'none', 1: 'platform', 2: 'recovery', 3: 'dlkm'}


def unpack_boot(b, out):
    assert b[:8] == BOOT_MAGIC, 'not a boot image'
    ver = struct.unpack_from('<I', b, 40)[0]
    rnd = lambda n, ps: (n + ps - 1) // ps * ps
    if ver <= 2:
        (ks, ka, rs, ra, ss, sa, ta, ps, _, osv) = struct.unpack_from('<10I', b, 8)
        info = dict(kernel_size=ks, kernel_addr=hex(ka), ramdisk_size=rs, ramdisk_addr=hex(ra),
                    second_size=ss, tags_addr=hex(ta), page_size=ps, header_version=ver,
                    os_version=hex(osv), name=b[48:64].split(b'\0')[0].decode(),
                    cmdline=(b[64:64+512].split(b'\0')[0] + b[608:608+1024].split(b'\0')[0]).decode())
        off, parts = ps, [('kernel', ks), ('ramdisk', rs), ('second', ss)]
        if ver >= 1:
            rds, rdo, hs = struct.unpack_from('<IQI', b, 1632)
            parts.append(('recovery_dtbo', rds)); info['header_size'] = hs
        if ver >= 2:
            ds, da = struct.unpack_from('<IQ', b, 1648)
            parts.append(('dtb', ds)); info['dtb_addr'] = hex(da)
    elif ver in (3, 4):
        ks, rs, osv, hs = struct.unpack_from('<4I', b, 8)
        assert hs == (1584 if ver == 4 else 1580), f'bad v{ver} header_size {hs}'
        info = dict(kernel_size=ks, ramdisk_size=rs, os_version=hex(osv), header_size=hs,
                    header_version=ver, page_size=4096,
                    cmdline=b[44:44+1536].split(b'\0')[0].decode())
        ps = 4096
        off, parts = ps, [('kernel', ks), ('ramdisk', rs)]
        if ver == 4:
            sig, = struct.unpack_from('<I', b, 1580)
            parts.append(('boot_signature', sig)); info['signature_size'] = sig
    else:
        raise SystemExit(f'unsupported boot header version {ver}')
    for name, sz in parts:
        if sz:
            open(f'{out}/{name}', 'wb').write(b[off:off+sz])
        info[name + '_offset'] = off
        off += rnd(sz, ps)
    return info


def unpack_vendor_boot(b, out):
    assert b[:8] == VNDR_MAGIC, 'not a vendor_boot image'
    ver, ps = struct.unpack_from('<II', b, 8)
    ka, ra, vrs = struct.unpack_from('<3I', b, 16)
    assert ps == 4096, f'unexpected vendor page size {ps}'
    hs, ds = struct.unpack_from('<II', b, 2096)
    da, = struct.unpack_from('<Q', b, 2104)
    info = dict(header_version=ver, page_size=ps, kernel_addr=hex(ka), ramdisk_addr=hex(ra),
                vendor_ramdisk_size=vrs, header_size=hs, dtb_size=ds, dtb_addr=hex(da),
                name=b[2080:2096].split(b'\0')[0].decode(),
                cmdline=b[28:28+2048].split(b'\0')[0].decode())
    rnd = lambda n: (n + ps - 1) // ps * ps
    off = ps
    for name, sz in [('vendor_ramdisk', vrs), ('dtb', ds)]:
        if sz:
            open(f'{out}/{name}', 'wb').write(b[off:off+sz])
        info[name + '_offset'] = off
        off += rnd(sz)
    if ver >= 4:
        trs, tnum, tesz, bcs = struct.unpack_from('<4I', b, 2112)
        assert tesz == 108, f'unexpected ramdisk table entry size {tesz}'
        info.update(vendor_ramdisk_table_size=trs, vendor_ramdisk_table_entry_num=tnum,
                    vendor_ramdisk_table_entry_size=tesz, bootconfig_size=bcs,
                    vendor_ramdisk_table_offset=off)
        table = b[off:off+trs]; off += rnd(trs)
        entries = []
        for i in range(tnum):
            e = table[i*tesz:(i+1)*tesz]
            rsz, roff, rtyp = struct.unpack_from('<3I', e, 0)
            name = e[12:44].split(b'\0')[0].decode()
            entries.append(dict(size=rsz, offset=roff, type=RAMDISK_TYPE.get(rtyp, rtyp), name=name))
            if rsz:  # split the fragments out of the vendor ramdisk blob
                open(f'{out}/vendor_ramdisk_{name or RAMDISK_TYPE.get(rtyp, rtyp)}', 'wb').write(
                    b[info['vendor_ramdisk_offset'] + roff: info['vendor_ramdisk_offset'] + roff + rsz])
        if entries:
            open(f'{out}/vendor_ramdisk_table.txt', 'w').write(
                '\n'.join(str(e) for e in entries) + '\n')
        if bcs:
            open(f'{out}/bootconfig', 'wb').write(b[off:off+bcs])
        info['bootconfig_offset'] = off
    return info


def write_info(out, info, tag=''):
    path = f'{out}/header{"-" + tag if tag else ""}.txt'
    with open(path, 'w') as f:
        for k, v in info.items():
            f.write(f'{k}: {v}\n')
    print(f'--- {tag or "boot"} header ---')
    print(open(path).read())


def repack_boot(src, kernel_file, dst):
    """Replace the kernel inside a boot image (v0-v4), keeping everything else.
    For v4 the boot signature is dropped (allowed with an unlocked bootloader).
    This is how a shim kernel gets booted on a Pixel 7: fastboot boot a repacked
    stock image, nothing is flashed."""
    b = bytearray(open(src, 'rb').read())
    k = open(kernel_file, 'rb').read()
    ver = struct.unpack_from('<I', b, 40)[0]
    if ver <= 2:
        ps, = struct.unpack_from('<I', b, 36)
        ks, = struct.unpack_from('<I', b, 8)            # old kernel size
        struct.pack_into('<I', b, 8, len(k))            # new kernel size
        rnd = lambda n: (n + ps - 1) // ps * ps
        rs, = struct.unpack_from('<I', b, 16)
        ss, = struct.unpack_from('<I', b, 24)
        rds = struct.unpack_from('<I', b, 1632)[0] if ver >= 1 else 0
        ds = struct.unpack_from('<I', b, 1648)[0] if ver >= 2 else 0
        # layout: header page, kernel, ramdisk, second, [recovery_dtbo, dtb]
        layout = [(ps, ks)]
        o = ps + rnd(ks)
        for sz in (rs, ss, rds, ds):
            if sz:
                layout.append((o, sz)); o += rnd(sz)
        out = bytearray(b[:ps])                          # patched header page
        out += k + b'\0' * (-len(k) % ps)
        for o, sz in layout[1:]:
            out += b[o:o+sz] + b'\0' * (-sz % ps)
    elif ver in (3, 4):
        ps = 4096
        ks, rs = struct.unpack_from('<II', b, 8)
        struct.pack_into('<I', b, 8, len(k))            # new kernel size
        rnd = lambda n: (n + ps - 1) // ps * ps
        if ver == 4:
            struct.pack_into('<I', b, 1580, 0)          # drop the boot signature
        out = bytearray(b[:ps])                          # patched header page
        out += k + b'\0' * (-len(k) % ps)
        o_ram = ps + rnd(ks)                            # ramdisk follows old kernel
        if rs:
            out += b[o_ram:o_ram+rs] + b'\0' * (-rs % ps)
        # v3 ends here; a v4 signature section is intentionally not copied
    else:
        raise SystemExit(f'unsupported boot header version {ver}')
    open(dst, 'wb').write(bytes(out))
    print(f'{dst}: kernel {ks} -> {len(k)} bytes (header v{ver})')


# ---- self-test: synthetic v2 / v4 boot + v4 vendor_boot round-trips ----

def selftest(tmp):
    aligned = lambda n: n + b'\0' * (-len(n) % 4096)
    os.makedirs(tmp, exist_ok=True)

    # v2 (Pixel 1 era): page 2048, all optional sections
    ps = 2048
    h2 = BOOT_MAGIC + struct.pack('<10I', 1000, 0x100, 300, 0x200, 64, 0x300, 0x400, ps, 2, 0)
    h2 += b'pixel1test'.ljust(16, b'\0') + b'console=ttyMSM0'.ljust(512, b'\0')
    h2 += b'\0' * 32 + b'androidboot.v2=yes'.ljust(1024, b'\0')
    h2 += struct.pack('<IQI', 128, 0x9000, 1648) + struct.pack('<IQ', 512, 0xa000)
    h2 = h2.ljust(1660, b'\0')
    pad2 = lambda n: n + b'\0' * (-len(n) % ps)
    img2 = (pad2(h2) + pad2(b'K' * 1000) + pad2(b'R' * 300) + pad2(b'S' * 64)
            + pad2(b'D' * 128) + pad2(b'T' * 512))
    p2 = os.path.join(tmp, 'boot_v2.img'); open(p2, 'wb').write(img2)

    # v4 boot (Pixel 7 era): 4096 pages, signature section after ramdisk
    h4 = (BOOT_MAGIC + struct.pack('<4I', 900, 250, 0, 1584) + b'\0' * 16
          + struct.pack('<I', 4) + b'stackdepot=1'.ljust(1536, b'\0') + struct.pack('<I', 300))
    assert len(h4) == 1584
    img4 = (aligned(h4) + aligned(b'K' * 900) + aligned(b'R' * 250) + aligned(b'S' * 300))
    p4 = os.path.join(tmp, 'boot_v4.img'); open(p4, 'wb').write(img4)

    # v4 vendor_boot: ramdisk fragments + dtb + ramdisk table + bootconfig
    entry = lambda sz, off, typ, name: (struct.pack('<3I', sz, off, typ)
                                        + name.ljust(32, b'\0') + struct.pack('<16I', *([0] * 16)))
    table = entry(300, 0, 3, b'dlkm') + entry(400, 300, 1, b'platform')
    assert len(table) == 216 and len(entry(0, 0, 0, b'')) == 108
    vh = (VNDR_MAGIC + struct.pack('<IIIII', 4, 4096, 0x80080000, 0x83000000, 700)
          + b'androidboot.vendor_ramdisk_fragment=test'.ljust(2048, b'\0')
          + struct.pack('<I', 0x40000000) + b'panther'.ljust(16, b'\0')
          + struct.pack('<IIQ', 2128, 1024, 0x90000000)
          + struct.pack('<4I', 216, 2, 108, 88))
    assert len(vh) == 2128
    vimg = (aligned(vh) + aligned(b'V' * 300 + b'P' * 400) + aligned(b'D' * 1024)
            + aligned(table) + aligned(b'B' * 88))
    pv = os.path.join(tmp, 'vendor_boot_v4.img'); open(pv, 'wb').write(vimg)

    import subprocess
    here = os.path.abspath(sys.argv[0])
    def run(args):
        r = subprocess.run([sys.executable, here] + args, capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
    run([p2, tmp + '/out2'])
    assert open(tmp + '/out2/kernel', 'rb').read() == b'K' * 1000
    assert open(tmp + '/out2/second', 'rb').read() == b'S' * 64
    assert open(tmp + '/out2/recovery_dtbo', 'rb').read() == b'D' * 128
    assert open(tmp + '/out2/dtb', 'rb').read() == b'T' * 512
    assert 'header_version: 2' in open(tmp + '/out2/header.txt').read()

    run([p4, tmp + '/out4'])
    assert open(tmp + '/out4/kernel', 'rb').read() == b'K' * 900
    assert open(tmp + '/out4/boot_signature', 'rb').read() == b'S' * 300
    assert 'signature_size: 300' in open(tmp + '/out4/header.txt').read()

    run([p4, tmp + '/outv', pv])
    assert open(tmp + '/outv/dtb', 'rb').read() == b'D' * 1024
    assert open(tmp + '/outv/vendor_ramdisk_dlkm', 'rb').read() == b'V' * 300
    assert open(tmp + '/outv/vendor_ramdisk_platform', 'rb').read() == b'P' * 400
    assert open(tmp + '/outv/bootconfig', 'rb').read() == b'B' * 88
    assert 'header_version: 4' in open(tmp + '/outv/header-vendor.txt').read()
    assert "type': 'dlkm'" in open(tmp + '/outv/vendor_ramdisk_table.txt').read()

    # repack: swap the kernel, verify the image still round-trips
    open(tmp + '/newkernel', 'wb').write(b'N' * 500)
    run(['--repack', p4, tmp + '/newkernel', tmp + '/boot_v4_repacked.img'])
    run([tmp + '/boot_v4_repacked.img', tmp + '/out4r'])
    assert open(tmp + '/out4r/kernel', 'rb').read() == b'N' * 500
    assert open(tmp + '/out4r/ramdisk', 'rb').read() == b'R' * 250
    assert 'signature_size: 0' in open(tmp + '/out4r/header.txt').read()
    run(['--repack', p2, tmp + '/newkernel', tmp + '/boot_v2_repacked.img'])
    run([tmp + '/boot_v2_repacked.img', tmp + '/out2r'])
    assert open(tmp + '/out2r/kernel', 'rb').read() == b'N' * 500
    assert open(tmp + '/out2r/dtb', 'rb').read() == b'T' * 512

    print('SELFTEST OK: v2 boot, v4 boot, v4 vendor_boot and repack round-trips verified')


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == '--selftest':
        selftest(sys.argv[2] if len(sys.argv) > 2 else '/tmp/boot-unpack-selftest')
        return
    if len(sys.argv) >= 5 and sys.argv[1] == '--repack':
        repack_boot(sys.argv[2], sys.argv[3], sys.argv[4])
        return
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    info = unpack_boot(open(src, 'rb').read(), out)
    write_info(out, info)
    if len(sys.argv) > 3:
        write_info(out, unpack_vendor_boot(open(sys.argv[3], 'rb').read(), out), tag='vendor')


if __name__ == '__main__':
    main()
