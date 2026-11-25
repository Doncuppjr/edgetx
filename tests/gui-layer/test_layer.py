# Compile the production stack source/header against a minimal LVGL boundary.
# Only window.h is substituted; no production method bodies are changed.
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / 'radio/src/gui/colorlcd/libui'
fixtures = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='edgetx-layer-') as name:
    build = Path(name)
    header = (source / 'layer.h').read_text()
    assert header.count('#include "window.h"') == 1
    (build / 'layer.h').write_text(header.replace('#include "window.h"', '#include "lvgl_stubs.h"'))
    (build / 'layer.cpp').write_text((source / 'layer.cpp').read_text())
    for fixture in ('lvgl_stubs.h', 'test_layer.cpp'):
        (build / fixture).write_bytes((fixtures / fixture).read_bytes())
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(build/'layer.cpp'), str(build/'test_layer.cpp'),
                    '-o', str(build/'test-layer')], check=True)
    subprocess.run([str(build/'test-layer')], check=True)
print('Production popup stack lifetime tests passed.')
