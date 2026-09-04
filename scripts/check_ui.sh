#!/usr/bin/env bash
# Transpile every <script type="text/babel"> block in data/index.html exactly the way the browser
# will, so a syntax error surfaces here instead of as a blank page on the device.
#
# There is no build step for the UI -- Babel runs in the browser at load time -- so nothing
# otherwise checks index.html before it is on the hardware. A single bad identifier takes out a
# whole block: this has produced a blank AUDIO tab once (2026-08-28, a null read outside its
# guard) and was caught here a second time (2026-09-01, a duplicate `micOn` in the StatusBar
# block). Each <script> block is its own scope, and this reports them separately for that reason.
#
# Uses the Babel already vendored in data/vendor, so it needs nothing but node.
set -euo pipefail
cd "$(dirname "$0")/.."
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
gunzip -c data/vendor/babel.min.js.gz > "$TMP/babel.js"
cat > "$TMP/check.js" <<'JS'
const fs = require('fs');
const Babel = require(process.argv[3] + '/babel.js');
const html = fs.readFileSync(process.argv[2], 'utf8');
const re = /<script type="text\/babel"[^>]*>([\s\S]*?)<\/script>/g;
let m, i = 0, bad = 0;
while ((m = re.exec(html)) !== null) {
  i++;
  const line = html.slice(0, m.index).split('\n').length;
  try {
    Babel.transform(m[1], { presets: ['react'] });
    console.log(`  block ${i} (line ${line}): ok, ${m[1].split('\n').length} lines`);
  } catch (e) {
    bad++;
    console.log(`  block ${i} (line ${line}): FAILED ${e.message.split('\n')[0]}`);
    continue;
  }
  // Scope check, not a syntax check. Each block is its own scope and destructures the React
  // hooks it needs from `React` at the top; a hook used but not destructured IN THAT BLOCK
  // transpiles perfectly and then throws a ReferenceError at render, blanking the tab. That has
  // happened twice -- most recently on 2026-09-04, when useRef/useCallback were added to the
  // PATCH tab whose block only had useState and useEffect.
  const decl = m[1].match(/const \{([^}]*)\}\s*=\s*React\s*;/);
  const have = new Set((decl ? decl[1] : '').split(',').map((x) => x.trim()).filter(Boolean));
  const HOOKS = ['useState','useEffect','useRef','useCallback','useMemo','useReducer','useLayoutEffect','useContext'];
  const missing = HOOKS.filter((h) => new RegExp('(^|[^.\\w])' + h + '\\s*\\(').test(m[1]) && !have.has(h));
  if (missing.length) {
    bad++;
    console.log(`  block ${i} (line ${line}): USES ${missing.join(', ')} but does not destructure ${missing.length > 1 ? 'them' : 'it'} from React in this block`);
  }
}
console.log(bad ? `\n${bad} block(s) failed` : `\nall ${i} blocks transpile cleanly`);
process.exit(bad ? 1 : 0);
JS
node "$TMP/check.js" ui/index.html "$TMP"
