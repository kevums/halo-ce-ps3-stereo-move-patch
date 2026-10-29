"""Compile and exercise the real settings loader with isolated config fixtures."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='halo-settings-') as temporary:
    work=Path(temporary)
    binary=work/'settings-test'
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-fsanitize=address,undefined',
        '-I'+str(root/'port/ps3/extensions'),'-I'+str(root/'port/ps3/src'),
        str(root/'tools/ps3_extensions_settings_test.c'),
        str(root/'port/ps3/extensions/extension_settings.c'),'-lm','-o',str(binary)],check=True)
    cases=[('',0),('move_enable 1\nmove_enabled 0\n',1),
           ('move_enabled 0\nmove_enable 1\n',1),('move_enabled 1\n',1),
           ('move_enable 0\nmove_enabled 1\n',0)]
    for index,(config,expected) in enumerate(cases):
        folder=work/str(index);folder.mkdir()
        (folder/'extensions.cfg').write_text(config)
        subprocess.run([str(binary),str(folder),str(expected)],check=True)
