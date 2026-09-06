from pathlib import Path

p = Path('main/device_config.c')
s = p.read_text()
s = s.replace('static bool config_upgrade_pending;\n', '')
s = s.replace('    config_upgrade_pending = false;\n', '')
s = s.replace('        if( config_store_import_pending() || config_upgrade_pending )\n', '        if( config_store_import_pending() )\n')
s = s.replace('                config_upgrade_pending = false;\n', '')
p.write_text(s)
