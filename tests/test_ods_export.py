"""Run with python3 tests/test_ods_export.py; requires a C++17 compiler."""
import pathlib
import shutil
import subprocess
import tempfile
import zipfile
import xml.etree.ElementTree as ET

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    work = pathlib.Path(directory)
    source = work / 'export.cpp'
    source.write_text(r'''
#include "OdsExporter.h"
#include <sqlite3.h>
int main(int argc, char** argv) {
    OdsExporter::Write(argv[1], {"Название", "Сумма", "Код", "Длинный номер"},
        {{" А & <Б>\n\tтекст", "123.45", "00123", "1234567890123456789"},
         {"=SUM(A1:A2)", "", "x"}},
        {SQLITE_TEXT, SQLITE_FLOAT, SQLITE_TEXT, SQLITE_INTEGER});
    OdsExporter::Write(argv[2], {"Пустой результат"}, {});
    try {
        OdsExporter::Write(argv[3], {"Ошибка"}, {});
        return 1;
    } catch (...) {}
}
''', encoding='utf-8')
    subprocess.run(['c++', '-std=c++17', '-I' + str(root / 'src'),
                    str(source), str(root / 'src/OdsExporter.cpp'),
                    '-o', str(work / 'export')], check=True)
    subprocess.run([str(work / 'export'), str(work / 'result.ods'),
                    str(work / 'empty.ods'), str(work / 'missing' / 'fail.ods')], check=True)
    ns = {'t': 'urn:oasis:names:tc:opendocument:xmlns:table:1.0',
          'o': 'urn:oasis:names:tc:opendocument:xmlns:office:1.0',
          'text': 'urn:oasis:names:tc:opendocument:xmlns:text:1.0'}
    with zipfile.ZipFile(work / 'result.ods') as ods:
        assert ods.testzip() is None
        first = ods.infolist()[0]
        assert first.filename == 'mimetype' and first.compress_type == 0 and not first.extra
        assert ods.read('mimetype') == b'application/vnd.oasis.opendocument.spreadsheet'
        ET.fromstring(ods.read('META-INF/manifest.xml'))
        tree = ET.fromstring(ods.read('content.xml'))
        rows = tree.findall('.//t:table-row', ns)
        assert len(rows) == 3
        cells = rows[1].findall('t:table-cell', ns)
        assert cells[1].get('{%s}value' % ns['o']) == '123.45'
        for i, value in [(2, '00123'), (3, '1234567890123456789')]:
            assert cells[i].get('{%s}value-type' % ns['o']) == 'string'
            assert ''.join(cells[i].itertext()) == value
        assert cells[0].find('.//text:line-break', ns) is not None
        assert cells[0].find('.//text:tab', ns) is not None
        assert 'А' in ''.join(cells[0].itertext()) and '&<Б>' in ''.join(cells[0].itertext())
        assert len(rows[2].findall('t:table-cell', ns)) == 4
        assert rows[2][0].get('{%s}formula' % ns['t']) is None
    with zipfile.ZipFile(work / 'empty.ods') as ods:
        assert len(ET.fromstring(ods.read('content.xml')).findall('.//t:table-row', ns)) == 1
    if shutil.which('libreoffice'):
        subprocess.run(['libreoffice', '-env:UserInstallation=' + (work / 'profile').as_uri(),
                        '--headless', '--convert-to', 'xlsx', '--outdir', str(work),
                        str(work / 'result.ods')], check=True, timeout=60)
        assert (work / 'result.xlsx').is_file(), 'LibreOffice could not read the ODS file'
print('ODS export checks passed')
