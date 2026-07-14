from argparse import ArgumentParser
from pathlib import Path
from typing import Any

from ..types import *


def exercise(path: Path, payload: dict[str, int], factory, *args, **kwargs):
    path.resolve().exists()
    path.parent.exists()
    payload.get("key")
    args.count("value")
    kwargs.get("key")

    values = []
    values.append("value")
    " value ".strip()

    parser = ArgumentParser()
    parser.add_argument("--value")

    worker = LocalWorker()
    worker.run()
    worker.client.send("unknown")
    PrivateWorker()

    with open("output.txt", "w") as stream:
        stream.write("value")

    factory().send("payload")
    candidate = payload.get("candidate")
    if isinstance(candidate, dict):
        candidate.get("inside")
    candidate.get("outside")


def conservative(any_value: Any, factory, condition, code):
    any_value.get("unknown")

    candidate = factory()
    while condition:
        candidate = {}
    candidate.get("after-loop")

    match code:
        case 1:
            candidate = {}
    candidate.get("after-match")

    ratio = 1 / 2
    ratio.as_integer_ratio()
