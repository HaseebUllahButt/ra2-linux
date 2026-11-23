"""Keep the exact x87 stack resident across straight-line integer code.

The input is final generated C, after the game's patches. Only a small list
of integer expressions can sit between guest x87 instructions. Calls, labels,
branches, SIMD, control-word loads and unknown expressions close a region.
Unmasked exceptions use the original per-instruction path, including its waits.
"""
import re

_WRAPPER = re.compile(
    r'^    \{ uint32_t _xa = [^\n]+, _xf; x87_prepare\(\);\n'
    r'    __asm__ volatile\("frstor %2; [^\n]+\n'
    r'    x87_finish\(\);\n'
    r'(?:    (?:eax =|_flag_a =)[^\n]+\n)?'
    r'    \}', re.M)
# These inline helpers contain integer code only. Never admit an arbitrary
# function just because its name looks like a generated helper.
_CALLS = {'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
          'int8_t', 'int16_t', 'int32_t', 'int64_t', 'uintptr_t',
          'MEM8', 'MEM16', 'MEM32', 'MEM64', 'ADDR', 'PUSH32', 'POP32_VAL',
          'LO8', 'HI8', 'LO16', 'SET_LO8', 'SET_HI8', 'SET_LO16',
          'RECOMP_PF', 'FK_NARROW'}
_CALLS |= {'CMP_' + s for s in ('EQ','NE','L','LE','G','GE','B','BE','A','AE')}
_CALLS |= {'TEST_' + s for s in ('Z','NZ','L','LE','G','GE','S','NS')}
_NAMES = _CALLS | set('eax ecx edx ebx esp ebp esi edi _cf _df _flag_a _flag_b _flag_k _itail_tgt _fpu_cmp int unsigned signed char short long'.split())


def _integer(line):
    s = re.sub(r'/\*.*?\*/', '', line).strip()
    if not s:
        return True
    if not s.endswith(';') or any(c in s for c in '{}'):
        return False
    if re.search(r'\b(?:if|goto|return|double|float|g_st|g_x87|g_fpu_cw|g_fp_top)\b', s):
        return False
    names = set(re.findall(r'\b[A-Za-z_]\w*', s))
    if any(n not in _NAMES and not n.startswith('FK_') for n in names):
        return False
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', s))
    return calls <= _CALLS and ('=' in s or s.startswith('PUSH32('))


def _changes_control(wrapper):
    # fldcw, fldenv and frstor can unmask pending exceptions inside a region.
    # Keep these instructions on the original path.
    code = re.search(r'\.byte ([0-9,]+);', wrapper)
    if not code:
        return True
    b = [int(v) for v in code[1].split(',')]
    if b[0] == 102:
        b = b[1:]
    return len(b) >= 2 and ((b[0] == 217 and b[1] in (32, 40)) or
                           (b[0] == 221 and b[1] == 32))


def batch_exact_x87(body):
    """Batch safe regions, preserving the old path for unmasked exceptions."""
    if "/* exact x87 region */" in body:
        return body
    units, pos = [], 0
    for m in _WRAPPER.finditer(body):
        units.extend((False, line) for line in body[pos:m.start()].splitlines(True))
        units.append((not _changes_control(m[0]), m[0] + '\n'))
        pos = m.end()
        if body[pos:pos+1] == '\n':
            pos += 1
    units.extend((False, line) for line in body[pos:].splitlines(True))
    out, i = [], 0
    while i < len(units):
        if not units[i][0]:
            out.append(units[i][1]); i += 1; continue
        end, count, j = i + 1, 1, i + 1
        while j < len(units):
            exact, text = units[j]
            if exact:
                end = j + 1; count += 1
            elif not _integer(text):
                break
            j += 1
        if count < 2:
            out.append(units[i][1]); i += 1; continue
        original = ''.join(t for _, t in units[i:end])
        fast = original.replace('; x87_prepare();', ';')
        fast = fast.replace('frstor %2; ', '').replace('; fnsave %2; fldcw %2', '')
        fast = fast.replace('    x87_finish();\n', '')
        out.append('    if ((g_fpu_cw & 0x3fu) == 0x3fu) { /* exact x87 region */\n'
                   '    x87_batch_begin();\n' + fast +
                   '    x87_batch_end();\n    } else {\n' + original + '    }\n')
        i = end
    return ''.join(out)
