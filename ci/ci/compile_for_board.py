import os
import subprocess
from pathlib import Path
from threading import Lock

from ci.boards import Board
from ci.locked_print import locked_print

ERROR_HAPPENED = False


IS_GITHUB = "GITHUB_ACTIONS" in os.environ
FIRST_BUILD_LOCK = Lock()
USE_FIRST_BUILD_LOCK = IS_GITHUB


def errors_happened() -> bool:
    """Return whether any errors happened during the build."""
    return ERROR_HAPPENED


def compile_for_board_and_example(
    project: Board, example: str, build_dir: str | None
) -> tuple[bool, str]:
    """Compile the given example for the given board."""
    board = project.board_name
    builddir = Path(build_dir) / board if build_dir else Path(".build") / board
    builddir.mkdir(parents=True, exist_ok=True)
    srcdir = builddir / "src"
    # Remove the previous *.ino file if it exists, everything else is recycled
    # to speed up the next build.
    if srcdir.exists():
        subprocess.run(["rm", "-rf", srcdir.as_posix()], check=True)
    locked_print(f"*** Building example {example} for board {board} ***")
    cmd_list = [
        "pio",
        "ci",
        "--board",
        board,
        "--lib=ci",
        "--lib=src",
        "--keep-build-dir",
        f"--build-dir={builddir.as_posix()}",
    ]
    cmd_list.append(f"examples/{example}/*ino")
    cmd_str = subprocess.list2cmdline(cmd_list)
    msg_lsit = [
        "\n\n******************************",
        "* Running command:",
        f"*     {cmd_str}",
        "******************************\n",
    ]
    msg = "\n".join(msg_lsit)
    locked_print(msg)
    result = subprocess.run(
        cmd_list,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )

    stdout = result.stdout
    # replace all instances of "lib/src" => "src" so intellisense can find the files
    # with one click.
    stdout = stdout.replace("lib/src", "src").replace("lib\\src", "src")
    locked_print(stdout)
    if result.returncode != 0:
        locked_print(f"*** Error compiling example {example} for board {board} ***")
        return False, stdout
    locked_print(f"*** Finished building example {example} for board {board} ***")
    return True, stdout


# Function to process task queues for each board
def compile_examples(
    project: Board, examples: list[str], build_dir: str | None
) -> tuple[bool, str]:
    """Process the task queue for the given board."""
    global ERROR_HAPPENED  # pylint: disable=global-statement
    board = project.board_name
    is_first = True
    for example in examples:
        if ERROR_HAPPENED:
            return True, ""
        locked_print(f"\n*** Building {example} for board {board} ***")
        if is_first:
            locked_print(f"*** Building for first example {example} board {board} ***")
        if is_first and USE_FIRST_BUILD_LOCK:
            with FIRST_BUILD_LOCK:
                # Github runners are memory limited and the first job is the most
                # memory intensive since all the artifacts are being generated in parallel.
                success, message = compile_for_board_and_example(
                    project=project, example=example, build_dir=build_dir
                )
        else:
            success, message = compile_for_board_and_example(
                project=project, example=example, build_dir=build_dir
            )
        is_first = False
        if not success:
            ERROR_HAPPENED = True
            return (
                False,
                f"Error building {example} for board {board}. stdout:\n{message}",
            )
    return True, ""
