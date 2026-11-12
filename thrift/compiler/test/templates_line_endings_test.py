#!/usr/bin/env python3
"""CRLF and LF templates must compile to identical bytes and recorded lengths."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--builder', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='thrift-template-endings-') as temp:
    folder = Path(temp)
    body = b'<%!\ncomment\n%>\nhello {{name}}\n'
    inputs = {'lf': body, 'crlf': body.replace(b'\n', b'\r\n'),
              'cr': body.replace(b'\n', b'\r'),
              'mixed': body.replace(b'\n', b'\r\n', 1).replace(b'comment\n', b'comment\r'),
              'blank': b'A\r\r\nB\r'}
    expected = {name: len(body) for name in inputs}
    expected['blank'] = len(b'A\n\nB\n')
    for name, data in inputs.items():
        (folder/(name+'.mustache')).write_bytes(data)
    result = subprocess.run([args.builder, str(folder)], check=True, capture_output=True)
    text = result.stdout.decode()
    sizes = re.search(r'templates_content_sizes\[\] = \{(.*?)\};', text, re.S).group(1)
    counts = [int(n) for n in re.findall(r'^([0-9]+),$', sizes, re.M)]
    assert counts == [expected[name] for name in sorted(inputs)], counts
    assert b'\r' not in result.stdout, 'raw literal still has CRLF'
    assert result.stdout.count(body) == 4, 'template body changed'
    assert result.stdout.count(b'A\n\nB\n') == 1, 'mixed blank lines changed'
print('PASS LF/CRLF/CR/mixed embedded bytes and exact recorded lengths')
