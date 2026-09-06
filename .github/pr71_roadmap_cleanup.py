from pathlib import Path
p = Path('docs/ROADMAP.md')
s = p.read_text()
s = s.replace('- [ ] Verify current-schema legacy-token normalization and complete theme/audio independence.\n', '- [ ] Verify invalid current-schema values retain compiled defaults and theme/audio independence.\n')
p.write_text(s)
