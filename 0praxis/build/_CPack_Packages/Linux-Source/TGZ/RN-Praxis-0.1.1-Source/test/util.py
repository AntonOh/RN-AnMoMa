import subprocess
import time


class KillOnExit(subprocess.Popen):
    """A Popen subclass that kills the subprocess when its context exits"""

    def __init__(self, *args, **kwargs):
        super().__init__(
            *args, **kwargs,
            # stdout=subprocess.PIPE,
        )
        time.sleep(.1)

    def __exit__(self, exc_type, value, traceback):
        self.kill()

        super().__exit__(exc_type, value, traceback)
        time.sleep(.1)
