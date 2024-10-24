import pytest
import subprocess

from util import KillOnExit


@pytest.fixture
def executable(request):
    return request.config.getoption('executable')


def test_ausfuehrbare_datei_existiert(executable):
    """check that 'build/hello_world' exists by seeing if it can be executed """
    try:
        with KillOnExit([executable]):
            pass
    except FileNotFoundError:
        error_msg = ("Die Binary-Datei 'build/hello_world' existiert nicht. Haben Sie sie korrekt erstellt? (Konsole: 'cmake -B build -D CMAKE_BUILD_TYPE=Debug && make -C build')")
        raise FileNotFoundError(error_msg)


def test_output(executable):
    """Run the program and check the output"""
    try:
        proc = KillOnExit([executable], stdout=subprocess.PIPE, text=True, shell=True)
        out, err = proc.communicate()
        print(out, err)
    except FileNotFoundError:
        raise FileNotFoundError("Test nicht möglich, die Binary-Datei 'build/hello_world' existiert nicht. Haben Sie in CMakeLists 'add_executable' richtig konfiguriert und sie korrekt erstellt? (Konsole: 'cmake -B build -D CMAKE_BUILD_TYPE=Debug && make -C build')")

    # Check the stdout output (from your printf)
    error_msg = f"Das Programm hat '{out}' ausgegeben, aber 'Hello World!' war erwartet"
    assert out == "Hello World!", error_msg
