"""Run prior meaningful C tests against the imported production modules.

Installer/Muse transport tests are deliberately excluded: those do not exercise
the XiaoZhi integration. The firmware junction resolves to this board's sources.
"""
import importlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from host_tools import CC

ROOT = Path(__file__).resolve().parent
BOARD = ROOT / 'xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo'
workspace = tempfile.TemporaryDirectory(prefix='nabo-production-tests-')
TESTROOT = Path(workspace.name)
shutil.copytree(BOARD, TESTROOT/'firmware')
shutil.copytree(BOARD/'helix', TESTROOT/'third_party/helix-mp3')
os.environ['CC'] = CC
sys.path.insert(0, str(ROOT/'tests/legacy'))
suite = unittest.TestSuite()
choices = {
    'test_dice': ['DiceRenderActualC', 'DiceUIActualC'],
    'test_keys': ['KeysModel'],
    'test_muyu': ['MuyuCoreC'],
    'test_muyu_ui': ['MuyuUIActualC'],
    'test_radio_stream': ['RadioStreamTests'],
    'test_radio_decoder': ['RadioDecoderTests'],
    'test_radio_menu': ['RadioMenuC'],
}
for name, classes in choices.items():
    module = importlib.import_module(name)
    module.ROOT = TESTROOT
    for cls in classes:
        suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(getattr(module, cls)))
result = unittest.TextTestRunner(verbosity=1).run(suite)
(ROOT/'ported-apps-validation.json').write_text(json.dumps({'tests': result.testsRun, 'errors': len(result.errors), 'failures': len(result.failures), 'passed': result.wasSuccessful(), 'modules': choices, 'production_sources': str(BOARD)}, indent=2), 'utf-8')
workspace.cleanup()
sys.exit(0 if result.wasSuccessful() else 1)
