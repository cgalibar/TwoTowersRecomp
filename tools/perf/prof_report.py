# Lee los informes [profile] de TTR_PROFILE (formato con pila: self / incl / from) de un run.log.
# uso: python tools/perf/prof_report.py <run.log> [informe, -1 = ultimo] [regex que elige los hilos] [max lineas]
# hilos: EE "dispatchGuestBranch", VIF1 "Vif1Worker::run", GS "GSAsyncBackend::run", raster "GSBandPipeline::workerLoop"
import re, sys
path = sys.argv[1]
want = int(sys.argv[2]) if len(sys.argv) > 2 else -1
pat = sys.argv[3] if len(sys.argv) > 3 else None   # only threads whose block matches this regex
maxlines = int(sys.argv[4]) if len(sys.argv) > 4 else 400
reports = []
for raw in open(path, 'r', encoding='latin-1'):
    idx = raw.find('[profile] ')
    if idx < 0: continue
    line = raw[idx + 10:].rstrip('\n')
    if re.match(r'\d+ samples$', line):
        reports.append([]); continue
    if not reports: continue
    if line.startswith('thread '):
        reports[-1].append([line])
    elif reports[-1]:
        reports[-1][-1].append(line)
print('reports:', len(reports))
for block in reports[want]:
    text = '\n'.join(block)
    if pat and not re.search(pat, text): continue
    for l in block[:maxlines]:
        l = re.sub(r"`lambda at .*?(\w+\.cpp):(\d+):\d+'", r'<\1:\2>', l)
        l = l.replace("`anonymous namespace'::", '')
        print(l[:170])
