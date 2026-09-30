#!/usr/bin/env python3
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOOKS = os.path.join(ROOT, 'src', 'app', 'code_hooks.cpp')
PACK = os.path.join(ROOT, 'Cemu', 'graphicspack', 'patch_tphd_tools.asm')

OVERRIDES = {
    0x026FD8A0: ['    lha   r11, 0xF18(r29)', '    subfic r8, r11, 13', '    blr'],
    0x0279525C: ['    addis r11, r10, 0x1007', '    lwz   r0, -0x3074(r11)', '    blr'],
}


def find_objdump():
    for name in ('powerpc-eabi-objdump', 'powerpc-eabi-objdump.exe'):
        path = shutil.which(name)
        if path:
            return path
    bases = [os.environ.get('DEVKITPPC', ''),
             os.path.join(os.environ.get('DEVKITPRO', ''), 'devkitPPC'),
             'C:/devkitPro/devkitPPC', '/opt/devkitpro/devkitPPC']
    for base in bases:
        for name in ('powerpc-eabi-objdump', 'powerpc-eabi-objdump.exe'):
            path = os.path.join(base, 'bin', name)
            if base and os.path.exists(path):
                return path
    raise SystemExit('powerpc-eabi-objdump not found; set DEVKITPPC')


class Rpx:
    def __init__(self, path):
        data = open(path, 'rb').read()
        shoff = struct.unpack_from('>I', data, 0x20)[0]
        entsize, count = struct.unpack_from('>HH', data, 0x2E)
        self.sections = []
        for i in range(count):
            _, kind, flags, addr, off, size = struct.unpack_from('>6I', data, shoff + i * entsize)
            if not addr or kind == 8 or not size or addr >= 0xC0000000:
                continue
            body = data[off:off + size]
            if flags & 0x08000000:
                body = zlib.decompress(body[4:])
            self.sections.append((addr, body))
        self.objdump = find_objdump()

    def locate(self, addr):
        for base, body in self.sections:
            if base <= addr < base + len(body):
                return base, body
        raise SystemExit('address %08X is not in the executable' % addr)

    def word(self, addr):
        base, body = self.locate(addr)
        return struct.unpack_from('>I', body, addr - base)[0]

    def disasm(self, addr, count):
        base, body = self.locate(addr)
        chunk = body[addr - base:addr - base + count * 4]
        with tempfile.NamedTemporaryFile(delete=False, suffix='.bin') as tmp:
            tmp.write(chunk)
        try:
            out = subprocess.run([self.objdump, '-D', '-b', 'binary', '-m', 'powerpc:750', '-EB',
                                  '--adjust-vma=0x%x' % addr, tmp.name],
                                 capture_output=True, text=True).stdout
        finally:
            os.unlink(tmp.name)
        insns = []
        for line in out.splitlines():
            m = re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s*(\S+)\s*(.*)', line)
            if m:
                insns.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
        return insns


def load_sites():
    src = open(HOOKS).read()
    body = src[src.index('static const Site kSites[] = {'):]
    body = body[:body.index('};')]
    pattern = r'\{\s*"(\w+)",\s*0x([0-9A-Fa-f]+)u,\s*0x([0-9A-Fa-f]+)u,\s*(CALL|JUMP)(?:_EXP)?,'
    return [(m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4))
            for m in re.finditer(pattern, body)]


class Generator:
    def __init__(self, rpx):
        self.rpx = rpx
        self.labels = {}

    def label(self, target):
        name = '_g_%08X' % target
        self.labels[target] = name
        return name

    def operands(self, mnem, ops):
        if mnem.startswith('b') and mnem not in ('blr', 'bctr', 'bctrl', 'blrl'):
            parts = [p.strip() for p in ops.split(',')]
            if parts[-1].startswith('0x'):
                parts[-1] = self.label(int(parts[-1], 16))
            return mnem.rstrip('-+'), ', '.join(parts)

        def hexdisp(m):
            v = int(m.group(1))
            return ('-0x%X' % -v if v < 0 else '0x%X' % v) + '(' + m.group(2) + ')'
        ops = re.sub(r'(-?\d+)\((r\d+)\)', hexdisp, ops)
        return mnem, ', '.join(p.strip() for p in ops.split(',')) if ops else ''

    def reads_r11(self, mnem, ops):
        parts = [p.strip() for p in ops.split(',')] if ops else []
        if not parts:
            return False
        if mnem.startswith('st') or mnem.startswith('cmp'):
            return any('r11' in p for p in parts)
        return any(re.search(r'\br11\b', p) for p in parts[1:])

    def r11_source(self, addr):
        chain = []
        for a, mnem, ops in reversed(self.rpx.disasm(addr - 4 * 24, 24)):
            parts = [p.strip() for p in ops.split(',')]
            if not parts or parts[0] != 'r11':
                continue
            if mnem == 'lis':
                chain.append('    lis   r11, 0x%X' % (int(parts[1], 0) & 0xFFFF))
                return list(reversed(chain))
            if mnem in ('lwz', 'lhz', 'lbz', 'lha') and '(r11)' in ops and not chain:
                m, o = self.operands(mnem, ops)
                chain.append('    %-5s %s' % (m, o))
                continue
            break
        raise SystemExit('cannot rebuild r11 before %08X; add an override' % addr)

    def fallback(self, addr, shape):
        if addr in OVERRIDES:
            return OVERRIDES[addr]
        _, mnem, ops = self.rpx.disasm(addr, 1)[0]
        mnem, ops = self.operands(mnem, ops)
        if mnem == 'bl':
            if shape == 'JUMP':
                raise SystemExit('JUMP hook over a bl at %08X' % addr)
            return ['    b     %s' % ops]
        if mnem == 'blr':
            return ['    blr']
        lines = self.r11_source(addr) if self.reads_r11(mnem, ops) else []
        lines.append('    %-5s %s' % (mnem, ops) if ops else '    %s' % mnem)
        lines.append('    blr' if shape == 'CALL' else '    b     %s' % self.label(addr + 4))
        return lines

    def build(self, sites):
        table, tramps, falls, patches = [], [], [], []
        for i, (name, addr, expected, shape) in enumerate(sites):
            live = self.rpx.word(addr)
            if live != expected:
                raise SystemExit('%s: %08X holds %08X, code_hooks.cpp expects %08X' %
                                 (name, addr, live, expected))
            table.append('_pkh_%d:\n.int _hkf_%d' % (i, i))
            tramps.append('_hkt_%d:\n    lis   r11, _pkh_%d@ha\n    lwz   r11, _pkh_%d@l(r11)\n'
                          '    mtctr r11\n    bctr' % (i, i, i))
            falls.append('_hkf_%d:\n%s' % (i, '\n'.join(self.fallback(addr, shape))))
            patches.append('0x%08X = %s _hkt_%d' % (addr, 'bla' if shape == 'CALL' else 'b  ', i))
        labels = ['0x%08X = %s:' % (t, n) for t, n in sorted(self.labels.items())]
        return labels, table, tramps, falls, patches


def splice(labels, table, tramps, falls, patches, count):
    asm = open(PACK).read().replace('\r\n', '\n')
    asm = re.sub(r'^0x[0-9A-F]{8} = _g_[0-9A-F]{8}:\n', '', asm, flags=re.M)
    asm = re.sub(r'_pk_hook_count:\n.*?(?=\n_rpl_state:)', '', asm, flags=re.S)
    asm = re.sub(r'_hkt_0:\n.*?(?=\ncounter_hook:)', '', asm, flags=re.S)
    asm = re.sub(r'^0x[0-9A-F]{8} = (?:bla|b  ) _hkt_\d+\n', '', asm, flags=re.M)
    asm = re.sub(r'\n{3,}', '\n\n', asm)

    anchor = '0x02BEF83C = _cCt_Counter_rest:\n'
    asm = asm.replace(anchor, anchor + ''.join(l + '\n' for l in labels))
    block = '_pk_hook_count:\n.int %d\n%s' % (count, '\n'.join(table))
    asm = asm.replace('\n_rpl_state:', '\n' + block + '\n\n_rpl_state:', 1)
    code = '\n\n'.join(tramps) + '\n\n' + '\n\n'.join(falls)
    asm = asm.replace('\ncounter_hook:', '\n' + code + '\n\ncounter_hook:', 1)
    asm = asm.rstrip('\n') + '\n\n' + '\n'.join(patches) + '\n'
    open(PACK, 'w', newline='\n').write(re.sub(r'\n{3,}', '\n\n', asm))


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: pack_hooks.py <path to Zelda.rpx>')
    sites = load_sites()
    labels, table, tramps, falls, patches = Generator(Rpx(sys.argv[1])).build(sites)
    splice(labels, table, tramps, falls, patches, len(sites))
    print('%d hook sites written to %s' % (len(sites), os.path.relpath(PACK, ROOT)))


if __name__ == '__main__':
    main()
