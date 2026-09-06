from pathlib import Path

p = Path('main/device_config.c')
s = p.read_text()
s = s.replace('ESP_LOGW( TAG, "invalid theme \'%s\'; using default", theme->valuestring );', 'ESP_LOGE( TAG, "invalid theme \'%s\'; using default", theme->valuestring );')
s = s.replace('        config_dirty = false;\n        }\n    return result;', '        config_dirty = false;\n    }\n    return result;')
p.write_text(s)

p = Path('docs/CONFIGURATION.md')
s = p.read_text()
s = s.replace('Visual themes are **Indoor**, **Outdoor**, and **Night** and affect colors only. Legacy `discipline` theme text is normalized to **Night + Quiet** if encountered in an otherwise current-schema document.', 'Visual themes are **Indoor**, **Outdoor**, and **Night** and affect colors only. Unsupported theme values, including legacy aliases such as `discipline`, retain the compiled default and are reported as configuration errors.')
s = s.replace('The persisted `recorder.input_gain_db` remains the Recorder capture-gain setting and currently accepts **0 through +24 dB in 3 dB steps**.', 'The persisted `recorder.input_gain_db` remains the Recorder capture-gain setting and currently accepts **0 through +48 dB in 3 dB steps**.')
p.write_text(s)
