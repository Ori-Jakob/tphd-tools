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

REST_LABEL = '0x02BEF83C = _cCt_Counter_rest:\n'
TABLE_START = '_pk_version:\n.int 2\n'
TABLE_END = '\n_rpl_state:'
CODE_START = 'kpad_done:\n    lwz   r3, 0x0c(r1)\n    lwz   r0, 0x24(r1)\n    mtlr  r0\n    addi  r1, r1, 0x30\n    blr\n'
PATCH_START = '0x02BEF838 = b   counter_hook\n'
PATCH_END = '0x02BFCEAC = bla kpad_read_hook\n'

INVERSE = {
    'beq': 'bne', 'bne': 'beq', 'blt': 'bge', 'bge': 'blt',
    'bgt': 'ble', 'ble': 'bgt', 'bso': 'bns', 'bns': 'bso',
}

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


def snake(name):
    return re.sub(r'(?<=[a-z0-9])([A-Z])', r'_\1', name).lower()


def load_groups():
    src = open(HOOKS).read()
    body = src[src.index('static const Site kSites[] = {'):]
    body = body[:body.index('};')]
    pattern = r'\{\s*"(\w+)",\s*0x([0-9A-Fa-f]+)u,\s*0x([0-9A-Fa-f]+)u,\s*(CALL|JUMP)(?:_EXP)?,'
    groups = [[]]
    for line in body.splitlines()[1:]:
        if not line.strip():
            if groups[-1]:
                groups.append([])
            continue
        m = re.search(pattern, line)
        if m:
            groups[-1].append((snake(m.group(1)), int(m.group(2), 16), int(m.group(3), 16),
                               m.group(4)))
    return [g for g in groups if g]


class Generator:
    def __init__(self, rpx):
        self.rpx = rpx
        self.labels = {}

    def label(self, target, name):
        return self.labels.setdefault(target, name)

    def operands(self, name, mnem, ops):
        if mnem.startswith('b') and mnem not in ('blr', 'bctr', 'bctrl', 'blrl'):
            parts = [p.strip() for p in ops.split(',')]
            if parts[-1].startswith('0x'):
                kind = 'call' if mnem == 'bl' else 'target'
                parts[-1] = self.label(int(parts[-1], 16), '_%s_%s' % (name, kind))
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

    def r11_source(self, name, addr):
        chain = []
        for _, mnem, ops in reversed(self.rpx.disasm(addr - 4 * 24, 24)):
            parts = [p.strip() for p in ops.split(',')]
            if not parts or parts[0] != 'r11':
                continue
            if mnem == 'lis':
                chain.append('    lis   r11, 0x%X' % (int(parts[1], 0) & 0xFFFF))
                return list(reversed(chain))
            if mnem in ('lwz', 'lhz', 'lbz', 'lha') and '(r11)' in ops and not chain:
                m, o = self.operands(name, mnem, ops)
                chain.append('    %-5s %s' % (m, o))
                continue
            break
        raise SystemExit('cannot rebuild r11 before %08X; add an override' % addr)

    def native(self, name, addr, shape):
        if addr in OVERRIDES:
            return OVERRIDES[addr]
        _, mnem, ops = self.rpx.disasm(addr, 1)[0]
        mnem, ops = self.operands(name, mnem, ops)
        def rest():
            if shape == 'CALL':
                return '    blr'
            return '    b     %s' % self.label(addr + 4, '_%s_rest' % name)
        if mnem in INVERSE:
            parts = [p.strip() for p in ops.split(',')]
            skip = '%s_skip' % name
            return ['    %-5s %s' % (INVERSE[mnem], ', '.join(parts[:-1] + [skip])),
                    '    b     %s' % parts[-1], skip + ':', rest()]
        if mnem == 'bl':
            if shape == 'JUMP':
                raise SystemExit('JUMP hook over a bl at %08X' % addr)
            return ['    b     %s' % ops]
        if mnem == 'blr':
            return ['    blr']
        lines = self.r11_source(name, addr) if self.reads_r11(mnem, ops) else []
        lines.append('    %-5s %s' % (mnem, ops) if ops else '    %s' % mnem)
        lines.append(rest())
        return lines

    def build(self, groups):
        count = sum(len(g) for g in groups)
        table = ['_pk_hook_count:', '.int %d' % count]
        code, patches = [], []
        for group in groups:
            table.append('')
            patches.append('')
            for name, addr, expected, shape in group:
                live = self.rpx.word(addr)
                if live != expected:
                    raise SystemExit('%s: %08X holds %08X, code_hooks.cpp expects %08X' %
                                     (name, addr, live, expected))
                table += ['_pk_%s:' % name, '.int %s_native' % name]
                code += ['', '%s_hook:' % name,
                         '    lis   r11, _pk_%s@ha' % name,
                         '    lwz   r11, _pk_%s@l(r11)' % name,
                         '    mtctr r11',
                         '    bctr',
                         '%s_native:' % name] + self.native(name, addr, shape)
                patches.append('0x%08X = %s %s_hook' % (addr, 'bla' if shape == 'CALL' else 'b  ', name))
        labels = ['0x%08X = %s:' % (t, n) for t, n in sorted(self.labels.items())]
        return labels, table, code, patches


def cut(text, start, end):
    a = text.index(start) + len(start)
    b = text.index(end, a) if end else len(text)
    return text[:a], text[b:]


def splice(labels, table, code, patches):
    asm = open(PACK).read().replace('\r\n', '\n')

    head, rest = cut(asm, REST_LABEL, '\n.origin = codecave')
    asm = head + ''.join(l + '\n' for l in labels) + rest

    head, rest = cut(asm, TABLE_START, TABLE_END)
    asm = head + '\n' + '\n'.join(table) + '\n' + rest

    head, rest = cut(asm, CODE_START, PATCH_START)
    asm = head + '\n'.join(code) + '\n\n' + rest

    head, _ = cut(asm, PATCH_END, None)
    asm = head + '\n'.join(patches) + '\n'

    open(PACK, 'w', newline='\n').write(re.sub(r'\n{3,}', '\n\n', asm))


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: pack_hooks.py <path to Zelda.rpx>')
    groups = load_groups()
    labels, table, code, patches = Generator(Rpx(sys.argv[1])).build(groups)
    splice(labels, table, code, patches)
    print('%d hook sites written to %s' % (sum(len(g) for g in groups), os.path.relpath(PACK, ROOT)))


if __name__ == '__main__':
    main()
