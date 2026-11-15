#!/usr/bin/env python3
"""Generate the offline single-character keyboard table from pinyin-data v0.16.0.

Usage: python3 screen/devui/tools/generate_pinyin.py /path/to/pinyin.txt
The source file is MIT licensed; see ../third_party/pinyin-data-LICENSE.
"""
import collections
import hashlib
import re
import sys
import unicodedata
from pathlib import Path

source = Path(sys.argv[1]).read_bytes()
expected = '2342a6f98c85dbc3c8d26aef31744876a36326c6466e51e9a0f16898c36c2011'
if hashlib.sha256(source).hexdigest() != expected:
    raise SystemExit('Unexpected pinyin-data source; verify provenance before updating')
groups = collections.defaultdict(set)
for line in source.decode('utf-8').splitlines():
    match = re.match(r'U\+([0-9A-F]+):\s*([^#]+)', line)
    if not match:
        continue
    cp = int(match.group(1), 16)
    if not 0x3400 <= cp <= 0x9fff:
        continue
    for reading in match.group(2).strip().split(','):
        reading = unicodedata.normalize('NFD', reading.lower()).replace('u\u0308', 'v')
        reading = ''.join(c for c in reading if unicodedata.category(c) != 'Mn')
        if re.fullmatch('[a-z]{1,8}', reading):
            groups[reading].add(chr(cp))
# Small hand-curated prefix puts ordinary names and messages before rare readings.
common = '你好吗我是的了不人在有个这中大为上国和到说们地也子时道出而要于就下得可会对生能都然家电信网机测延迟目名称设备手机路由器服务工作学习今天明天后现在需要使用连接成功失败发送收到谢谢再见中文英文输入测试地址远程系统时间数字'
rank = {c: i for i, c in reversed(list(enumerate(common)))}
def order(char):
    if char in rank:
        return (0, rank[char])
    try:
        return (1, char.encode('gb2312'))
    except UnicodeEncodeError:
        return (2, char.encode('utf-8'))

output = ['/* Generated from pinyin-data v0.16.0; see third_party/pinyin-data-LICENSE.',
          ' * Source SHA256: ' + expected + ' */',
          'static const struct { const char *syllable, *characters; } pinyin_data[] = {']
for syllable, chars in sorted(groups.items()):
    output.append('  {"' + syllable + '", "' + ''.join(sorted(chars, key=order)) + '"},')
output.append('};\n')
Path(__file__).resolve().parents[1].joinpath('pinyin_data.h').write_text('\n'.join(output))
