#!/usr/bin/env python3
"""Mechanical checks of a Qt .ts translation: placeholders, HTML tags, link targets, accelerators."""
import re, sys, json, collections
import xml.etree.ElementTree as ET

PH = re.compile(r'%(?:L?\d+|n)')
TAG = re.compile(r'<(/?)([a-zA-Z][a-zA-Z0-9]*)')   # a tag has no space after '<'
ATTR = re.compile(r'''(href|onclick|src|name|id|class|style)\s*=\s*(['"])(.*?)\2''', re.I | re.S)
URL = re.compile(r'(?:https?|mailto|file)://[^\s\'"<>)]+')

def check(src, tr):
    issues = []
    a, b = collections.Counter(PH.findall(src)), collections.Counter(PH.findall(tr))
    if a != b: issues.append('placeholders %s vs %s' % (dict(a), dict(b)))
    ta = collections.Counter((s, n.lower()) for s, n in TAG.findall(src))
    tb = collections.Counter((s, n.lower()) for s, n in TAG.findall(tr))
    if ta != tb: issues.append('tags differ: missing %s extra %s' % (dict(ta - tb), dict(tb - ta)))
    aa = sorted((k.lower(), v) for k, _, v in ATTR.findall(src) if k.lower() in ('href', 'onclick', 'src', 'name', 'id'))
    ab = sorted((k.lower(), v) for k, _, v in ATTR.findall(tr) if k.lower() in ('href', 'onclick', 'src', 'name', 'id'))
    if aa != ab: issues.append('link/attr values differ: %s vs %s' % (aa, ab))
    ua, ub = sorted(URL.findall(src)), sorted(URL.findall(tr))
    if ua != ub: issues.append('urls differ: %s vs %s' % (ua, ub))
    # keyboard accelerator: a single '&' before a letter (not &amp; entities)
    acc = lambda s: len(re.findall(r'&(?![a-zA-Z]+;|#\d+;)(?=\w)', s))
    if acc(src) != acc(tr): issues.append('accelerator count %d vs %d' % (acc(src), acc(tr)))
    if src.count('\n') and not tr.count('\n'): issues.append('line breaks dropped')
    if src.endswith(' ') != tr.endswith(' ') and src.strip(): issues.append('trailing space differs')
    return issues

def main(path, only_unfinished=False):
    root = ET.parse(path).getroot()
    out = []
    for ctx in root.findall('context'):
        name = ctx.find('name').text
        for m in ctx.findall('message'):
            t = m.find('translation')
            typ = t.get('type')
            if typ in ('vanished', 'obsolete'): continue
            if only_unfinished and typ != 'unfinished': continue
            src = m.find('source').text or ''
            forms = [f.text or '' for f in t.findall('numerusform')] or [t.text or '']
            for f in forms:
                if not f.strip(): continue
                iss = check(src, f)
                if iss: out.append({'context': name, 'source': src, 'translation': f, 'type': typ or 'finished', 'issues': iss})
    return out

if __name__ == '__main__':
    res = main(sys.argv[1], '--unfinished' in sys.argv)
    for r in res: print(json.dumps(r, ensure_ascii=False))
    print('TOTAL', len(res), file=sys.stderr)
