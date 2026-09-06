from pathlib import Path
p = Path('docs/ROADMAP.md')
s = p.read_text()
s = s.replace('**Status:** Implemented by PR #60; release-level combination/migration validation remains open.', '**Status:** Implemented by PR #60; release-level combination/configuration validation remains open.')
p.write_text(s)
