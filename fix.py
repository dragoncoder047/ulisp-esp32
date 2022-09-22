from requests import get

print('downloading...')
t = get('https://raw.githubusercontent.com/dragoncoder047/ulisp-esp32/master/ulisp-esp32.ino').text

'''
#define ULISP_FUNCTION(id,name,doc) const char string_##id[] PROGMEM = id; const char doc_##id[] PROGMEM = doc;
#define TBL_ENTRY(id,fun,mm) { string_##id, fun, mm, doc_##id }
'''

import re
stringerRE = re.compile(r'STRINGER\((\S+?),\s+"(\S*?|)?"?\)')
docserRE = re.compile(r'doc.+("\(?(\S+)[\s\S]+?);', re.MULTILINE)
lineinRE = re.compile(r'\{\s+string_(\S+),\s+(\S+),\s+0x\d+,\s+NULL\s+\}')
header = r'object \*%s \(object \*args, object \*env)\s+\{'

print('finding strings')
strings = {}
for s in stringerRE.finditer(t):
    strings[s.group(1), s.group(0)] = s.group(2)
print(strings)

print('finding docs')
docs = {}
for s in docserRE.finditer(t):
    if len(s.group(1)) < 1000:
        docs[s.group(2).removesuffix('\\n"')] = [s.group(1), s.group(0)]
print(docs)

print('finding tbl')
tbl = {}
for s in lineinRE.finditer(t):
    tbl[s.group(1)] = [s.group(2), s.group(0)]
print(tbl)

print('replacing')
for [[name, match], ls] in strings.items():
    print(name)
    t, n = re.subn(header % name, f'ULISP_FUNCTION({name}, "{ls}", {docs.get(ls, [55])[0]})\n' + re.escape(header.replace('\\','')) % name, t, 1)
    if n:
        t = t.replace(match, '', 1) # clear STRINGER
        t = t.replace(docs.get(ls, [0, '\xFF'])[1], '', 1) # clear DOCS

