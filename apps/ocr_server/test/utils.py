import logging
import os
import subprocess
import sys
from pathlib import Path


TEST_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_TEST_RESOURCE_ROOT = TEST_DIR
DEFAULT_RESOURCE_DOWNLOAD_ROOT = PROJECT_ROOT / "build" / "baas-resource-downloads"

REQUIRED_TEST_IMAGES = [
    Path("ocr/en-us/0.png"),
    Path("ocr/ja-jp/12.png"),
    Path("ocr_for_single_line/en-us/0.png"),
    Path("ocr_for_single_line/zh-tw/3.png"),
]


class TestLogger:
    def __init__(self):
        self.logger = logging.Logger("test_logger")
        self.logger.setLevel(logging.DEBUG)
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.DEBUG)
        formatter = logging.Formatter('%(asctime)s | %(levelname)s : %(message)s')
        console_handler.setFormatter(formatter)
        self.logger.addHandler(console_handler)
        self.hr_line = "-" * 80

    def sub_title(self, title):
        self._out(f"<<< {title} >>>", 2)

    def hr(self, message):
        self._out(self.hr_line, 2)
        msg_len = len(message)
        left_space_len = (80 - 2 - msg_len) / 2
        right_space_len = 80 - msg_len - left_space_len - 2
        out = "|" + " " * int(left_space_len) + message + " " * int(right_space_len) + "|"
        self._out(out, 2)
        self._out(self.hr_line, 2)

    def debug(self, msg):
        self._out(msg, 1)

    def info(self, msg):
        self._out(msg, 2)

    def warning(self, msg):
        self._out(msg, 3)

    def error(self, msg):
        self._out(msg, 4)

    def critical(self, msg):
        self._out(msg, 5)

    def _out(self, msg, level=1):
        if level == 1:
            self.logger.debug(msg)
        elif level == 2:
            self.logger.info(msg)
        elif level == 3:
            self.logger.warning(msg)
        elif level == 4:
            self.logger.error(msg)
        elif level == 5:
            self.logger.critical(msg)


def count_files(directory):
    return len([f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))])


def _test_images_exist(root):
    return all((root / relative).is_file() for relative in REQUIRED_TEST_IMAGES)


def ensure_ocr_test_images():
    output_root = Path(os.environ.get("BAAS_OCR_TEST_RESOURCE_ROOT", DEFAULT_TEST_RESOURCE_ROOT))
    image_root = output_root / "test_images"
    if _test_images_exist(image_root):
        return image_root

    fetch_script = PROJECT_ROOT / "scripts" / "fetch_resources.py"
    lock_file = PROJECT_ROOT / "resources.lock.json"
    download_root = Path(os.environ.get("BAAS_RESOURCE_DOWNLOAD_ROOT", DEFAULT_RESOURCE_DOWNLOAD_ROOT))

    command = [
        sys.executable,
        str(fetch_script),
        "--lock",
        str(lock_file),
        "--output-root",
        str(output_root),
        "--download-root",
        str(download_root),
        "--resource",
        "ocr_test_images",
    ]
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)

    if not _test_images_exist(image_root):
        raise RuntimeError(f"OCR test images are missing after fetch: {image_root}")
    return image_root


def ocr_test_image_dir(*parts):
    return str(ensure_ocr_test_images().joinpath(*parts))


def ocr_test_image_path(*parts):
    return str(ensure_ocr_test_images().joinpath(*parts))


logger = TestLogger()
