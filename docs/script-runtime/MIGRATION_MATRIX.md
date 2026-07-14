# Python-to-C++ Script Runtime Migration Matrix

This matrix is the authoritative coverage ledger. Each Python operation family
must ultimately resolve to concrete source locations, a versioned script API,
a C++ host binding, and executable parity evidence.

| Domain | Python source/operation | Current C++ capability | Planned script API | Parity evidence | Status |
| --- | --- | --- | --- | --- | --- |
| Language semantics | 109 module files: 749 `if`, 177 `for`, 71 `while`, functions/callbacks, ordered maps, slices, comprehensions | No general runtime | Core language draft | Conformance corpus pending | Inventoried |
| Logging | 333 module `info`, 97 `warning`, 20 `error`; service streams scoped structured logs | Existing spdlog logger | `baas/log` host module | Golden event trace pending | Inventoried |
| Configuration | 97-field user config, 25-field static config, event and setup schemas; unknown fields must survive | JSON pointer based config classes | `baas/config` snapshot/transaction API | Round-trip and conflict parity pending | Inventoried |
| Resources | 2,961 PNG, 130 JSON, 253 generated coordinate modules; locale/activity dynamic imports | CMake resource fetch and image cache | `baas/resource` immutable snapshots | Validator/manifest/update tests pending | Inventoried |
| Screenshot | Cached screenshot, 0.3 s throttle, 1280x720 ratio; ADB/Nemu/u2/scrcpy/desktop backends | ADB/scrcpy and Windows Nemu/ldopengl | `baas/device.capture` | Image/hash/timing parity pending | Inventoried |
| Image processing | `co_detect` 199 calls; RGB range 90; OpenCV/template/search helpers | OpenCV utilities and feature classes | `baas/vision` plus ordered `co_detect` intrinsic | Numeric/image/state trace parity pending | Inventoried |
| OCR | Region OCR 17, integer OCR 8, language/candidate/filter/pass-method semantics | ONNX Runtime OCR core and tested OCR server | `baas/ocr` lifecycle/inference API | OCR corpus parity pending | Inventoried |
| Device input/control | Click 95, swipe 23, async click/wait-over distinction, jitter/ratio/clamp; app lifecycle | ADB/scrcpy/Nemu controls | `baas/device` capability API | Emulator input trace pending | Inventoried |
| Feature/procedure | Ordered `co_detect`, popup/tentative-click/loading/timeout behavior | Feature and one procedure type; AutoFight-specific workflow | Versioned host intrinsics plus script composition | Workflow trace pending | Inventoried |
| Scheduler/workflow | 36 registered tasks, 26 default events; priority, reset, interval, pre/post tasks, retries | Partial app-specific workflows | `baas/task` structured cancellation and scheduler API | Retry/cancel/state parity pending | Inventoried |
| Service/Tauri | 9 HTTP, 5 WS, provider/sync/trigger/remote channels, 4 pipe channels and secure handshake | OCR-only HTTP service | Versioned compatibility service | Shared wire/golden protocol suite pending | Inventoried |
| Updates/rollback | Git/updater plus setup.toml and mutable resources | Configure-time resource fetch only | Signed manifests, atomic switch, rollback | Atomic update/rollback pending | Inventoried |

## Entry completion rule

An entry is complete only when all relevant Python call sites are enumerated,
the host API and script signature are documented, platform/threading/error
semantics are explicit, and automated parity evidence passes. A similar C++
class name or a successful narrow smoke test is not sufficient evidence.

<!-- BEGIN GENERATED OPERATION INDEX -->
## Generated operation-level index

Deterministic JSON evidence: [evidence/operation-index.json](evidence/operation-index.json).

Snapshot `75bbacb545bc87e9510d85cbe8034f9180397004` contains 569 Python files, 4556 unique operations, 15469 observed sites, 4022 unclassified operations, and 0 parse errors.

| Evidence ID | Kind/form | Operation symbol | Uses | Family | Owner | Proposed C++ binding | Parity test | Status | Representative source |
| --- | --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| op-199fdb385bfcb316 | call/alias | `PyQt5.QtCore.QLocale` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:77` |
| op-4d000870e6a29e61 | call/alias | `PyQt5.QtCore.QMutex` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:210` |
| op-94971f461a5eecb8 | call/alias | `PyQt5.QtCore.QMutexLocker` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:107` |
| op-936117afd7aa7e13 | call/alias | `PyQt5.QtCore.QObject` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:12` |
| op-e4783a0e165b1d49 | call/alias | `PyQt5.QtCore.QPoint` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:313` |
| op-d90feef85240f373 | call/alias | `PyQt5.QtCore.QPropertyAnimation` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:90` |
| op-40a25bfb3cd747cc | call/alias | `PyQt5.QtCore.QRectF` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:193` |
| op-fd64fd28c3bfa188 | call/alias | `PyQt5.QtCore.QSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:397` |
| op-3926e3a203395020 | call/alias | `PyQt5.QtCore.QUrl` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:31` |
| op-2d5f41a00e3900a3 | call/alias | `PyQt5.QtCore.pyqtProperty` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:179` |
| op-c5194006ab5f97be | call/alias | `PyQt5.QtCore.pyqtSignal` | 18 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:26` |
| op-f5c4d64db31f76ff | call/alias | `PyQt5.QtCore.pyqtSlot` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:164` |
| op-850c01a5ef9fa00d | call/alias | `PyQt5.QtGui.QColor` | 21 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:495` |
| op-0540004ca6d57df2 | call/alias | `PyQt5.QtGui.QDoubleValidator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:31` |
| op-5f29606a4b972618 | call/alias | `PyQt5.QtGui.QFontMetrics` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:28` |
| op-faa04947cc3fbff4 | call/alias | `PyQt5.QtGui.QIcon` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:508` |
| op-45dc7b3f08889a29 | call/alias | `PyQt5.QtGui.QIntValidator` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:37` |
| op-f6194942cde5b760 | call/alias | `PyQt5.QtGui.QPainter` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:188` |
| op-959b8bdf8e8d75b3 | call/alias | `PyQt5.QtGui.QPainterPath` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:190` |
| op-48d3e131803f5ec4 | call/alias | `PyQt5.QtGui.QPixmap` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:96` |
| op-896517788b2a22aa | call/alias | `PyQt5.QtSvg.QSvgRenderer` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:812` |
| op-83fdb033909f9e7d | call/alias | `PyQt5.QtWidgets.QApplication` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:57` |
| op-19fe788bbae478c2 | call/alias | `PyQt5.QtWidgets.QFileDialog` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:41` |
| op-ece7607f8370296d | call/alias | `PyQt5.QtWidgets.QFrame` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:49` |
| op-bdd2529c0a08dca3 | call/alias | `PyQt5.QtWidgets.QGraphicsDropShadowEffect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:134` |
| op-3e7be5024745d4a1 | call/alias | `PyQt5.QtWidgets.QGridLayout` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:182` |
| op-abb0f7218bb028f8 | call/alias | `PyQt5.QtWidgets.QGroupBox` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:181` |
| op-6fc996a1f450bfa7 | call/alias | `PyQt5.QtWidgets.QHBoxLayout` | 71 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:16` |
| op-bf3b6651b76f4610 | call/alias | `PyQt5.QtWidgets.QLabel` | 66 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:20` |
| op-b7abca6d686fe431 | call/alias | `PyQt5.QtWidgets.QTableWidgetItem` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:474` |
| op-1605c7c07d417697 | call/alias | `PyQt5.QtWidgets.QVBoxLayout` | 32 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:15` |
| op-3f69394ef967fc87 | call/alias | `PyQt5.QtWidgets.QWidget` | 14 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:40` |
| op-ecab866aeca049a8 | call/alias | `adbutils.adb_path` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:185` |
| op-4721b35a1bfb0928 | call/alias | `alive_progress.alive_bar` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:188` |
| op-76cabedce5762ba6 | call/alias | `bs4.BeautifulSoup` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:143` |
| op-f998a71a4e1913b9 | call/alias | `collections.defaultdict` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/utils/logging.py:28` |
| op-686331c2312ed463 | call/alias | `collections.deque` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/system_logging.py:121` |
| op-05ade0586549db71 | call/alias | `contextlib.suppress` | 19 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:74` |
| op-e811a8972f6bd65a | call/alias | `copy.deepcopy` | 5 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `deploy/installer/_installer.py:355` |
| op-7fbe6a04bb219b28 | call/alias | `core.Baas_thread.Baas_thread` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:53` |
| op-7c5a65203447a49b | call/alias | `core.color.check_sweep_availability` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/explore_tasks/sweep_task.py:58` |
| op-140c1610598cab56 | call/alias | `core.color.match_rgb_feature` | 3 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/picture.py:116` |
| op-b8ce1fbfdb893742 | call/alias | `core.color.rgb_in_range` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/dailyGameActivities/HinaSummerVacationAudioGame.py:23` |
| op-4ed2ff43e7880e9e | call/alias | `core.config.config_set.ConfigSet` | 12 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `cli.example.py:52` |
| op-f921a1fd4be8e0ff | call/alias | `core.config.generated_static_config.StaticConfig` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `core/config/config_set.py:40` |
| op-1c728a4c2fc20c57 | call/alias | `core.config.generated_user_config.Config` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `core/config/config_set.py:44` |
| op-c148785497b8dd90 | call/alias | `core.device.Control.Control` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:332` |
| op-8598c8be1143118a | call/alias | `core.device.Screenshot.Screenshot` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:331` |
| op-735ff19dd7d088af | call/alias | `core.device.connection.Connection` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:321` |
| op-f299a1c38b561c27 | call/alias | `core.device.control.adb.AdbControl` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Control.py:29` |
| op-849d794fd30c4349 | call/alias | `core.device.control.nemu.NemuControl` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Control.py:27` |
| op-38cf095a6de4c4c0 | call/alias | `core.device.control.pyautogui.PyautoguiControl` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Control.py:38` |
| op-e12a38cd9777dec4 | call/alias | `core.device.control.scrcpy.ScrcpyControl` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Control.py:33` |
| op-1efc952e6cb3597a | call/alias | `core.device.control.uiautomator2.U2Control` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Control.py:31` |
| op-50222954f41341a4 | call/alias | `core.device.emulator_manager.auto_scan_simulator.auto_scan_simulators` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/ldplayer_manager_api.py:16` |
| op-255b58763bdb2bc0 | call/alias | `core.device.emulator_manager.mumu12_api_backend` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/nemu_client.py:409` |
| op-e82acb86107ddb0f | call/alias | `core.device.scrcpy.control.ControlSender` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/scrcpy_client.py:27` |
| op-bb250d3b960c19c9 | call/alias | `core.device.scrcpy.core.Client` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/scrcpy_client.py:22` |
| op-3a6f42edd2b568f9 | call/alias | `core.device.screenshot.adb.AdbScreenshot` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Screenshot.py:33` |
| op-c70090bae95d4759 | call/alias | `core.device.screenshot.mss.MssScreenshot` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Screenshot.py:45` |
| op-95efef7759b4a191 | call/alias | `core.device.screenshot.nemu.NemuScreenshot` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Screenshot.py:31` |
| op-b85fceea52126955 | call/alias | `core.device.screenshot.pyautogui.PyautoguiScreenshot` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Screenshot.py:43` |
| op-66c3cd35b621f657 | call/alias | `core.device.screenshot.scrcpy.ScrcpyScreenshot` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Screenshot.py:37` |
| op-2d782d8691af2491 | call/alias | `core.device.screenshot.uiautomator2.U2Screenshot` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/Screenshot.py:35` |
| op-b95caf6cbf527390 | call/alias | `core.device.uiautomator2_client.BAAS_U2_Initer` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:429` |
| op-f162003984961dff | call/alias | `core.device.window_capture.windows.window_info.win32_WindowInfo` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/connection.py:65` |
| op-f4f87c8cc9ea5339 | call/alias | `core.exception.FunctionCallTimeout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:83` |
| op-873b4bc3e98eb4b8 | call/alias | `core.exception.LogTraceback` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:603` |
| op-32355f3643450e27 | call/alias | `core.exception.OcrInternalError` | 24 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:250` |
| op-7db18cb875dcfc18 | call/alias | `core.exception.PackageIncorrect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:93` |
| op-6ff3039079d0851e | call/alias | `core.exception.RequestHumanTakeOver` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:61` |
| op-8508ba2abdebef34 | call/alias | `core.exception.SharedMemoryError` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ipc_manager.py:24` |
| op-4b864adff0f29c11 | call/alias | `core.geometry.parallelogram.Parallelogram` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:409` |
| op-387b1322056c3619 | call/alias | `core.image.compare_image` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/activities/activity_utils.py:400` |
| op-e83f9c589b68342b | call/alias | `core.image.swipe_search_target_str` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/explore_tasks/task_utils.py:495` |
| op-8544a953b91bdb35 | call/alias | `core.notification.notify` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:481` |
| op-2ab0daa6ebdf0c5a | call/alias | `core.notification.toast` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:988` |
| op-6033d41f7129d80c | call/alias | `core.ocr.baas_ocr_client.server_installer.check_git` | 2 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `main.py:29` |
| op-1e0898d7c5384c9f | call/alias | `core.picture.co_detect` | 4 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/collect_pass_reward.py:35` |
| op-38cea291d7a864cc | call/alias | `core.pushkit.push` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:495` |
| op-db518ca254542378 | call/alias | `core.scheduler.Scheduler` | 2 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/Baas_thread.py:874` |
| op-1868255f58e31366 | call/alias | `core.utils.Logger` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1132` |
| op-d2f35d7484310f3c | call/alias | `core.utils.build_possible_string_dict_and_length` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:123` |
| op-5f559d65b3d56825 | call/alias | `core.utils.delay` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:366` |
| op-e029378230673f4d | call/alias | `core.utils.get_nearest_hour` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:28` |
| op-202259c4898256c5 | call/alias | `core.utils.merge_nearby_coordinates` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/image.py:216` |
| op-fd2c17adbe23471f | call/alias | `core.utils.most_similar_string` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:153` |
| op-8a45a1abc17c4100 | call/alias | `core.utils.purchase_ticket_times_to_int` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:26` |
| op-9950119328a96be0 | call/alias | `cryptography.hazmat.primitives.ciphers.aead.ChaCha20Poly1305` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:21` |
| op-c9740a2b35ccda45 | call/alias | `cryptography.hazmat.primitives.kdf.hkdf.HKDF` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:34` |
| op-37fccc22b225f731 | call/alias | `ctypes.wintypes.RECT` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:153` |
| op-5d9054948a548bcb | call/alias | `dataclasses.asdict` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:76` |
| op-4d35ad321ef7139b | call/alias | `dataclasses.field` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/models.py:43` |
| op-cbf7ea2e13aab343 | call/alias | `dataclasses.fields` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:715` |
| op-17faa954bd31fb5d | call/alias | `datetime.datetime` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/restart.py:37` |
| op-52c156586e54fd33 | call/alias | `datetime.timedelta` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:51` |
| op-db3166a38c23ece5 | call/alias | `deploy.installer.const.get_remote_sha_methods_for_channel` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:330` |
| op-d712f3197360ba7f | call/alias | `deploy.installer.const.method_for_repo_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:113` |
| op-9a8a3e07158d4842 | call/alias | `deploy.installer.const.normalize_update_channel` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:223` |
| op-2473b319d9d41a00 | call/alias | `deploy.installer.const.repo_url_for_method` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:584` |
| op-172cdc290ad3b1e2 | call/alias | `deploy.installer.mirrorc_update.mirrorc_updater.MirrorC_Updater` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:65` |
| op-9bc7359bbdd38e44 | call/alias | `deploy.installer.toml_config.TOML_Config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:77` |
| op-a35504de1c3aac7e | call/alias | `easydict.EasyDict` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:279` |
| op-d62b7356c7dfe594 | call/alias | `enum.auto` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/const.py:7` |
| op-a105ac6b026ea438 | call/alias | `fastapi.APIRouter` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:20` |
| op-385466b1f95e5149 | call/alias | `fastapi.FastAPI` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:37` |
| op-603d872deafc679f | call/alias | `fastapi.HTTPException` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:32` |
| op-ec0c6bc4f4981599 | call/alias | `fastapi.Response` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:144` |
| op-fbdd0fedf22cc3cd | call/alias | `fastapi.testclient.TestClient` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_http_contract.py:49` |
| op-551c1631ea488fb9 | call/alias | `functools.partial` | 25 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:264` |
| op-369abd8f4bc1f40f | call/alias | `functools.wraps` | 19 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:96` |
| op-4445bb13c70a3c13 | call/alias | `gui.components.dialog_panel.CreateErrorInfoMessageBox` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:87` |
| op-ef2363bf5617f920 | call/alias | `gui.components.dialog_panel.CreateSettingMessageBox` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:562` |
| op-803b7bf4a595343a | call/alias | `gui.components.expand.expandTemplate.TemplateLayoutV2` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:62` |
| op-163ac0a391f0a04c | call/alias | `gui.components.template_card.SimpleSettingCard` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:85` |
| op-139a9c2adbf18eed | call/alias | `gui.components.template_card.TemplateSettingCard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:156` |
| op-c6c7a5f021f244dc | call/alias | `gui.fragments.glob.GlobalFragment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:440` |
| op-1775a1122c21e736 | call/alias | `gui.fragments.history.HistoryWindow` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:526` |
| op-06aa6e1eb85262d5 | call/alias | `gui.fragments.home.HomeFragment` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:431` |
| op-6ed6c39ae5c34f77 | call/alias | `gui.fragments.process.ProcessFragment` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:432` |
| op-4bac50dbb62c6f77 | call/alias | `gui.fragments.readme.ReadMeWindow` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:516` |
| op-444b4c6a12d7e2c2 | call/alias | `gui.fragments.settings.SettingsFragment` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:434` |
| op-96750d24a8c1407d | call/alias | `gui.fragments.switch.SwitchFragment` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:433` |
| op-9f6007115df6b943 | call/alias | `gui.util.config_gui.isWin11` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:118` |
| op-a0a568710cf31a2c | call/alias | `gui.util.config_translation.ConfigTranslation` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:13` |
| op-7c6aa5342119536a | call/alias | `gui.util.customized_ui.AssetsWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:101` |
| op-8ea3d75530aee321 | call/alias | `gui.util.customized_ui.BoundComponent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:117` |
| op-eff2a6d9b801140d | call/alias | `gui.util.customized_ui.ClickFocusLineEdit` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:137` |
| op-fc9062147c0b96a9 | call/alias | `gui.util.customized_ui.ColorSvgWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:112` |
| op-f23c650cbd385c9d | call/alias | `gui.util.customized_ui.DialogSettingBox` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:216` |
| op-e70cf8fed3cd2381 | call/alias | `gui.util.customized_ui.FuncLabel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:63` |
| op-94d94d5ab3edc30a | call/alias | `gui.util.customized_ui.OutlineLabel` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:62` |
| op-568335e3dcbd3b80 | call/alias | `gui.util.customized_ui.TableManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:95` |
| op-d310de850a0b2522 | call/alias | `gui.util.hotkey_manager.GlobalHotkeyManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:125` |
| op-98947c0118a52aac | call/alias | `gui.util.language.Language` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:77` |
| op-f5657a12b7cf295d | call/alias | `gui.util.notification.error` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:95` |
| op-d35c65594fa9e2ba | call/alias | `gui.util.notification.success` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:164` |
| op-59fd385ae6fbac11 | call/alias | `halo.Halo` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:312` |
| op-c58fbee9a575f4ed | call/alias | `hashlib.md5` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:26` |
| op-243a3c5f72558bec | call/alias | `html.escape` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:213` |
| op-d7b55ac87dd8536d | call/alias | `inspect.signature` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:19` |
| op-b1bdb2e039e17874 | call/alias | `io.StringIO` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:60` |
| op-f47593901523221f | call/alias | `java.jclass` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:332` |
| op-fb9eb091a9cb9322 | call/alias | `main.Main` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:51` |
| op-2f1ee864bb391a49 | call/alias | `math.ceil` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `gui/components/expand/eventMapConfig.py:68` |
| op-846e3c26bb816d7a | call/alias | `mirrorc_update.mirrorc_updater.MirrorC_Updater` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:84` |
| op-df4100f99299c878 | call/alias | `mirrorc_update.utils.detect_system_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:58` |
| op-1d5de01940d4b077 | call/alias | `mirrorc_update.utils.remove_first_dir` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:130` |
| op-5f0ca2f443f3f063 | call/alias | `module.activities.PresidentHinasSummerVacation.to_activity` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/HinaSummerVacationAudioGame.py:13` |
| op-cd380c016f4517b0 | call/alias | `module.activities.activity_utils.activity_sweep` | 62 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/ACertainScientificRecordOfYouth.py:6` |
| op-dbdcc2ba4a312f3f | call/alias | `module.activities.activity_utils.explore_activity_challenge` | 62 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/ACertainScientificRecordOfYouth.py:18` |
| op-c3d8d07c73289ce9 | call/alias | `module.activities.activity_utils.explore_activity_mission` | 62 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/ACertainScientificRecordOfYouth.py:14` |
| op-5ee4f9202497f10c | call/alias | `module.activities.activity_utils.explore_activity_story` | 62 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/ACertainScientificRecordOfYouth.py:10` |
| op-cf425fb1a0c6a775 | call/alias | `module.activities.activity_utils.get_stage_data` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:138` |
| op-44f026cd10924183 | call/alias | `module.activities.activity_utils.preprocess_activity_region` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:11` |
| op-4971a82a85fd8235 | call/alias | `module.activities.activity_utils.preprocess_activity_sweep_times` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:10` |
| op-8bb04a16341cd8c8 | call/alias | `module.cafe_reward.get_invitation_ticket_next_time` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:25` |
| op-a8cac6beddab3591 | call/alias | `module.cafe_reward.get_invitation_ticket_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:22` |
| op-c638deadf7211bfa | call/alias | `module.cafe_reward.interaction_for_cafe_solve_method3` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:13` |
| op-5b1958168af3a40c | call/alias | `module.cafe_reward.invite_girl` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:12` |
| op-e641a4a45aabd1d9 | call/alias | `module.cafe_reward.to_cafe` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:10` |
| op-18f1ad703beeef5c | call/alias | `module.cafe_reward.to_no2_cafe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no2_cafe_invite.py:8` |
| op-9c461fd0b40f627a | call/alias | `module.clear_special_task_power.get_task_count` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:8` |
| op-f0f31248df730d6b | call/alias | `module.explore_tasks.explore_task.validate_and_add_task` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:36` |
| op-532414f240901a23 | call/alias | `module.explore_tasks.sweep_task.read_task` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:932` |
| op-638f927f780243da | call/alias | `module.explore_tasks.task_utils.convert_team_config` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_normal_task.py:47` |
| op-f00ca4457be15933 | call/alias | `module.explore_tasks.task_utils.employ_units` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_normal_task.py:95` |
| op-9b7caafbe9d132c3 | call/alias | `module.explore_tasks.task_utils.execute_grid_task` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:63` |
| op-3f61551893e7aafa | call/alias | `module.explore_tasks.task_utils.get_challenge_state` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:110` |
| op-bc3c099320866d31 | call/alias | `module.explore_tasks.task_utils.get_stage_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:56` |
| op-5103815949e397fe | call/alias | `module.explore_tasks.task_utils.to_hard_event` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:241` |
| op-49845081e898f30f | call/alias | `module.explore_tasks.task_utils.to_mission_info` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:59` |
| op-a29b17a9ae10f263 | call/alias | `module.explore_tasks.task_utils.to_normal_event` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:168` |
| op-b138bca9faeccd1b | call/alias | `module.explore_tasks.task_utils.to_region` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:58` |
| op-fe8018f4d3a1efe5 | call/alias | `module.main_story.auto_fight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:165` |
| op-6637cd5db503e116 | call/alias | `module.main_story.set_acc_and_auto` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:181` |
| op-45ea09a04214c612 | call/alias | `module.mini_story.check_6_region_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group_story.py:13` |
| op-13bbd38442968089 | call/alias | `module.no1_cafe_invite.delay_cafe_reward_execution_time` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no2_cafe_invite.py:12` |
| op-7780b2c2843fd2e5 | call/alias | `module.no1_cafe_invite.judge_use_invitation_ticket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no2_cafe_invite.py:9` |
| op-7893a8f9ccf1e2f0 | call/alias | `module.shop.shop_utils.buy` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:32` |
| op-daa0ec0a78cab425 | call/alias | `module.shop.shop_utils.get_purchase_state` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:34` |
| op-9c081012d2c5154e | call/alias | `module.shop.shop_utils.goto_shop_by_name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/tactical_challenge_shop.py:20` |
| op-80fcbcb2e1986bc3 | call/alias | `module.shop.shop_utils.to_common_shop` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:12` |
| op-146bdc362e23921a | call/alias | `pathlib.Path` | 63 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `deploy/installer/_installer.py:234` |
| op-3c37d0226c033a0a | call/alias | `pydantic.Field` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/types.py:51` |
| op-2ec6d1ccef7c46c8 | call/alias | `pydantic.root_validator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/types.py:56` |
| op-182b34afb91098b1 | call/alias | `pydantic.validator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/types.py:31` |
| op-6c962d27acd5d726 | call/alias | `pygit2.Repository` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:930` |
| op-94451b0d137caeec | call/alias | `pygit2.clone_repository` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:777` |
| op-cb975ad58bc08eb6 | call/alias | `qfluentwidgets.Action` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:250` |
| op-949f574263559de1 | call/alias | `qfluentwidgets.BodyLabel` | 16 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:38` |
| op-b157c027cbdaff58 | call/alias | `qfluentwidgets.BoolValidator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:88` |
| op-3912a36f00a30848 | call/alias | `qfluentwidgets.CaptionLabel` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:97` |
| op-485b1fa42929f8d2 | call/alias | `qfluentwidgets.CheckBox` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:24` |
| op-c28a212ea911504b | call/alias | `qfluentwidgets.ColorConfigItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:86` |
| op-7e9823e2df18cbac | call/alias | `qfluentwidgets.ComboBox` | 23 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:27` |
| op-bdcb3ee2f1d2becc | call/alias | `qfluentwidgets.ComboBoxSettingCard` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:149` |
| op-d62ea2a6fe9ffa54 | call/alias | `qfluentwidgets.ConfigItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:88` |
| op-61ea8380889225f9 | call/alias | `qfluentwidgets.CustomColorSettingCard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:130` |
| op-a5e3e5dbbb6612dd | call/alias | `qfluentwidgets.ExpandLayout` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:80` |
| op-f1e4b0b32bf6b3e9 | call/alias | `qfluentwidgets.FlowLayout` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:15` |
| op-42b65e633bfd18e5 | call/alias | `qfluentwidgets.FluentTranslator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:662` |
| op-1a51600e319fc470 | call/alias | `qfluentwidgets.ImageLabel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:366` |
| op-6f618d45e8b1d321 | call/alias | `qfluentwidgets.InfoBar` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/notification.py:38` |
| op-22bd97288d30b0d4 | call/alias | `qfluentwidgets.LineEdit` | 22 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:11` |
| op-7a570ec9e1fc040a | call/alias | `qfluentwidgets.ListWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:72` |
| op-eea38ecaf7739b91 | call/alias | `qfluentwidgets.MessageBox` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:434` |
| op-34979f8bbca30858 | call/alias | `qfluentwidgets.OptionsConfigItem` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:89` |
| op-99652d3b7c41a635 | call/alias | `qfluentwidgets.OptionsSettingCard` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:119` |
| op-3f85aa17ad145ee0 | call/alias | `qfluentwidgets.OptionsValidator` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:90` |
| op-5388f7c3833ebb16 | call/alias | `qfluentwidgets.PrimaryPushButton` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:235` |
| op-be6b2014a3281f45 | call/alias | `qfluentwidgets.PrimaryPushSettingCard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:72` |
| op-238e78002bbd4337 | call/alias | `qfluentwidgets.PushButton` | 25 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:29` |
| op-492d560bab3c8c75 | call/alias | `qfluentwidgets.RoundMenu` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:249` |
| op-9092c9d3e348707a | call/alias | `qfluentwidgets.ScrollArea` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:225` |
| op-73d92014baac1b45 | call/alias | `qfluentwidgets.SettingCardGroup` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:83` |
| op-5c3864233c7d0260 | call/alias | `qfluentwidgets.SpinBox` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:125` |
| op-12b28457d37eac8b | call/alias | `qfluentwidgets.SplashScreen` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:396` |
| op-cac3c5ebde28fdb8 | call/alias | `qfluentwidgets.StrongBodyLabel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:64` |
| op-eb47289b9a75bded | call/alias | `qfluentwidgets.SubtitleLabel` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:10` |
| op-a952eec9141f096f | call/alias | `qfluentwidgets.SwitchButton` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:63` |
| op-c00a235f54d133f8 | call/alias | `qfluentwidgets.SwitchSettingCard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:111` |
| op-df5595e1e2ee3775 | call/alias | `qfluentwidgets.TableWidget` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:279` |
| op-767f67df29434a7a | call/alias | `qfluentwidgets.TextEdit` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:219` |
| op-daa0897debfd3b50 | call/alias | `qfluentwidgets.TitleLabel` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:81` |
| op-5275ecbbe413f693 | call/alias | `qfluentwidgets.ToolTipFilter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:32` |
| op-08ea9f7f2fcb71e2 | call/alias | `qfluentwidgets.TransparentToolButton` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:355` |
| op-ae4adaf0089e445f | call/alias | `qfluentwidgets.VBoxLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:105` |
| op-4d905fe7b872383c | call/alias | `qfluentwidgets.setFont` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:189` |
| op-c8ff71f5432ce427 | call/alias | `qfluentwidgets.setTheme` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:227` |
| op-46b9939c6c07317e | call/alias | `qfluentwidgets.setThemeColor` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:228` |
| op-5da754bf93aa1e65 | call/alias | `qfluentwidgets.window.fluent_window.FluentTitleBar` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:77` |
| op-f4f63518077fdc49 | call/alias | `random.random` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:26` |
| op-eaf664bcc763efe4 | call/alias | `retry.retry` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:229` |
| op-c532d3547cd5793b | call/alias | `rich.console.Console` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:10` |
| op-a27b2df6f82235a7 | call/alias | `rich.markup.escape` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:97` |
| op-1cbe945e20c4a2ee | call/alias | `rich.traceback.install` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:58` |
| op-957ccf4c25d9fac7 | call/alias | `service.android_local_device.AndroidLocalControl` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:441` |
| op-a6820c5bb3d8d41e | call/alias | `service.android_local_device.AndroidLocalScreenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:457` |
| op-6b9956baab602918 | call/alias | `service.api.commands.execute_command` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:51` |
| op-4b8f6ab5d76e5a5b | call/alias | `service.auth.AuthenticationError` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:102` |
| op-5dcd632c29db7dfd | call/alias | `service.auth.JsonChaChaChannel` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:113` |
| op-9bddbdae3eeb34a8 | call/alias | `service.auth.ServiceAuthManager` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:37` |
| op-f73939216d9e04d0 | call/alias | `service.auth.b64d` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:28` |
| op-720ac02705c502e2 | call/alias | `service.auth.b64e` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:70` |
| op-70b0575ecc5dac2d | call/alias | `service.auth.canonical_dumps` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:97` |
| op-219f87b991922f7e | call/alias | `service.auth.hmac_sha256` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:95` |
| op-6d77dc5d0ba9fba3 | call/alias | `service.channels.ProviderChannelHandler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_provider.py:45` |
| op-d225f344564fd6f7 | call/alias | `service.channels.RemoteChannelHandler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_remote.py:23` |
| op-52a10d5706f3b3d4 | call/alias | `service.channels.SyncChannelHandler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_sync.py:41` |
| op-9ef8c2ae937171d5 | call/alias | `service.channels.TriggerChannelHandler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_trigger.py:24` |
| op-5b0ef97dfebcb877 | call/alias | `service.conf.ensure_safe_config_id` | 2 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `tests/service/test_config_runtime_update.py:48` |
| op-7609fe5a31dec6a7 | call/alias | `service.conf.initializer.ConfigInitializer` | 6 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/ops.py:7` |
| op-e23870d2cc2af7de | call/alias | `service.conf.manager.ConfigManager` | 7 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `tests/service/test_service_logic.py:99` |
| op-b01387f2767d773b | call/alias | `service.conf.resolve_config_dir` | 3 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `tests/service/test_config_runtime_update.py:52` |
| op-2f62258799e6d539 | call/alias | `service.context.ServiceContext` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/state.py:22` |
| op-192e9df30c99f5a7 | call/alias | `service.injection.prepare_service_imports` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:10` |
| op-3cb9237b8eb9efa5 | call/alias | `service.remote.ScrcpyProxySession` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/remote.py:20` |
| op-f2e686b2670dceb6 | call/alias | `service.runtime.ServiceRuntime` | 14 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:74` |
| op-2303a1989bb45417 | call/alias | `service.runtime._AndroidDisplayResizeGuard` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:30` |
| op-d1cbf353438d25ac | call/alias | `service.set_log_format` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:56` |
| op-d1965239e5a4e1c2 | call/alias | `service.system_logging.JsonLineFormatter` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:38` |
| op-2a414cd45d74651f | call/alias | `service.system_logging.clear_system_logs` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:95` |
| op-20b4cc206c039799 | call/alias | `service.system_logging.configure_dependency_log_levels` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:70` |
| op-3cab2cc16b36f7f9 | call/alias | `service.system_logging.read_system_logs` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:82` |
| op-2f1f4848abb45874 | call/alias | `service.system_logging.system_log_files` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:88` |
| op-db6a16b5cdb8970c | call/alias | `service.system_logging.system_log_path` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:88` |
| op-5e0d8c663ab03c2c | call/alias | `service.transport.InMemoryChannelEndpoint` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:96` |
| op-e66b2b8c164220b0 | call/alias | `service.transport.WebSocketChannelEndpoint` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_provider.py:45` |
| op-22d0736fac955487 | call/alias | `service.transport.framing.FrameDecoder` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:25` |
| op-cebde4d411a8b044 | call/alias | `service.transport.framing.encode_frame` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:26` |
| op-cc6088b87703c0c7 | call/alias | `service.transport.framing.encode_json` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:31` |
| op-f68b8bdac24c3f5f | call/alias | `service.transport.pipe_server.PipeTransportServer` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:57` |
| op-f8a4d636776ebd5e | call/alias | `service.transport.websocket_endpoint.WebSocketChannelEndpoint` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:57` |
| op-d3b85012b2730aec | call/alias | `service.types.CommandMessage` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:18` |
| op-ab3ed48d8554707a | call/alias | `service.types.PatchOperation` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:383` |
| op-d58b5e751472a426 | call/alias | `service.types.ProviderRequest` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:37` |
| op-c2ce8342f3ace2f9 | call/alias | `service.types.SyncPatchMessage` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:60` |
| op-1f94f49f4d3adf66 | call/alias | `service.types.SyncPullMessage` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:50` |
| op-4617590201744933 | call/alias | `service.types.SyncPushPayload` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:381` |
| op-74832eff48cf685f | call/alias | `service.update.checks._git_wrapper_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:77` |
| op-3f4594e42bba97a2 | call/alias | `service.update.read_setup_toml` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:260` |
| op-8b1fbda8cef00f71 | call/alias | `service.update.repository.update_repo_to_latest` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:736` |
| op-b805da8e664a452a | call/alias | `service.update.setup_io.read_setup_toml` | 13 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:376` |
| op-5d4d79fb94fc150a | call/alias | `service.update.setup_io.write_setup_toml` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:392` |
| op-2ec38f1af2da13d0 | call/alias | `service.update.setup_schema.legacy_repo_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:120` |
| op-f0b1d967d53ebfb3 | call/alias | `service.update.setup_schema.legacy_runtime_view` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:356` |
| op-f07dc900376a9d60 | call/alias | `service.update.setup_schema.migrate_to_current_schema` | 14 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:114` |
| op-e5a1bb00f02a29a0 | call/alias | `service.update.setup_schema.setup_channel` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:377` |
| op-b9cac437b2f2ec54 | call/alias | `service.utils.broadcast.BroadcastChannel` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:41` |
| op-9bf7385655191316 | call/alias | `service.utils.diff.PatchConflictError` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:344` |
| op-a6a08ae9010eb075 | call/alias | `service.utils.diff.apply_patch` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:348` |
| op-9540d7afc51f5c8c | call/alias | `service.utils.diff.diff_documents` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:470` |
| op-f613432fd90efb7b | call/alias | `service.utils.timestamps.file_mtime_ms` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:282` |
| op-9f8588cd2384c783 | call/alias | `service.utils.timestamps.unix_timestamp_ms` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:198` |
| op-451972a9aa703d75 | call/alias | `statistics.median` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/image.py:223` |
| op-bc706e01902de305 | call/alias | `time.monotonic` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:284` |
| op-2c6feed94f2b88be | call/alias | `time.sleep` | 3 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/device/scrcpy/control.py:259` |
| op-2d9c24e8b611f9c7 | call/alias | `toml_config.TOML_Config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:262` |
| op-2e302e313c3206bb | call/alias | `types.SimpleNamespace` | 39 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:417` |
| op-779d0cca421590f7 | call/alias | `urllib.parse.quote` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:198` |
| op-e9124cd7071d8809 | call/alias | `urllib.parse.urljoin` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:180` |
| op-270e1f086511b3ac | call/alias | `urllib.parse.urlparse` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:171` |
| op-502ead29595322bf | call/alias | `watchfiles.awatch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:496` |
| op-1fd6f0c8e4ae9069 | call/alias | `zipfile.ZipFile` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:783` |
| op-8b701a368f80ce90 | call/alias-member | `PyInstaller.__main__.run` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:546` |
| op-1e0eab57f02af259 | call/alias-member | `PyQt5.QtCore.QTimer.singleShot` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:64` |
| op-8aea4596b787438e | call/alias-member | `PyQt5.QtCore.Qt.QSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:824` |
| op-25c28787a3b05e91 | call/alias-member | `PyQt5.QtWidgets.QApplication.clipboard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:163` |
| op-92f905fae063dbe8 | call/alias-member | `PyQt5.QtWidgets.QApplication.desktop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:504` |
| op-4985c4fd412e3903 | call/alias-member | `PyQt5.QtWidgets.QApplication.processEvents` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:430` |
| op-6921a11c80fea045 | call/alias-member | `PyQt5.QtWidgets.QApplication.setAttribute` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:55` |
| op-e51b2ef4a7035b8f | call/alias-member | `PyQt5.QtWidgets.QApplication.setHighDpiScaleFactorRoundingPolicy` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:54` |
| op-0876aedfc4418993 | call/alias-member | `adbutils.adb.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:363` |
| op-671ab9a5c0188e8a | call/alias-member | `adbutils.adb.device` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:228` |
| op-d563c5b7a7d4e0f6 | call/alias-member | `adbutils.adb.device_list` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:86` |
| op-2184e23d1e5009ab | call/alias-member | `adbutils.adb.disconnect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:343` |
| op-4c6ca6e9618f1c87 | call/alias-member | `adbutils.adb.list` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:225` |
| op-7cb7bd08a55d9b66 | call/alias-member | `adbutils.adb.server_kill` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:299` |
| op-9e8198fd02fef11e | call/alias-member | `android_backend.bootstrap.restart` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:915` |
| op-e63ceab3ba4de1eb | call/alias-member | `argparse.ArgumentParser` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:31` |
| op-268f2947e28fcd8a | call/alias-member | `asyncio.Event` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/transport/pipe_endpoint.py:19` |
| op-f87bc66fc9dede02 | call/alias-member | `asyncio.Lock` | 8 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/api/ws_provider.py:29` |
| op-9ac9ebe7e41eea52 | call/alias-member | `asyncio.Queue` | 8 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/auth/manager.py:225` |
| op-b9040337b578a7f3 | call/alias-member | `asyncio.StreamReader` | 2 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `tests/service/pipe_live_smoke.py:26` |
| op-92b4a46db984d405 | call/alias-member | `asyncio.StreamReaderProtocol` | 2 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `tests/service/pipe_live_smoke.py:27` |
| op-086e344876944b42 | call/alias-member | `asyncio.StreamWriter` | 2 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `tests/service/pipe_live_smoke.py:29` |
| op-d08a7cafb916b828 | call/alias-member | `asyncio.as_completed` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/runtime.py:659` |
| op-ecbdf2d1ca9172eb | call/alias-member | `asyncio.create_task` | 14 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/api/ws_control.py:46` |
| op-d967acd8b23c9c62 | call/alias-member | `asyncio.gather` | 5 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `cli.example.py:57` |
| op-2d2b78ef440ee569 | call/alias-member | `asyncio.get_running_loop` | 12 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/conf/manager.py:485` |
| op-e908570a7a8835c2 | call/alias-member | `asyncio.new_event_loop` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/device/nemu_client.py:172` |
| op-a41a03adb70ba30e | call/alias-member | `asyncio.open_unix_connection` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `tests/service/test_pipe_transport.py:105` |
| op-44ab9dc83197ca45 | call/alias-member | `asyncio.run` | 40 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `cli.example.py:60` |
| op-8a9d93872697bf5d | call/alias-member | `asyncio.run_coroutine_threadsafe` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/utils/broadcast.py:48` |
| op-892ac652762cf588 | call/alias-member | `asyncio.sleep` | 7 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `cli.example.py:47` |
| op-16d033426f60bc6a | call/alias-member | `asyncio.to_thread` | 7 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `cli.example.py:20` |
| op-ee885759415c2f8a | call/alias-member | `asyncio.wait` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/remote/scrcpy.py:541` |
| op-b7d5832ca66bc5dc | call/alias-member | `asyncio.wait_for` | 4 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/device/nemu_client.py:267` |
| op-a3a1dbe460619ecd | call/alias-member | `av.codec.CodecContext.create` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:244` |
| op-66c23870b4cdf411 | call/alias-member | `base64.urlsafe_b64decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:26` |
| op-721685c61e253520 | call/alias-member | `base64.urlsafe_b64encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:22` |
| op-9ef529ed02fff689 | call/alias-member | `const.get_remote_sha_methods.pop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:158` |
| op-1d8cec49f5e4340a | call/alias-member | `copy.deepcopy` | 17 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:714` |
| op-f181ab15c393772d | call/alias-member | `core.color.check_sweep_availability` | 16 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/activities/bunnyChaserOnTheShip.py:40` |
| op-51a8a7b4256d1b66 | call/alias-member | `core.color.compareTotalRGBDiff` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/dailyGameActivities/serikaSummerRamenStall.py:84` |
| op-7132973b0633ee3e | call/alias-member | `core.color.create_rgb_feature` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/shop/shop_utils.py:77` |
| op-09831949c422d0b4 | call/alias-member | `core.color.getRegionMeanRGB` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/dailyGameActivities/serikaSummerRamenStall.py:82` |
| op-6a5dee3ad933f54c | call/alias-member | `core.color.match_rgb_feature` | 4 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/picture.py:102` |
| op-af0981902f08b6b4 | call/alias-member | `core.color.remove_rgb_feature` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/shop/shop_utils.py:84` |
| op-b36f97dd02a5a752 | call/alias-member | `core.color.rgb_in_range` | 87 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/activities/bunnyChaserOnTheShip.py:67` |
| op-3385720db3d1d3ef | call/alias-member | `core.color.wait_loading` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/picture.py:96` |
| op-7472e7e4cda3b1e3 | call/alias-member | `core.config.config_set.ConfigSet._init_static_config` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/injection.py:189` |
| op-a97ae98e0b77e8b0 | call/alias-member | `core.device.connection.Connection._split_serial` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `service/injection.py:389` |
| op-d13062b55247ae81 | call/alias-member | `core.device.connection.Connection.revise_serial` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `service/injection.py:349` |
| op-a55448d9d4d2d0fb | call/alias-member | `core.device.emulator_manager.autosearch` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/connection.py:164` |
| op-e86e49a3317d0632 | call/alias-member | `core.device.emulator_manager.process_api.start` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:289` |
| op-9c78a83c48ef891f | call/alias-member | `core.device.emulator_manager.process_api.terminate` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:976` |
| op-50378c7856de200b | call/alias-member | `core.device.emulator_manager.start_simulator_classic` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:271` |
| op-2eace0ebb39e9151 | call/alias-member | `core.device.emulator_manager.stop_simulator_classic` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:973` |
| op-76770f68a2365b03 | call/alias-member | `core.device.nemu_client.NemuClient.get_instance` | 4 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/nemu.py:94` |
| op-b0d7989b15ca332f | call/alias-member | `core.device.nemu_client.NemuClient.get_possible_mumu12_folder` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/nemu.py:98` |
| op-aa930bbb529b65dc | call/alias-member | `core.device.nemu_client.NemuClient.serial_to_id` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/nemu.py:91` |
| op-891766e85fe89693 | call/alias-member | `core.device.scrcpy_client.ScrcpyClient.get_instance` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/scrcpy.py:7` |
| op-e4ea483cc9aa1716 | call/alias-member | `core.device.uiautomator2_client.BAAS_U2_Initer.__new__` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `tests/core/device/test_uiautomator2_client.py:9` |
| op-3f98c7c470e6e054 | call/alias-member | `core.device.uiautomator2_client.U2Client.get_instance` | 5 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:1099` |
| op-3cb02454f65bead3 | call/alias-member | `core.geometry.triangle.Triangle` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/geometry/parallelogram.py:10` |
| op-0b7e8201a788369e | call/alias-member | `core.image.click_to_disappear` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/cafe_reward.py:319` |
| op-2b0059aa6fcd3071 | call/alias-member | `core.image.click_until_image_disappear` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/create.py:134` |
| op-68d85b6295ad1ebf | call/alias-member | `core.image.click_until_template_disappear` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/momo_talk.py:74` |
| op-f01054288f0e3799 | call/alias-member | `core.image.compare_image` | 17 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/picture.py:216` |
| op-229557110a42f16c | call/alias-member | `core.image.detect` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/activities/bunnyChaserOnTheShip2.py:271` |
| op-cd6eb76e76e66ee1 | call/alias-member | `core.image.get_image_all_appear_position` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/cafe_reward.py:416` |
| op-fce118b77ec43b94 | call/alias-member | `core.image.screenshot_cut` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/create.py:815` |
| op-e8c7d7cbde7aa398 | call/alias-member | `core.image.search_image_in_area` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/create.py:818` |
| op-9ff2229c659691ec | call/alias-member | `core.image.search_in_area` | 5 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/create.py:854` |
| op-70763f4d50b2ddab | call/alias-member | `core.image.swipe_search_target_str` | 4 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `develop_tools/test/test_explore_normal_task.py:68` |
| op-d6c5439ac0c434ca | call/alias-member | `core.ipc_manager.SharedMemory.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1059` |
| op-6ab3f6f45369c461 | call/alias-member | `core.ipc_manager.SharedMemory.release` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:95` |
| op-8173c79f120590b4 | call/alias-member | `core.ipc_manager.SharedMemory.set_data` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:241` |
| op-55d1166290fddc83 | call/alias-member | `core.ocr.baas_ocr_client.Client.BaasOcrClient` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/ocr.py:97` |
| op-8ecafa0db5061b63 | call/alias-member | `core.ocr.baas_ocr_client.server_installer._install_android_prebuild` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `tests/core/ocr/test_android_runtime.py:37` |
| op-0b29c9d65123ce31 | call/alias-member | `core.ocr.ocr.Baas_ocr` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `main.py:37` |
| op-7cd535fb26cfbe1c | call/alias-member | `core.picture.co_detect` | 185 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/Baas_thread.py:695` |
| op-67beaeb6ed31c2db | call/alias-member | `core.picture.match_img_feature` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/explore_tasks/task_utils.py:27` |
| op-bca3dac9ffa29bd8 | call/alias-member | `core.position.get_area` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/image.py:25` |
| op-a7c2b4497183dad0 | call/alias-member | `core.position.init_image_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:699` |
| op-16fea1ac0ed321ef | call/alias-member | `core.utils.Logger` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:103` |
| op-eb947e1b10af9375 | call/alias-member | `cryptography.hazmat.primitives.asymmetric.ed25519.Ed25519PrivateKey.from_private_bytes` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:569` |
| op-06002cd008495767 | call/alias-member | `cryptography.hazmat.primitives.asymmetric.ed25519.Ed25519PublicKey.from_public_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:609` |
| op-769fa0ad1fd4903e | call/alias-member | `cryptography.hazmat.primitives.asymmetric.x25519.X25519PrivateKey.generate` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:80` |
| op-e0c5c39cdd8e9603 | call/alias-member | `cryptography.hazmat.primitives.asymmetric.x25519.X25519PublicKey.from_public_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:109` |
| op-e101298604629c09 | call/alias-member | `cryptography.hazmat.primitives.hashes.SHA256` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:35` |
| op-631933e4b4f77634 | call/alias-member | `ctypes.CDLL` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:189` |
| op-5cc60f8b263024eb | call/alias-member | `ctypes.POINTER` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:308` |
| op-a6d5197146161941 | call/alias-member | `ctypes.WINFUNCTYPE` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:69` |
| op-2930d27fb1776bfa | call/alias-member | `ctypes.byref` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:69` |
| op-a748dc2a482bac78 | call/alias-member | `ctypes.c_int` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:68` |
| op-2c5c2029a70b3f0c | call/alias-member | `ctypes.c_void_p` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:112` |
| op-8df56cf14d11d4ce | call/alias-member | `ctypes.cast` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:93` |
| op-57292de1cef9e36b | call/alias-member | `ctypes.create_unicode_buffer` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:47` |
| op-348a5749be1a7b7e | call/alias-member | `ctypes.pointer` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:306` |
| op-f56312983b9f3067 | call/alias-member | `ctypes.sizeof` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:166` |
| op-31fa7798a5b0a751 | call/alias-member | `ctypes.windll.kernel32.GetCurrentThreadId` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:119` |
| op-b3ff0aa39f430af6 | call/alias-member | `ctypes.windll.kernel32.GetLastError` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:133` |
| op-735e5b864a780a14 | call/alias-member | `ctypes.windll.user32.CallNextHookEx` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:115` |
| op-bfff83eb0bb26422 | call/alias-member | `ctypes.windll.user32.ClientToScreen` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:70` |
| op-848cce7716b333d8 | call/alias-member | `ctypes.windll.user32.DispatchMessageW` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:139` |
| op-6798f28569661c79 | call/alias-member | `ctypes.windll.user32.GetClientRect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:68` |
| op-51ecb9aa3317f975 | call/alias-member | `ctypes.windll.user32.GetMessageW` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:137` |
| op-f97e16bdf8e47f4c | call/alias-member | `ctypes.windll.user32.GetWindowRect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:154` |
| op-f885cfe027929252 | call/alias-member | `ctypes.windll.user32.GetWindowTextLengthW` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:45` |
| op-1ad381344a5552d1 | call/alias-member | `ctypes.windll.user32.GetWindowTextW` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:48` |
| op-bb9abb35e4240e35 | call/alias-member | `ctypes.windll.user32.IsWindow` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:40` |
| op-e3269c386093c7af | call/alias-member | `ctypes.windll.user32.IsZoomed` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:97` |
| op-3a1f2aaa37677ad1 | call/alias-member | `ctypes.windll.user32.PostThreadMessageW` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:148` |
| op-b3c9f914b29b580e | call/alias-member | `ctypes.windll.user32.SetWindowPos` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:174` |
| op-06f2d9181ea888ca | call/alias-member | `ctypes.windll.user32.SetWindowsHookExW` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:125` |
| op-e73bf52799b08302 | call/alias-member | `ctypes.windll.user32.TranslateMessage` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:138` |
| op-3a01a0db39a47a6d | call/alias-member | `ctypes.windll.user32.UnhookWindowsHookEx` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:142` |
| op-3af39f1946077035 | call/alias-member | `ctypes.wintypes.MSG` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:136` |
| op-54fb7e1bef837a87 | call/alias-member | `ctypes.wintypes.POINT` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:69` |
| op-37749cb63439e803 | call/alias-member | `ctypes.wintypes.RECT` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:67` |
| op-6e2beb76071dddb1 | call/alias-member | `cv2.cvtColor` | 10 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/Baas_thread.py:170` |
| op-aa8eaa6454020966 | call/alias-member | `cv2.flip` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/nemu_client.py:346` |
| op-e312d16421bbfbc1 | call/alias-member | `cv2.imdecode` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/screenshot/adb.py:18` |
| op-e9d42dd25be4f5e0 | call/alias-member | `cv2.imencode` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/ocr/baas_ocr_client/Client.py:281` |
| op-bcdbf04f597d8b1a | call/alias-member | `cv2.imread` | 7 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/position.py:53` |
| op-bd83af06eecc29fd | call/alias-member | `cv2.imwrite` | 4 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `develop_tools/lesson/lesson_affection_student_image_extractor.py:61` |
| op-3ff1dc688cb1205f | call/alias-member | `cv2.matchTemplate` | 8 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:32` |
| op-047761bfe69b23cd | call/alias-member | `cv2.minMaxLoc` | 4 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:50` |
| op-027fe4d5d1aeb23b | call/alias-member | `cv2.resize` | 4 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:30` |
| op-f41860d79ce712eb | call/alias-member | `cv2.rotate` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/Baas_thread.py:186` |
| op-18ddec57a80b5024 | call/alias-member | `datetime.datetime` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:73` |
| op-6276c96f7e9d7546 | call/alias-member | `datetime.datetime.fromtimestamp` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:904` |
| op-bde5f0dfe00ff532 | call/alias-member | `datetime.datetime.now` | 22 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:902` |
| op-5a391ede8d12247a | call/alias-member | `datetime.datetime.strptime` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:283` |
| op-ed7ae52b8842f3a1 | call/alias-member | `datetime.timedelta` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:144` |
| op-a15c3ef3999454de | call/alias-member | `deploy.installer.const.get_remote_sha_methods.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:450` |
| op-964ff0780b9e4b1a | call/alias-member | `deploy.installer.mirrorc_update.mirrorc_updater.MirrorC_Updater.apply_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:551` |
| op-4c4e5e97368f10f0 | call/alias-member | `functools.wraps` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:18` |
| op-133b7df35a949f97 | call/alias-member | `gc.collect` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:787` |
| op-98d2f082427b5123 | call/alias-member | `getpass.getpass` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:257` |
| op-cd36f4751b3a2716 | call/alias-member | `gui.util.config_gui.configGui.appRestartSig.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:226` |
| op-522ffa12ca0a1094 | call/alias-member | `gui.util.config_gui.configGui.get` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:227` |
| op-1fdeeda5a90b004f | call/alias-member | `gui.util.config_gui.configGui.language.valueChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:305` |
| op-48275360b4eb71da | call/alias-member | `gui.util.config_gui.configGui.micaEnableChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:669` |
| op-f6c538c4346ebb89 | call/alias-member | `gui.util.config_gui.configGui.micaEnableChanged.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:229` |
| op-3752f40b8edf51e2 | call/alias-member | `gui.util.config_gui.configGui.set` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:328` |
| op-3976244d99ffde23 | call/alias-member | `gui.util.config_gui.configGui.themeChanged.connect` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:135` |
| op-445715aec9f83be7 | call/alias-member | `gui.util.customized_ui.LineWidget.get_combo_box` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:46` |
| op-ce933cb48061cd2d | call/alias-member | `gui.util.hotkey_manager.HotkeyInputDialog.get_hotkey` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:201` |
| op-0e27e73aa99ab0c8 | call/alias-member | `gui.util.language.Language.combobox` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:154` |
| op-42486ed55d398e6f | call/alias-member | `gui.util.language.Language.get_raw` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:319` |
| op-e189b4561d697209 | call/alias-member | `gui.util.notification.error` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:185` |
| op-3cd3ee8536821a71 | call/alias-member | `gui.util.notification.success` | 22 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:83` |
| op-382394ae4aeea972 | call/alias-member | `gui.util.style_sheet.StyleSheet.PROCESS.apply` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:136` |
| op-91cd376837b712d2 | call/alias-member | `gui.util.translator.baasTranslator.addStudent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:45` |
| op-e4fac255a811edea | call/alias-member | `gui.util.translator.baasTranslator.getStudent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:38` |
| op-258b79eb39d25ecb | call/alias-member | `gui.util.translator.baasTranslator.isChinese` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:42` |
| op-341b0587210970dd | call/alias-member | `gui.util.translator.baasTranslator.tr` | 28 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:59` |
| op-eeac26a895356315 | call/alias-member | `gui.util.translator.baasTranslator.undo` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:67` |
| op-ea27403e3236670f | call/alias-member | `hashlib.sha256` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:111` |
| op-798542f6caf6e409 | call/alias-member | `hmac.compare_digest` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:151` |
| op-299faebfbe5ccc1a | call/alias-member | `hmac.new` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:43` |
| op-cd38e1d5c5feb967 | call/alias-member | `html.escape` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:185` |
| op-c92acb7a597c0c69 | call/alias-member | `importlib.import_module` | 10 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/position.py:42` |
| op-04150460faa409b8 | call/alias-member | `importlib.util.find_spec` | 1 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/config/default_config.py:5168` |
| op-c8dd03e66ec901f6 | call/alias-member | `inspect.isawaitable` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:490` |
| op-0c33f55d715bb34f | call/alias-member | `io.BytesIO` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:22` |
| op-f9263b0dfbb17d17 | call/alias-member | `io.TextIOWrapper` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:22` |
| op-3963537e0bf737f6 | call/alias-member | `ipaddress.ip_address` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:162` |
| op-2f669511eb8ed0b0 | call/alias-member | `json.dump` | 11 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/config/config_set.py:78` |
| op-0c5a4451292bd478 | call/alias-member | `json.dumps` | 42 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/ocr/baas_ocr_client/Client.py:198` |
| op-a993cd23389d1b3b | call/alias-member | `json.load` | 23 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/Baas_thread.py:404` |
| op-c6438e1286fe155c | call/alias-member | `json.loads` | 42 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/device/emulator_manager/get_adb_address.py:34` |
| op-6cf011190b7c3380 | call/alias-member | `logging.Formatter` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/utils.py:60` |
| op-452097a74e113918 | call/alias-member | `logging.LogRecord` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `tests/service/test_system_logging.py:39` |
| op-7a931d02a29fc8b2 | call/alias-member | `logging.StreamHandler` | 2 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/utils.py:61` |
| op-974d3396ad34e1c9 | call/alias-member | `logging.basicConfig` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/device/nemu_client.py:419` |
| op-36cd1d8bcafb888d | call/alias-member | `logging.captureWarnings` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/system_logging.py:83` |
| op-9289530445061a32 | call/alias-member | `logging.error` | 8 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/update/repository.py:227` |
| op-580e494f2d32dd64 | call/alias-member | `logging.getLogger` | 38 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/device/nemu_client.py:420` |
| op-f1f605f6e6fafc65 | call/alias-member | `logging.handlers.RotatingFileHandler` | 2 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/system_logging.py:71` |
| op-b53866a9609cea8b | call/alias-member | `logging.info` | 29 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/conf/manager.py:530` |
| op-a7bf99e7bbf12882 | call/alias-member | `logging.root.handlers.pop` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/utils.py:84` |
| op-ef59f3c8f003988a | call/alias-member | `logging.warning` | 10 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/update/repository.py:196` |
| op-5f36cc34c9c8f31e | call/alias-member | `loguru.logger.add` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:299` |
| op-caef60efe7dad0e8 | call/alias-member | `loguru.logger.critical` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:919` |
| op-f40a66b6c18aba7c | call/alias-member | `loguru.logger.error` | 17 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:107` |
| op-d90ccc0cd0a68a10 | call/alias-member | `loguru.logger.exception` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:377` |
| op-fc8c57baf40f2e19 | call/alias-member | `loguru.logger.info` | 131 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:149` |
| op-481b1ec5a5ab307b | call/alias-member | `loguru.logger.remove` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:298` |
| op-a1c279db41d60cad | call/alias-member | `loguru.logger.success` | 32 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:198` |
| op-a09259340af9f842 | call/alias-member | `loguru.logger.warning` | 17 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:125` |
| op-8db54ea8b11882ad | call/alias-member | `lxml.etree.parse` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:77` |
| op-d5e0e5c2e1d4bd74 | call/alias-member | `math.gcd` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:1087` |
| op-05d2cf3d15fe7a3f | call/alias-member | `math.sqrt` | 2 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/scrcpy_client.py:99` |
| op-d5f2499ebd5e2058 | call/alias-member | `mirrorc_update.mirrorc_updater.MirrorC_Updater.apply_update` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1175` |
| op-014453a72f8f1d40 | call/alias-member | `mirrorc_update.mirrorc_updater.MirrorC_Updater.log_mirrorc_error` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1105` |
| op-6966a68f20737d18 | call/alias-member | `module.ExploreTasks.TaskUtils.to_hard_event` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:57` |
| op-849cc76404475c19 | call/alias-member | `module.ExploreTasks.TaskUtils.to_normal_event` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:70` |
| op-832e89a50a9aadf7 | call/alias-member | `module.cafe_reward.gift_to_cafe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/module/test_cafe_reward_match.py:107` |
| op-20c61d53df373c4a | call/alias-member | `module.cafe_reward.match` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/module/test_cafe_reward_match.py:36` |
| op-31f24ee4f3eb4505 | call/alias-member | `module.collect_daily_task_power.implement` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_reward.py:5` |
| op-9fa6a49049e8b2ff | call/alias-member | `module.main_story.auto_fight` | 19 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:67` |
| op-d8aa865757928508 | call/alias-member | `mss.mss` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/screenshot/mss.py:9` |
| op-efa274a290552570 | call/alias-member | `multiprocessing.shared_memory.SharedMemory` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ipc_manager.py:43` |
| op-ab381b0e9923e913 | call/alias-member | `numpy.abs` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/color.py:156` |
| op-4cb26b8c59d164f3 | call/alias-member | `numpy.append` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/control/nemu.py:72` |
| op-c7b6b30a1c676dc1 | call/alias-member | `numpy.arange` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/control/nemu.py:52` |
| op-e6f1b5dd95d258d3 | call/alias-member | `numpy.array` | 17 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/Baas_thread.py:170` |
| op-10765228a645812d | call/alias-member | `numpy.cos` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/control/nemu.py:14` |
| op-cad99a4f9d06deb9 | call/alias-member | `numpy.ctypeslib.as_array` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/nemu_client.py:344` |
| op-025c932be18a8083 | call/alias-member | `numpy.frombuffer` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/screenshot/adb.py:17` |
| op-df1c5eaf80c7fdaf | call/alias-member | `numpy.full` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/total_assault.py:112` |
| op-953a9d516cee5f89 | call/alias-member | `numpy.linalg.norm` | 3 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/control/nemu.py:44` |
| op-88cbfb9456f738dc | call/alias-member | `numpy.mean` | 3 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/control/nemu.py:8` |
| op-0fd4ad1b4eb03799 | call/alias-member | `numpy.random.uniform` | 4 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/Baas_thread.py:161` |
| op-8854bcf46f6101fa | call/alias-member | `numpy.sign` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/control/nemu.py:54` |
| op-cd5ab38d50dc6afe | call/alias-member | `numpy.sin` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/control/nemu.py:14` |
| op-47b06f79ec7c3fa5 | call/alias-member | `numpy.subtract` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/device/control/nemu.py:63` |
| op-6e87e8439fa13f2e | call/alias-member | `numpy.unravel_index` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/dailyGameActivities/serikaSummerRamenStall.py:80` |
| op-f1cea898ce5df83a | call/alias-member | `numpy.where` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:143` |
| op-5bdccbd071e3f3c5 | call/alias-member | `numpy.zeros` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `module/total_assault.py:319` |
| op-7bd3e836115e8c08 | call/alias-member | `os.chmod` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:234` |
| op-0fc85dea8e5e0c90 | call/alias-member | `os.close` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:66` |
| op-27271769be670ba9 | call/alias-member | `os.dup` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:54` |
| op-e325492327505aaf | call/alias-member | `os.dup2` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:41` |
| op-4f2aec2f48e3e428 | call/alias-member | `os.environ.copy` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:66` |
| op-08488c4d4c004940 | call/alias-member | `os.environ.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:154` |
| op-088abc649d414168 | call/alias-member | `os.execv` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:646` |
| op-343805df05cfd31e | call/alias-member | `os.fdopen` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:42` |
| op-05129c27cc757686 | call/alias-member | `os.fsencode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:310` |
| op-13bfc4f59f18e2fe | call/alias-member | `os.getcwd` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:297` |
| op-47bca5f05a356580 | call/alias-member | `os.getenv` | 40 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:363` |
| op-14731aa937f1de1c | call/alias-member | `os.getpid` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:77` |
| op-d796d2c44f75f5cd | call/alias-member | `os.listdir` | 13 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/notification.py:17` |
| op-a2cffb624ed19a94 | call/alias-member | `os.makedirs` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/device_config.py:50` |
| op-bb1342f16d0478fb | call/alias-member | `os.mkdir` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:31` |
| op-2dfbdbc17a4d094b | call/alias-member | `os.path.abspath` | 23 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/Baas_thread.py:75` |
| op-7622f93b402f3146 | call/alias-member | `os.path.basename` | 6 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/Baas_thread.py:123` |
| op-24ea0804bb0813dd | call/alias-member | `os.path.dirname` | 33 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/Baas_thread.py:75` |
| op-8500d2ba5370ee1b | call/alias-member | `os.path.exists` | 94 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/Baas_thread.py:364` |
| op-34773c11520e9c70 | call/alias-member | `os.path.expanduser` | 1 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/device/uiautomator2_client.py:15` |
| op-e590d8785fa0da97 | call/alias-member | `os.path.isdir` | 6 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/ocr/baas_ocr_client/Client.py:69` |
| op-c72491530c969be6 | call/alias-member | `os.path.isfile` | 1 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/device/emulator_manager/process_api.py:39` |
| op-3005628e56501e51 | call/alias-member | `os.path.join` | 81 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/Baas_thread.py:363` |
| op-d161e32d9303f4c8 | call/alias-member | `os.path.normcase` | 2 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/device/emulator_manager/process_api.py:64` |
| op-24e27ac48fa4ccdc | call/alias-member | `os.path.relpath` | 1 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `deploy/installer/_installer.py:723` |
| op-ea635fa05bcbae85 | call/alias-member | `os.path.splitext` | 1 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `develop_tools/auto_translate.py:147` |
| op-cbc1b7fd8e1513b1 | call/alias-member | `os.pipe` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:52` |
| op-abc22e764eca920a | call/alias-member | `os.read` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:78` |
| op-a9604ad5306d0a67 | call/alias-member | `os.remove` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:384` |
| op-b32ee66dbf507f46 | call/alias-member | `os.rename` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:569` |
| op-17acfbd20c521b54 | call/alias-member | `os.stat` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:234` |
| op-ac51882e31489324 | call/alias-member | `os.system` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:212` |
| op-992c22ca1ffe721e | call/alias-member | `os.utime` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:191` |
| op-8c42a158cb13f7bb | call/alias-member | `os.walk` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:185` |
| op-6d7abbd1c24495de | call/alias-member | `pathlib.Path.cwd` | 12 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `deploy/installer/installer.py:437` |
| op-36d9521875a042dd | call/alias-member | `platform.machine` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:51` |
| op-83a36b1c447c2272 | call/alias-member | `platform.system` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:8` |
| op-aebb00093f62aa31 | call/alias-member | `psutil.Process` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/process_api.py:66` |
| op-99ed4bf0422ea05f | call/alias-member | `psutil.pid_exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:515` |
| op-4c8a2ce2dc27e423 | call/alias-member | `psutil.process_iter` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:257` |
| op-399c1e211397a71d | call/alias-member | `pyautogui.click` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:63` |
| op-92345b96ecb4a5ac | call/alias-member | `pyautogui.dragTo` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:27` |
| op-d52da043c0830305 | call/alias-member | `pyautogui.getWindowsWithTitle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:53` |
| op-1d5b291c7400f628 | call/alias-member | `pyautogui.mouseDown` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:59` |
| op-9222dd363a864d08 | call/alias-member | `pyautogui.mouseUp` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:61` |
| op-b2ed32011583451c | call/alias-member | `pyautogui.moveTo` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:26` |
| op-5e2f290f687b28b7 | call/alias-member | `pyautogui.screenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/screenshot/pyautogui.py:13` |
| op-b1c049ca570799c6 | call/alias-member | `pyautogui.scroll` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:33` |
| op-3193c5db33905d94 | call/alias-member | `pygit2.Repository` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:287` |
| op-d763ed6224b56870 | call/alias-member | `pygit2.clone_repository` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:334` |
| op-2c0655af1eef08e8 | call/alias-member | `pygit2.init_repository` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:306` |
| op-13c714389c174913 | call/alias-member | `pytest.importorskip` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/module/test_cafe_reward_match.py:8` |
| op-f641b3f0dcedc99c | call/alias-member | `pytest.mark.skipif` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:42` |
| op-6b2b4eb29492f066 | call/alias-member | `pytest.raises` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/device/test_uiautomator2_client.py:68` |
| op-df92286dd17fa9ac | call/alias-member | `pythoncom.CoInitialize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/process_api.py:49` |
| op-4ca195568ce52fb8 | call/alias-member | `qfluentwidgets.FluentIcon.HELP.icon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:377` |
| op-f99585d0445f3cf4 | call/alias-member | `qfluentwidgets.FluentIcon.HISTORY.icon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:375` |
| op-7c22ae6b7f68cf36 | call/alias-member | `qfluentwidgets.FluentIcon.LANGUAGE.icon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:379` |
| op-86aba7d9e3abccbe | call/alias-member | `qfluentwidgets.qconfig.load` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:101` |
| op-ac4b44977c3490a4 | call/alias-member | `queue.Queue` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:149` |
| op-bbb3ab3eb5c9979a | call/alias-member | `re.compile` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:690` |
| op-ced586b00b350aed | call/alias-member | `re.escape` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/bluestacks_module.py:31` |
| op-2f66e4becc3610c2 | call/alias-member | `re.findall` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:118` |
| op-3fd2ce3acfd3af61 | call/alias-member | `re.finditer` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:288` |
| op-498ee3e9db00f0c7 | call/alias-member | `re.match` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:357` |
| op-e61880c702f1d27c | call/alias-member | `re.search` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:95` |
| op-cc801e8ab82f3d65 | call/alias-member | `re.sub` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:101` |
| op-cdc75ef6e48bfd96 | call/alias-member | `requests.get` | 27 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:420` |
| op-a5958fbf211fd3cf | call/alias-member | `requests.post` | 27 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:84` |
| op-7d3ca7b67bfe3638 | call/alias-member | `secrets.token_bytes` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:53` |
| op-3593b96536e29f1c | call/alias-member | `service.android_ocr_client.BaasOcrClient.__new__` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/ocr/test_android_runtime.py:53` |
| op-f780c8c92f45f428 | call/alias-member | `service.api.commands.execute_command` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_commands.py:39` |
| op-056196303cbe229b | call/alias-member | `service.api.security.finalize_control_auth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:127` |
| op-ef81e5d36fa39c3b | call/alias-member | `service.api.security.is_allowed_origin` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:90` |
| op-8af5d6be822b05fa | call/alias-member | `service.api.security.perform_business_resume` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:149` |
| op-b6ffa3ebb78d160f | call/alias-member | `service.api.security.recv_stream_json` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:104` |
| op-882415ac030afd4b | call/alias-member | `service.api.security.send_stream_json` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:103` |
| op-9ff5f6c47d0b33d8 | call/alias-member | `service.api.ws_provider.provider_sender` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:45` |
| op-6b376cb83ba86a2d | call/alias-member | `service.api.ws_sync.apply_sync_patch` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_sync_conflict.py:36` |
| op-74ec637b1abc4135 | call/alias-member | `service.api.ws_sync.sync_sender` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:31` |
| op-916ba4433545e79f | call/alias-member | `service.conf.initializer.ConfigInitializer.check_single_event` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/ops.py:11` |
| op-95ab4252782d2e3d | call/alias-member | `service.conf.initializer.ConfigInitializer.update_config_reserve_old` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/ops.py:27` |
| op-a820954f5095bea8 | call/alias-member | `service.conf.manager.ConfigManager.__new__` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `tests/service/test_config_runtime_update.py:354` |
| op-3e7650f094d728ab | call/alias-member | `service.injection._patch_cafe_reward` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/module/test_cafe_reward_match.py:19` |
| op-49b054d88574b5ae | call/alias-member | `service.injection._patch_logger` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_injection.py:30` |
| op-d1c3c36ac2188eb1 | call/alias-member | `service.remote.scrcpy.ScrcpyClient` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `tests/service/test_scrcpy_client.py:38` |
| op-0e0e9737861fd3df | call/alias-member | `service.transport.framing.HEADER.unpack` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:17` |
| op-44d33dd99c50fadb | call/alias-member | `service.update.checks.GitOperationHandler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:39` |
| op-0aea8822749b418f | call/alias-member | `service.update.checks.VersionInfo` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:179` |
| op-6d4c0c4b61493684 | call/alias-member | `service.update.checks._github_archive_url_for_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:101` |
| op-c0a528114d53b7a3 | call/alias-member | `service.update.checks.check_for_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:200` |
| op-b2dbd9c577282cd6 | call/alias-member | `service.update.checks.test_repo_sha` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:80` |
| op-e67b2296de2586bf | call/alias-member | `shlex.quote` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:199` |
| op-4bbf68e565ba7e19 | call/alias-member | `shutil.copy` | 4 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/ocr/baas_ocr_client/Client.py:32` |
| op-c5076789b0f12dc2 | call/alias-member | `shutil.copy2` | 7 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `deploy/installer/_installer.py:224` |
| op-a3b9e997bd7b61b3 | call/alias-member | `shutil.copytree` | 5 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:231` |
| op-4e9c9c053c2e5b69 | call/alias-member | `shutil.ignore_patterns` | 1 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `service/update/checks.py:646` |
| op-fbf75c083d121ddf | call/alias-member | `shutil.move` | 3 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `deploy/installer/_installer.py:820` |
| op-f5cff346c2e5cf3a | call/alias-member | `shutil.rmtree` | 33 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/ocr/baas_ocr_client/Client.py:75` |
| op-aafe7fcde1decbd5 | call/alias-member | `shutil.which` | 6 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:254` |
| op-49bb09af53004a29 | call/alias-member | `socket.socket` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/wsa_support.py:7` |
| op-6ec49bdf0e35785e | call/alias-member | `struct.Struct` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/framing.py:13` |
| op-826ea8c4eef34426 | call/alias-member | `struct.pack` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:20` |
| op-be0797db90bd4944 | call/alias-member | `struct.unpack` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:166` |
| op-d5ebeacedf936e14 | call/alias-member | `subprocess.CalledProcessError` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:151` |
| op-a99ebc4a865d175a | call/alias-member | `subprocess.Popen` | 15 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/process_api.py:42` |
| op-289268bbe3e5c936 | call/alias-member | `subprocess.check_output` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_start_command.py:17` |
| op-f0d262f6e29c2c90 | call/alias-member | `subprocess.run` | 38 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1001` |
| op-0cbbb4f69b49c92e | call/alias-member | `sys.argv.copy` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:979` |
| op-05861b5092241193 | call/alias-member | `sys.exit` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:591` |
| op-b8663bacdbb0c011 | call/alias-member | `sys.getdefaultencoding` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_start_command.py:20` |
| op-9648268c28a9b8b4 | call/alias-member | `sys.getwindowsversion` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:81` |
| op-8e2b404099d4cd47 | call/alias-member | `sys.path.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:31` |
| op-7babcc637df6daab | call/alias-member | `sys.path.insert` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:10` |
| op-a26003620488e3ae | call/alias-member | `sys.platform.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/utils.py:16` |
| op-bc2b3af9150acfe7 | call/alias-member | `sys.stderr.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:45` |
| op-00dca26e7f9f4170 | call/alias-member | `sys.stderr.fileno` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:51` |
| op-9e27bc90c8b01255 | call/alias-member | `sys.stdout.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:40` |
| op-212138ecb9bf2a96 | call/alias-member | `sys.stdout.fileno` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:50` |
| op-4c7539883dda100a | call/alias-member | `tempfile.TemporaryDirectory` | 18 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:221` |
| op-9ae350e3e9412f70 | call/alias-member | `threading.Event` | 4 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `deploy/installer/installer.py:125` |
| op-02ed9c8c1ce67d9d | call/alias-member | `threading.Lock` | 11 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `cli.example.py:8` |
| op-4b19b29606b31b76 | call/alias-member | `threading.RLock` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `service/auth/manager.py:44` |
| op-98c2df505124cf6e | call/alias-member | `threading.Thread` | 26 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/Baas_thread.py:137` |
| op-5e1cd65d82f73214 | call/alias-member | `threading.Timer` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/utils.py:26` |
| op-1d21b0c3c35bcb8e | call/alias-member | `time.localtime` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1108` |
| op-eae64b699968c4ed | call/alias-member | `time.perf_counter` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:43` |
| op-03dcb7e9ce25c8e5 | call/alias-member | `time.sleep` | 79 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/Baas_thread.py:160` |
| op-43a196680c855686 | call/alias-member | `time.strftime` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1108` |
| op-370d13bfe9f56272 | call/alias-member | `time.time` | 81 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:459` |
| op-7d1fc9e3e90fac5c | call/alias-member | `tomli.load` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/toml_config.py:67` |
| op-d1f4d92e21473420 | call/alias-member | `tomli_w.dump` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:259` |
| op-37d787a04f7b36bd | call/alias-member | `traceback.format_exc` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:504` |
| op-585a26217e749c43 | call/alias-member | `traceback.print_exc` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/exception.py:61` |
| op-f97301471ea08125 | call/alias-member | `translators.translate_html` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:57` |
| op-e85d2ceaae563d53 | call/alias-member | `translators.translate_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:52` |
| op-802a5ebe8697a7d2 | call/alias-member | `types.ModuleType` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:39` |
| op-3a069a23cc702bc9 | call/alias-member | `types.SimpleNamespace` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_injection.py:10` |
| op-052bf8b5a37b8ef6 | call/alias-member | `uiautomator2.connect` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1129` |
| op-450ade50ca8db36d | call/alias-member | `uiautomator2.connect_usb` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:28` |
| op-04f0158458deb061 | call/alias-member | `uiautomator2.version.__atx_agent_version__.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:166` |
| op-636f7b211f34c427 | call/alias-member | `urllib.request.urlopen` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/update_student_name.py:11` |
| op-32298086d25ce562 | call/alias-member | `uuid.uuid4` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/device_config.py:34` |
| op-7b2565870c239173 | call/alias-member | `uvicorn.Config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:68` |
| op-34498aa8e11b5f80 | call/alias-member | `uvicorn.Server` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:76` |
| op-63d10cabb6163e8e | call/alias-member | `warnings.filterwarnings` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:56` |
| op-6f5f680d869ced1a | call/alias-member | `webbrowser.open` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:242` |
| op-c03ac2985ccfb494 | call/alias-member | `websockets.connect` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/remote/scrcpy.py:371` |
| op-f5bead08eb8c8475 | call/alias-member | `win32api.GetKeyState` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:96` |
| op-22fa2539683290cc | call/alias-member | `win32api.GetModuleHandle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:122` |
| op-6fa1f37e570d0389 | call/alias-member | `win32com.client.Dispatch` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:225` |
| op-4039f35a53193fc4 | call/alias-member | `winreg.CloseKey` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/ldplayer_manager_api.py:15` |
| op-2b2d5658f8bf4f64 | call/alias-member | `winreg.OpenKey` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/bluestacks_module.py:13` |
| op-ce0ea37d81c63bc0 | call/alias-member | `winreg.QueryValueEx` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/bluestacks_module.py:14` |
| op-db6589b88b56ca03 | call/alias-member | `zipfile.ZipFile` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:226` |
| op-fd9311179cf3a96c | call/alias-member | `zipfile.is_zipfile` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:271` |
| op-7aea32c4c0754039 | call/chained | `dynamic:Await.decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/websocket_endpoint.py:21` |
| op-255f7788be5f736a | call/chained | `dynamic:Await.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:37` |
| op-3d4c459461d89ab2 | call/chained | `dynamic:BinOp.exists` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:688` |
| op-7d9b2f2ae488a8bc | call/chained | `dynamic:BinOp.read_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:63` |
| op-a79dd36558de8147 | call/chained | `dynamic:BinOp.read_text` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:209` |
| op-a465acd2738d6aa8 | call/chained | `dynamic:BinOp.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:261` |
| op-6dd73a555127307b | call/chained | `dynamic:BinOp.resolve` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:84` |
| op-9688469180a1181b | call/chained | `dynamic:BinOp.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:429` |
| op-401141e8b03a043f | call/chained | `dynamic:BinOp.sum` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/tactical_challenge_shop.py:27` |
| op-aa6abc475720711e | call/chained | `dynamic:BinOp.timestamp` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:52` |
| op-ec3510607e998531 | call/chained | `dynamic:BinOp.write_text` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/ocr/test_android_runtime.py:23` |
| op-b7e7e0d61ad73f93 | call/chained | `dynamic:BoolOp.copy` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:86` |
| op-11b69d5ebc785776 | call/chained | `dynamic:BoolOp.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:80` |
| op-d0a3ffb75d8ed55a | call/chained | `dynamic:Constant.format` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/bluestacks_module.py:31` |
| op-f6330d14cd10f347 | call/chained | `dynamic:Constant.join` | 22 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:373` |
| op-1d5b0aa6a247afa8 | call/chained | `dynamic:Constant.strip` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:263` |
| op-161eb32f472734f5 | call/chained | `dynamic:JoinedStr.encode` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:26` |
| op-6ed9dd4cadfb2442 | call/chained | `dynamic:JoinedStr.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:54` |
| op-8fb822e7f2c11a13 | call/chained | `dynamic:ListComp.index` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:541` |
| op-abc55f0b3a4f8563 | call/chained | `dynamic:call-result(ConfigInitializer)().check_config` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:701` |
| op-b10f76edd08899b3 | call/chained | `dynamic:call-result(PyQt5.QtCore.QUrl)().isValid` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:31` |
| op-4ac68a884b5a31b0 | call/chained | `dynamic:call-result(PyQt5.QtGui.QPixmap)().scaled` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:96` |
| op-89a370888b10e006 | call/chained | `dynamic:call-result(PyQt5.QtWidgets.QApplication.clipboard)().setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:163` |
| op-5a90c9b2b163017b | call/chained | `dynamic:call-result(PyQt5.QtWidgets.QApplication.desktop)().availableGeometry` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:504` |
| op-f2601d09ce7df6ef | call/chained | `dynamic:call-result(ScrcpyClient)().init` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:574` |
| op-a950bb38b561aa1e | call/chained | `dynamic:call-result(action.text)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:326` |
| op-be24705cdd8b312b | call/chained | `dynamic:call-result(archive.read)().decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:826` |
| op-f087846180d76243 | call/chained | `dynamic:call-result(asyncio.get_running_loop)().run_in_executor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:77` |
| op-4b17b596f4925516 | call/chained | `dynamic:call-result(base64.urlsafe_b64encode)().decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:22` |
| op-da94178779a11b37 | call/chained | `dynamic:call-result(builtins.list)().index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:88` |
| op-fb32da2aa4890d8a | call/chained | `dynamic:call-result(builtins.str)().encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:102` |
| op-4e061adf5ee40ec8 | call/chained | `dynamic:call-result(builtins.str)().lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:616` |
| op-c8cc518def9fb6fd | call/chained | `dynamic:call-result(builtins.str)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/ops.py:39` |
| op-1af2fc1fa9556636 | call/chained | `dynamic:call-result(builtins.str)().split` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:35` |
| op-dd52e818f8ac0de2 | call/chained | `dynamic:call-result(builtins.str)().strip` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/const.py:147` |
| op-a52c1f5f55ce95b3 | call/chained | `dynamic:call-result(builtins.str)().upper` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:12` |
| op-30822edc7cb17230 | call/chained | `dynamic:call-result(builtins.type)()` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:98` |
| op-2f6e88675c82a5b7 | call/chained | `dynamic:call-result(config.get_signal)().emit` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/notification.py:11` |
| op-003da53ad76b5de0 | call/chained | `dynamic:call-result(core.geometry.parallelogram.Parallelogram)().pixels` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:409` |
| op-c764414fafbf00af | call/chained | `dynamic:call-result(core.utils.Logger)().error` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:467` |
| op-3b2be42545bf1882 | call/chained | `dynamic:call-result(cryptography.hazmat.primitives.kdf.hkdf.HKDF)().derive` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:34` |
| op-67f00e7f7aa73121 | call/chained | `dynamic:call-result(ctypes.POINTER)()` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:308` |
| op-2fb57db09ce55515 | call/chained | `dynamic:call-result(d.getprop)().strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:80` |
| op-b47d5682622e6545 | call/chained | `dynamic:call-result(data.get)().get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:158` |
| op-83bd575ec84d20b4 | call/chained | `dynamic:call-result(datetime.datetime)().timestamp` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/restart.py:37` |
| op-2a9f6ec0fa78f93e | call/chained | `dynamic:call-result(datetime.datetime.fromtimestamp)().isoformat` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:33` |
| op-29428374142c3752 | call/chained | `dynamic:call-result(datetime.datetime.fromtimestamp)().strftime` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:80` |
| op-826eea13a36653be | call/chained | `dynamic:call-result(datetime.datetime.now)().isoformat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:121` |
| op-784a8779e2cb73fe | call/chained | `dynamic:call-result(datetime.datetime.now)().strftime` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:91` |
| op-1f641028a734fbcd | call/chained | `dynamic:call-result(datetime.datetime.now)().timestamp` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:66` |
| op-53a80993025f8188 | call/chained | `dynamic:call-result(datetime.datetime.strptime)().timestamp` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:283` |
| op-d584a8f5dbf0822a | call/chained | `dynamic:call-result(develop_tools.explore_task_data_generator.get_input)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:39` |
| op-80454f143a2417dc | call/chained | `dynamic:call-result(develop_tools.explore_task_data_generator.get_input)().split` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:42` |
| op-3912812d41085523 | call/chained | `dynamic:call-result(dynamic:Constant.join)().strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:781` |
| op-c5798b8f5fd5150e | call/chained | `dynamic:call-result(dynamic:call-result(builtins.str)().strip)().lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/const.py:147` |
| op-d4823b088b2339da | call/chained | `dynamic:call-result(dynamic:call-result(dynamic:call-result(dynamic:call-result(dynamic:call-result(self.parent)().parent)().parent)().parent)().parent)().parent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:238` |
| op-36820c7ac55d6016 | call/chained | `dynamic:call-result(dynamic:call-result(dynamic:call-result(dynamic:call-result(self.parent)().parent)().parent)().parent)().parent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:238` |
| op-7ea509b34f0807dc | call/chained | `dynamic:call-result(dynamic:call-result(dynamic:call-result(self.parent)().parent)().parent)().parent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:238` |
| op-0a9c711153ba0ec2 | call/chained | `dynamic:call-result(dynamic:call-result(dynamic:call-result(serial.replace)().replace)().replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:82` |
| op-fbd279975b6295df | call/chained | `dynamic:call-result(dynamic:call-result(dynamic:call-result(st.replace)().replace)().replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:44` |
| op-f8dfec8691b4f6cd | call/chained | `dynamic:call-result(dynamic:call-result(dynamic:subscript(dynamic:call-result(self.vBoxLayout.children)()).itemAt)().widget)().checkedChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:38` |
| op-bec44c6f6a096db6 | call/chained | `dynamic:call-result(dynamic:call-result(dynamic:subscript(widgets).text)().replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:200` |
| op-857f15561aa6abb4 | call/chained | `dynamic:call-result(dynamic:call-result(match.group)().lstrip)().rstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:46` |
| op-3c44ccaf7cdd52f2 | call/chained | `dynamic:call-result(dynamic:call-result(numpy.array)().flatten)().tolist` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:46` |
| op-af7be8c3567d46cf | call/chained | `dynamic:call-result(dynamic:call-result(os.getenv)().strip)().lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:34` |
| op-f732fe75deb5d1c6 | call/chained | `dynamic:call-result(dynamic:call-result(resp.json)().get)().get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:484` |
| op-8086ad7ce4b184e1 | call/chained | `dynamic:call-result(dynamic:call-result(self.__video_socket.recv)().decode)().rstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:134` |
| op-36c105a87beb1d53 | call/chained | `dynamic:call-result(dynamic:call-result(self.current_status)().get)().get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:462` |
| op-304fe20bab3cc939 | call/chained | `dynamic:call-result(dynamic:call-result(self.input.text)().replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:93` |
| op-534b05133678adfc | call/chained | `dynamic:call-result(dynamic:call-result(self.input_hard.text)().replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:113` |
| op-b76514ab216daa9a | call/chained | `dynamic:call-result(dynamic:call-result(self.parent)().parent)().parent` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:238` |
| op-32994b75500b0066 | call/chained | `dynamic:call-result(dynamic:call-result(serial.replace)().replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:82` |
| op-812359eae1de0ec4 | call/chained | `dynamic:call-result(dynamic:call-result(source.getparent)().getparent)().remove` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:90` |
| op-26a0c4fb0b97ed5c | call/chained | `dynamic:call-result(dynamic:call-result(st.replace)().replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:44` |
| op-f20e66a59408423b | call/chained | `dynamic:call-result(dynamic:subscript(dynamic:call-result(self.vBoxLayout.children)()).itemAt)().widget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:38` |
| op-399324ec33bf3427 | call/chained | `dynamic:call-result(dynamic:subscript(dynamic:subscript(dynamic:subscript(data))).replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/update_student_name.py:44` |
| op-01662a3abadf23e1 | call/chained | `dynamic:call-result(dynamic:subscript(image).mean)().mean` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/color.py:137` |
| op-2cce4ff03360080a | call/chained | `dynamic:call-result(dynamic:subscript(widgets).text)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:200` |
| op-d568070a951bb7cf | call/chained | `dynamic:call-result(entry.get)().upper` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:133` |
| op-8998e208d3de18dd | call/chained | `dynamic:call-result(filename.lower)().endswith` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:697` |
| op-a3e582282b0092e1 | call/chained | `dynamic:call-result(fp.read)().strip` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:137` |
| op-c28585be84f9343e | call/chained | `dynamic:call-result(fr.read)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/default_config.py:5173` |
| op-59967dc580b51495 | call/chained | `dynamic:call-result(gui.components.dialog_panel.CreateErrorInfoMessageBox)().exec_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:87` |
| op-7e7a3e4e6742fbff | call/chained | `dynamic:call-result(gui.util.config_gui.configGui.get)().value.name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:319` |
| op-47c0aaecfb4907ce | call/chained | `dynamic:call-result(hashlib.md5)().hexdigest` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:26` |
| op-102abaa039750e69 | call/chained | `dynamic:call-result(hashlib.sha256)().digest` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:111` |
| op-79225a202385685d | call/chained | `dynamic:call-result(hmac.new)().digest` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:43` |
| op-f1e284af98953280 | call/chained | `dynamic:call-result(java.jclass)().load` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:332` |
| op-0274d85670cd043e | call/chained | `dynamic:call-result(json.dumps)().encode` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:30` |
| op-5c828ab5dd51e9d7 | call/chained | `dynamic:call-result(json.dumps)().lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:135` |
| op-8666a361169bc507 | call/chained | `dynamic:call-result(label.style)().polish` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:385` |
| op-8ffbea802fa48630 | call/chained | `dynamic:call-result(level.strip)().upper` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:119` |
| op-abfcc8b5b95bfc97 | call/chained | `dynamic:call-result(line.lstrip)().rstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:63` |
| op-e02582d326f70e11 | call/chained | `dynamic:call-result(line.strip)().split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:270` |
| op-c903492c7df28fc0 | call/chained | `dynamic:call-result(logging.getLogger)().critical` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:219` |
| op-ffda587f7d8073d8 | call/chained | `dynamic:call-result(logging.getLogger)().debug` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:72` |
| op-9bb7eb3fe185ea2c | call/chained | `dynamic:call-result(logging.getLogger)().error` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:100` |
| op-d7b83bfda159016b | call/chained | `dynamic:call-result(logging.getLogger)().exception` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:124` |
| op-4422040c3d1a2e1f | call/chained | `dynamic:call-result(logging.getLogger)().info` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:58` |
| op-1dc38bef75ee14a5 | call/chained | `dynamic:call-result(logging.getLogger)().setLevel` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:49` |
| op-6d60b191ebe504c3 | call/chained | `dynamic:call-result(logging.getLogger)().warning` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:139` |
| op-c4e25ac0e6a4857c | call/chained | `dynamic:call-result(match.group)().lstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:46` |
| op-3572eb175bb5661b | call/chained | `dynamic:call-result(message.replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:186` |
| op-4d35e6ca1147bafd | call/chained | `dynamic:call-result(numpy.abs)().mean` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/color.py:156` |
| op-d98ef83aa353aa31 | call/chained | `dynamic:call-result(numpy.array)().flatten` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:46` |
| op-f7f6757bd24c7adb | call/chained | `dynamic:call-result(numpy.ctypeslib.as_array)().reshape` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:344` |
| op-edea1f6550b5efcf | call/chained | `dynamic:call-result(os.getenv)().lower` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:49` |
| op-d1e0925b214a5251 | call/chained | `dynamic:call-result(os.getenv)().split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:36` |
| op-f7c55d7173f6696e | call/chained | `dynamic:call-result(os.getenv)().strip` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:114` |
| op-7f2014fd9946f321 | call/chained | `dynamic:call-result(os.path.dirname)().strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/mumu_manager_api.py:28` |
| op-e63204778ae3ffa3 | call/chained | `dynamic:call-result(output.strip)().split` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_start_command.py:21` |
| op-22ca56d2662e7053 | call/chained | `dynamic:call-result(part.strip)().lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:214` |
| op-4856b9b7414ccbf0 | call/chained | `dynamic:call-result(path.relative_to)().as_posix` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:786` |
| op-c204bcfcd97bf291 | call/chained | `dynamic:call-result(pathlib.Path)().as_posix` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/utils.py:10` |
| op-b8b816b2322db7b7 | call/chained | `dynamic:call-result(pathlib.Path)().exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:123` |
| op-3b15016ad26d1b05 | call/chained | `dynamic:call-result(pathlib.Path)().expanduser` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:239` |
| op-ff541179fc497e0a | call/chained | `dynamic:call-result(pathlib.Path)().is_absolute` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:803` |
| op-cc59938627611a3a | call/chained | `dynamic:call-result(pathlib.Path)().resolve` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:234` |
| op-b2a1441bd24a4bfd | call/chained | `dynamic:call-result(platform.machine)().lower` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:51` |
| op-d8e943799accd0d1 | call/chained | `dynamic:call-result(platform.system)().lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/utils.py:15` |
| op-1645db3c497b6164 | call/chained | `dynamic:call-result(point.astype)().tolist` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/nemu.py:62` |
| op-0f754055ecf7ae35 | call/chained | `dynamic:call-result(psutil.Process)().terminate` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1092` |
| op-b4e82e94db36aa05 | call/chained | `dynamic:call-result(pyautogui.screenshot)().crop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/screenshot/pyautogui.py:13` |
| op-ca4279f14cdd3f47 | call/chained | `dynamic:call-result(qfluentwidgets.InfoBar)().show` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/notification.py:38` |
| op-3949fe7f2221007d | call/chained | `dynamic:call-result(qfluentwidgets.MessageBox)().show` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:434` |
| op-8f85592b1fdb4589 | call/chained | `dynamic:call-result(qq_combo.itemText)().split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:162` |
| op-b00a4ef0185b81d7 | call/chained | `dynamic:call-result(query.strip)().lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:120` |
| op-d2fbb9191b7646c0 | call/chained | `dynamic:call-result(requests.get)().text.strip` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:233` |
| op-0100ec9809e61c19 | call/chained | `dynamic:call-result(resp.json)().get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:484` |
| op-05d91aca53727e5c | call/chained | `dynamic:call-result(response.read)().decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/update_student_name.py:12` |
| op-4bc3206105c81471 | call/chained | `dynamic:call-result(response_json.get)().get` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:123` |
| op-e7fb41a19ee36a06 | call/chained | `dynamic:call-result(s.recv)().decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:170` |
| op-02a21179f8c28ef0 | call/chained | `dynamic:call-result(segment.replace)().replace` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:12` |
| op-8bb59c1eb2518624 | call/chained | `dynamic:call-result(self.__shell)().strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:198` |
| op-a2e7158d018da12a | call/chained | `dynamic:call-result(self.__video_socket.recv)().decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:134` |
| op-17e75b097c430b96 | call/chained | `dynamic:call-result(self._device.shell)().strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:158` |
| op-df6270b9d0a484de | call/chained | `dynamic:call-result(self._mirrorc_cdk_TextEdit.toPlainText)().strip` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:389` |
| op-cc530a82331486af | call/chained | `dynamic:call-result(self._signing_key.public_key)().public_bytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:62` |
| op-eeb7974217ef9b19 | call/chained | `dynamic:call-result(self.banner.size)().width` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:216` |
| op-6738dc7153706142 | call/chained | `dynamic:call-result(self.config.get)().get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:464` |
| op-78c1d14e99612f4b | call/chained | `dynamic:call-result(self.config.get_main_thread)().start_fhx` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/otherConfig.py:27` |
| op-11263c20c6b4419c | call/chained | `dynamic:call-result(self.config.get_main_thread)().start_hard_task` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/exploreConfig.py:48` |
| op-7aba0347b18615b1 | call/chained | `dynamic:call-result(self.config.get_main_thread)().start_normal_task` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/exploreConfig.py:55` |
| op-fb70d09428cf8667 | call/chained | `dynamic:call-result(self.config.get_signal)().connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:79` |
| op-dc531020cd25b318 | call/chained | `dynamic:call-result(self.config.get_signal)().emit` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/exploreConfig.py:47` |
| op-e94f540d9947dd92 | call/chained | `dynamic:call-result(self.current_status)().get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:462` |
| op-75ce741e718224f6 | call/chained | `dynamic:call-result(self.document)().setDocumentMargin` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:26` |
| op-10ba6b89c58cec7b | call/chained | `dynamic:call-result(self.file_path.lower)().rfind` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:238` |
| op-56c384a3352fd1db | call/chained | `dynamic:call-result(self.horizontalHeader)().hide` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:640` |
| op-3a9045457677509e | call/chained | `dynamic:call-result(self.horizontalHeader)().setSectionResizeMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:650` |
| op-3461b4278d84d036 | call/chained | `dynamic:call-result(self.horizontalHeader)().show` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:651` |
| op-9fc4e57b2203e0f1 | call/chained | `dynamic:call-result(self.input.text)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:93` |
| op-611533975264dab8 | call/chained | `dynamic:call-result(self.input_hard.text)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:113` |
| op-daf6e79e5a6b5e04 | call/chained | `dynamic:call-result(self.lesson_favorStudent_LineEdit.text)().split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:108` |
| op-69455a76e6d9173e | call/chained | `dynamic:call-result(self.parent)().parent` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:238` |
| op-aba40675f736cea3 | call/chained | `dynamic:call-result(self.rect)().translated` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:173` |
| op-c6cf82b609ecd56b | call/chained | `dynamic:call-result(self.sender)().currentIndex` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:118` |
| op-ff3778276af283ff | call/chained | `dynamic:call-result(self.sender)().text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:99` |
| op-3d302724c457450a | call/chained | `dynamic:call-result(self.size)().isValid` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:824` |
| op-ec43c45141c91928 | call/chained | `dynamic:call-result(self.stackedWidget.currentWidget)().objectName` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:545` |
| op-0e4c9e41991a71ab | call/chained | `dynamic:call-result(self.tabBar.currentTab)().routeKey` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:540` |
| op-8297a5d56d6bc009 | call/chained | `dynamic:call-result(self.tabBar.tabRegion)().contains` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:386` |
| op-18d1c610249b7024 | call/chained | `dynamic:call-result(self.tableView.horizontalHeader)().setSectionResizeMode` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:109` |
| op-83ea81787a25c160 | call/chained | `dynamic:call-result(self.table_view.horizontalHeader)().setSectionResizeMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:105` |
| op-ad82b725585f0551 | call/chained | `dynamic:call-result(self.table_view.verticalHeader)().setVisible` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:107` |
| op-47430cd617622685 | call/chained | `dynamic:call-result(self.test_result_table.item)().data` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:504` |
| op-0559986979c46f8b | call/chained | `dynamic:call-result(self.test_result_table.item)().setData` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:487` |
| op-e872c9f77a7402cb | call/chained | `dynamic:call-result(self.test_result_table.item)().setForeground` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:495` |
| op-a201dbdf47e9995c | call/chained | `dynamic:call-result(self.test_result_table.item)().setText` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:494` |
| op-98e609cc0ad830db | call/chained | `dynamic:call-result(self.test_result_table.item)().setToolTip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:521` |
| op-b58b05c597515a65 | call/chained | `dynamic:call-result(self.tr)().format` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:540` |
| op-c61fa662bf953e71 | call/chained | `dynamic:call-result(self.u2)().pinch_in` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:128` |
| op-e68c58cabdca2784 | call/chained | `dynamic:call-result(self.u2._adb_device.shell)().strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:372` |
| op-df84da20074c043d | call/chained | `dynamic:call-result(self.u2.http.get)().json` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1108` |
| op-c851077e54066d9a | call/chained | `dynamic:call-result(self.verticalHeader)().hide` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:639` |
| op-e791d4e9a10a61f2 | call/chained | `dynamic:call-result(self.verticalHeader)().setSectionResizeMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:644` |
| op-4d099c04f53a5880 | call/chained | `dynamic:call-result(self.verticalHeader)().show` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:645` |
| op-7faf49f6fa1c0138 | call/chained | `dynamic:call-result(self.verticalScrollBar)().setValue` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:86` |
| op-6a69bc071fc60e2b | call/chained | `dynamic:call-result(self.verticalScrollBar)().value` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:81` |
| op-3b9475240817b0e5 | call/chained | `dynamic:call-result(self.viewport)().setStyleSheet` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:74` |
| op-2a01dcebb86b80ab | call/chained | `dynamic:call-result(self.viewport)().width` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:48` |
| op-026962b73b6cae2a | call/chained | `dynamic:call-result(serial.replace)().replace` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:82` |
| op-a72cfb869f729669 | call/chained | `dynamic:call-result(server_private.public_key)().public_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:81` |
| op-85a41e8c1e3402d6 | call/chained | `dynamic:call-result(service.channels.ProviderChannelHandler)().handle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_provider.py:45` |
| op-607ad2a69f4f39b1 | call/chained | `dynamic:call-result(service.channels.RemoteChannelHandler)().handle` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_remote.py:23` |
| op-4629a6cda3b58313 | call/chained | `dynamic:call-result(service.channels.SyncChannelHandler)()._handle_message` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_sync.py:41` |
| op-84b6aeabe382d953 | call/chained | `dynamic:call-result(service.channels.SyncChannelHandler)().handle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_sync.py:57` |
| op-a149739203e5c6eb | call/chained | `dynamic:call-result(service.channels.TriggerChannelHandler)().handle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_trigger.py:24` |
| op-6c1ed92d97ce18b9 | call/chained | `dynamic:call-result(service.conf.initializer.ConfigInitializer)().check_and_update_user_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/ops.py:31` |
| op-a39d3e7e82ceeae0 | call/chained | `dynamic:call-result(service.conf.initializer.ConfigInitializer)().check_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/ops.py:35` |
| op-2f9865e18c1e69a1 | call/chained | `dynamic:call-result(service.conf.initializer.ConfigInitializer)().check_event_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/ops.py:15` |
| op-5e2f5c8b0ba253ff | call/chained | `dynamic:call-result(service.conf.initializer.ConfigInitializer)().check_static_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/ops.py:23` |
| op-a290f0c06524fcf5 | call/chained | `dynamic:call-result(service.conf.initializer.ConfigInitializer)().check_switch_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/ops.py:7` |
| op-4113fb780d533672 | call/chained | `dynamic:call-result(service.conf.initializer.ConfigInitializer)().delete_deprecated_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/ops.py:19` |
| op-99cf9872453b51d8 | call/chained | `dynamic:call-result(source.getparent)().find` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:84` |
| op-7da3c3a77215278c | call/chained | `dynamic:call-result(source.getparent)().getparent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:90` |
| op-fa8675981cb764c3 | call/chained | `dynamic:call-result(st.replace)().replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:44` |
| op-c27aaddc409547a5 | call/chained | `dynamic:call-result(tableView.horizontalHeader)().setSectionResizeMode` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:70` |
| op-892d22f4cc82ff54 | call/chained | `dynamic:call-result(tests.core.device.test_uiautomator2_client.make_initer)()._install_uiautomator_apks` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/device/test_uiautomator2_client.py:36` |
| op-cd43d51bce4bace2 | call/chained | `dynamic:call-result(text)().split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:309` |
| op-c42d86abc1e80dbb | call/chained | `dynamic:call-result(threading.Thread)().start` | 13 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:36` |
| op-25314a6c2195e14d | call/chained | `dynamic:call-result(timestamp.astimezone)().isoformat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:119` |
| op-df0e64bbf6e2b037 | call/chained | `dynamic:call-result(uiautomator2.connect)().shell` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:108` |
| op-8acf14c9af7dd5fa | call/chained | `dynamic:call-result(value.lstrip)().rstrip` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:42` |
| op-7f170e8f725aebfe | call/chained | `dynamic:call-result(value.strip)().lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:17` |
| op-256d4de34c8a6c69 | call/chained | `dynamic:call-result(x.text)().split` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:89` |
| op-dbfd506b97fb7eb3 | call/chained | `dynamic:subscript(args).parent.control_socket.send` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:23` |
| op-dd36e3c8fe98b9b9 | call/chained | `dynamic:subscript(button_detected).any` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:322` |
| op-f56d269c04c1502f | call/chained | `dynamic:subscript(character_dict).setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:155` |
| op-cff79e9bf25a2d71 | call/chained | `dynamic:subscript(cmdline_dict).append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:87` |
| op-8a77bf7c6f8a007e | call/chained | `dynamic:subscript(core.ipc_manager.SharedMemory.shm_map)._release` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ipc_manager.py:33` |
| op-65d90da6eef6999b | call/chained | `dynamic:subscript(css).isspace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:303` |
| op-0a0c5a70daec77b6 | call/chained | `dynamic:subscript(current).get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:185` |
| op-ef21924d77933727 | call/chained | `dynamic:subscript(data).get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:469` |
| op-e453ce232dd45b49 | call/chained | `dynamic:subscript(data).replace` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:272` |
| op-c30777b2c77809a0 | call/chained | `dynamic:subscript(detected_student_pos).add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:541` |
| op-e46a7ce2bdf0dbeb | call/chained | `dynamic:subscript(detected_student_pos).remove` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:587` |
| op-6997ffcb9af3c695 | call/chained | `dynamic:subscript(dynamic:call-result(dynamic:subscript(dynamic:call-result(tail.split)()).split)()).strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:244` |
| op-48542887cfea6d38 | call/chained | `dynamic:subscript(dynamic:call-result(mostPossibleMaterial.split)()).split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:95` |
| op-92c2fcf59594681f | call/chained | `dynamic:subscript(dynamic:call-result(numpy.array)()).tolist` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/nemu.py:73` |
| op-0bad3473b310c2ff | call/chained | `dynamic:subscript(dynamic:call-result(self.vBoxLayout.children)()).itemAt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:38` |
| op-9cced3116cf5bd8b | call/chained | `dynamic:subscript(dynamic:call-result(tail.split)()).split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:244` |
| op-4e44b901c76f9d80 | call/chained | `dynamic:subscript(dynamic:subscript(data)).startswith` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/taskDataFixer.py:27` |
| op-d9d7a571373f4f2a | call/chained | `dynamic:subscript(dynamic:subscript(dynamic:subscript(data))).replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/update_student_name.py:44` |
| op-339e996c816712b2 | call/chained | `dynamic:subscript(dynamic:subscript(dynamic:subscript(dynamic:call-result(response.json)()))).endswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:141` |
| op-27fccfcb59af8ab2 | call/chained | `dynamic:subscript(dynamic:subscript(image_x_y_range)).update` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:47` |
| op-b527aefce17dc183 | call/chained | `dynamic:subscript(dynamic:subscript(ocr_res)).lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:325` |
| op-972ab68256d838c9 | call/chained | `dynamic:subscript(dynamic:subscript(self.check_box_for_lesson_levels)).isChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:118` |
| op-a6c62437ff9b6f6a | call/chained | `dynamic:subscript(dynamic:subscript(self.check_box_for_lesson_levels)).toggled.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:140` |
| op-6c1217a2fd63f927 | call/chained | `dynamic:subscript(dynamic:subscript(self.config.static_config.hard_task_student_material)).split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:35` |
| op-5cc000ef1096b559 | call/chained | `dynamic:subscript(gui.components.expand.__dict__).Layout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:85` |
| op-62963a346344a9cc | call/chained | `dynamic:subscript(host_value).split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:43` |
| op-4815cbea2834f2b0 | call/chained | `dynamic:subscript(image).mean` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/color.py:137` |
| op-b53080a627675c6b | call/chained | `dynamic:subscript(info).isdigit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:34` |
| op-2407a7e83caab8fb | call/chained | `dynamic:subscript(message).encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:484` |
| op-92e6c25f7c262cff | call/chained | `dynamic:subscript(name).isdigit` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:77` |
| op-8a41738bd9896f3e | call/chained | `dynamic:subscript(name).lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:535` |
| op-54c63c2a39fdeb1c | call/chained | `dynamic:subscript(ocr_res).isdigit` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:813` |
| op-9f84119d8eff8465 | call/chained | `dynamic:subscript(ocr_res).lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:287` |
| op-6634db550f36620d | call/chained | `dynamic:subscript(one_action).append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:141` |
| op-08246cb51afd067a | call/chained | `dynamic:subscript(parts).isdigit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:271` |
| op-ff1371ff25c365cf | call/chained | `dynamic:subscript(possible_string_letter_dict).keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:184` |
| op-fcceefac1e2b7edc | call/chained | `dynamic:subscript(process).lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:37` |
| op-fadcdf46e030d8f7 | call/chained | `dynamic:subscript(process.info).lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:11` |
| op-f6c9ccc91cde061e | call/chained | `dynamic:subscript(repo.remotes).fetch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:538` |
| op-1bc9e564f84617e3 | call/chained | `dynamic:subscript(request.handlers).handle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:14` |
| op-437deb6a6b818a05 | call/chained | `dynamic:subscript(res).__str__` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:56` |
| op-f8d72bbde5e55307 | call/chained | `dynamic:subscript(res).isdigit` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:27` |
| op-fd0e5127ab45af83 | call/chained | `dynamic:subscript(self._crt_order_config).update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:267` |
| op-b6eedecf3e7228b8 | call/chained | `dynamic:subscript(self._event_config).update` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:234` |
| op-1ac21886c5f57341 | call/chained | `dynamic:subscript(self._history_per_scope).append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:113` |
| op-3dc0ac4943b9673a | call/chained | `dynamic:subscript(self._sessions).baas.scheduler.event_map.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:956` |
| op-ff287621faee9de1 | call/chained | `dynamic:subscript(self._sub_list).append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:596` |
| op-21a012b191dd2f01 | call/chained | `dynamic:subscript(self.bar_ref).__exit__` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:770` |
| op-5c2fb8d70459e7ad | call/chained | `dynamic:subscript(self.boxes).isChecked` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:54` |
| op-d419ee0b12f2e4cc | call/chained | `dynamic:subscript(self.boxes).setChecked` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:54` |
| op-db13f35721988a7c | call/chained | `dynamic:subscript(self.check_box_for_lesson_levels).append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:85` |
| op-f535038677ee696e | call/chained | `dynamic:subscript(self.check_boxes).blockSignals` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:313` |
| op-7443ce43c84f804a | call/chained | `dynamic:subscript(self.check_boxes).isChecked` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:230` |
| op-2be457ec7211b806 | call/chained | `dynamic:subscript(self.check_boxes).setChecked` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:314` |
| op-09c4787566e4d6a6 | call/chained | `dynamic:subscript(self.each_student_task_number_dict).append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:41` |
| op-58e0785e6998432c | call/chained | `dynamic:subscript(self.handlers).handle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:60` |
| op-8996ff19df6b9a91 | call/chained | `dynamic:subscript(self.lesson_time_input).setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:91` |
| op-d148d9ca7d4fab9a | call/chained | `dynamic:subscript(self.lesson_time_input).setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:90` |
| op-f337c35cad7dd0ec | call/chained | `dynamic:subscript(self.lesson_time_input).setValidator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:89` |
| op-9a6d05fd3f0aa4e0 | call/chained | `dynamic:subscript(self.lesson_time_input).text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:128` |
| op-309693233ab31571 | call/chained | `dynamic:subscript(self.lesson_time_input).textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:141` |
| op-1b025a864a53b98b | call/chained | `dynamic:subscript(self.listeners).append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:276` |
| op-6048c3156e08f38f | call/chained | `dynamic:subscript(self.listeners).remove` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:286` |
| op-f9fe62308c95c741 | call/chained | `dynamic:subscript(self.patch_t_dict).setText` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:496` |
| op-72b05aa362d9c4d7 | call/chained | `dynamic:subscript(self.patch_v_dict).setText` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:494` |
| op-4477a9d1e23e8dca | call/chained | `dynamic:subscript(self.qLabels).text` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:228` |
| op-785f61ac149f54ab | call/chained | `dynamic:subscript(self.static_config.lesson_region_name).copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:21` |
| op-e0d1bde42f96fe2b | call/chained | `dynamic:subscript(self.static_config.main_story_available_episodes).copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:173` |
| op-9b3eb93a3ab755d5 | call/chained | `dynamic:subscript(self.times).blockSignals` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:274` |
| op-0e31a88eb6a88e0b | call/chained | `dynamic:subscript(self.times).setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:275` |
| op-edb12301fece15a4 | call/chained | `dynamic:subscript(self.times).text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:229` |
| op-de79f9300e0958d8 | call/chained | `dynamic:subscript(self.total_assault_difficulty_names).lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:65` |
| op-3881747b127384d3 | call/chained | `dynamic:subscript(self.total_assault_difficulty_names).upper` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:339` |
| op-a1d74cea4db0c92c | call/chained | `dynamic:subscript(setup_toml).get` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:502` |
| op-2314cc8dce0d8d8f | call/chained | `dynamic:subscript(string_letter_dict).setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:156` |
| op-9b33899752c54d67 | call/chained | `dynamic:subscript(task).items` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:43` |
| op-894ddc740c0c27b0 | call/chained | `dynamic:subscript(task).startswith` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/taskDataFixer.py:17` |
| op-7df9bdda0ed544ad | call/chained | `dynamic:subscript(tasks).split` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:271` |
| op-28b49cd1395d38a1 | call/chained | `dynamic:subscript(team_config).append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:168` |
| op-c8e9aca55bc37440 | call/chained | `dynamic:subscript(temp).isdigit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:103` |
| op-b19dbb9f1f6ad8b8 | call/chained | `dynamic:subscript(times).split` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:556` |
| op-7429c32d0392059e | call/chained | `dynamic:subscript(widgets).setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:207` |
| op-fcaa0098169ee360 | call/chained | `dynamic:subscript(widgets).text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:200` |
| op-10e2094bfb1ca2c3 | call/dynamic | `dynamic:BinOp` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:333` |
| op-be468dee2bb4e9d4 | call/dynamic | `dynamic:getattr(self.component,?)` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:51` |
| op-3476e0afc77d6da8 | call/dynamic | `dynamic:subscript(client.callbacks)` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_remote_proxy.py:44` |
| op-d50e51bb1868511e | call/dynamic | `dynamic:subscript(func)` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:711` |
| op-9dbeab510403d227 | call/dynamic | `dynamic:subscript(func_dict)` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:538` |
| op-c7f5c4b3802ea83f | call/dynamic | `dynamic:subscript(gui.util.notification.__dict__)` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:455` |
| op-54b5d6f51ceaa191 | call/dynamic | `dynamic:subscript(self.bar_ref)` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:766` |
| op-5c5f96bc1191c347 | call/member | `Baas_instance.get_config` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Control.py:16` |
| op-e12d12ef9fca836b | call/member | `Baas_instance.get_logger` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Control.py:18` |
| op-030bd267ccf3fb79 | call/member | `CHANNEL_REPO_SHA_METHODS.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/const.py:163` |
| op-a160bdff207fef6e | call/member | `DrillConfig.tr` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:15` |
| op-4335cecdbd6c0b73 | call/member | `EmulatorConfig.tr` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:17` |
| op-fe4f3e4b6bbbb908 | call/member | `EventMapConfig.tr` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:18` |
| op-d76d3d46b66c5f6a | call/member | `ExploreConfig.tr` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/exploreConfig.py:13` |
| op-f8bb1df36f9c5e2b | call/member | `G.runtime_path.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:284` |
| op-30c3c2bc3d8daa5c | call/member | `HEADER.pack` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/framing.py:20` |
| op-27f74372df8d5479 | call/member | `HEADER.unpack_from` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/framing.py:36` |
| op-55b93ca32b14881e | call/member | `LEVEL_MAP.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:122` |
| op-e9df7ca67eb72ad2 | call/member | `NOISY_DEPENDENCY_LOG_LEVELS.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:55` |
| op-e63a8cff4856db79 | call/member | `OCR_SERVER_PREBUILD_API_URL.format` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:155` |
| op-6b6ac7a2ef071b9f | call/member | `OtherConfig.tr` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/otherConfig.py:11` |
| op-3b13a78a0ffde5ec | call/member | `ProceedPlot.tr` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/proceedPlot.py:10` |
| op-ce1d8369697842ff | call/member | `PushConfig.tr` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/pushConfig.py:9` |
| op-518b6e91a7dc052c | call/member | `RenameDialogContext.tr` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:206` |
| op-e737d75c6665f676 | call/member | `ServerConfig.tr` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:23` |
| op-9ba1c23d782819bd | call/member | `SweepCountConfig.tr` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/sweepCountConfig.py:9` |
| op-003bdc260d373f9e | call/member | `TARGET_BRANCH.startswith` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:71` |
| op-3cc52f2efb2b898a | call/member | `VLayout.addWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:38` |
| op-c69ab31c0e4331ee | call/member | `_HANDLERS.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:75` |
| op-7268e1bd41529021 | call/member | `_KEY_MAP.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:176` |
| op-641dc685a191d256 | call/member | `_MODIFIER_MAP.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:161` |
| op-c351bcf9bb24255d | call/member | `__dict__for_scheduler_selector.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:39` |
| op-bdd9dfe446eb231e | call/member | `__dict_for_create_method.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:242` |
| op-ae9eb0a2aaace712 | call/member | `__env__.copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:964` |
| op-5b9d290ee755bd3f | call/member | `_all_label_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:213` |
| op-4afd983efed04f4e | call/member | `_changed_row.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:613` |
| op-48c76d87275f593c | call/member | `_changed_table.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:614` |
| op-0e50b9a54f1308a8 | call/member | `_config.add_signal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:587` |
| op-f2613844fec9db11 | call/member | `_config.set_window` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:588` |
| op-4ecaee8a3dc98207 | call/member | `_configItems.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:56` |
| op-9db2f8d6cadef6ea | call/member | `_event_map_inv.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:960` |
| op-aac35d3229ae9d2e | call/member | `_f.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:533` |
| op-372ed27758bc4303 | call/member | `_f.write` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:527` |
| op-2ff6d99b06b10473 | call/member | `_followed_cmd.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:362` |
| op-4b58639d46ba7813 | call/member | `_layout.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:48` |
| op-25c82fb410ccd8b7 | call/member | `_layout.addWidget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:47` |
| op-7f4e3ccd5ee088b2 | call/member | `_lines.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/exception.py:71` |
| op-a31d7b63aeb31472 | call/member | `_list.insert` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:269` |
| op-bf4a9a57918c6efc | call/member | `_logger.debug` | 18 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/api/security.py:128` |
| op-8ae33daaaf687458 | call/member | `_logger.exception` | 6 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/api/ws_control.py:79` |
| op-b2b83568fbd729a6 | call/member | `_logger.info` | 10 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/api/ws_control.py:44` |
| op-ca436fcdd628e90a | call/member | `_logger.warning` | 5 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/api/ws_control.py:66` |
| op-3504f7b8aa910e02 | call/member | `_main_thread_._init_script` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:242` |
| op-ddb9a76ca37e026c | call/member | `_main_thread_.get_baas_thread` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:238` |
| op-5fdbab81c22f33de | call/member | `_repo.lookup_branch` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:866` |
| op-3818009e94681e62 | call/member | `_repo.references.delete` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:877` |
| op-194d1aa8f1dbe708 | call/member | `_repo.remotes.create` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:874` |
| op-0040f98a3a1afff8 | call/member | `_repo.remotes.delete` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:873` |
| op-c4c610037446fa86 | call/member | `_res.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:262` |
| op-576b0c760692ca07 | call/member | `_scheduler_selector_label.installEventFilter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:32` |
| op-b13b2ec29f43fd07 | call/member | `_scheduler_selector_label.setToolTip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:31` |
| op-71c8ff1ff882dc8c | call/member | `_scheduler_selector_layout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:50` |
| op-3775433a1987a741 | call/member | `_value.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:191` |
| op-e26461816b8e6d97 | call/member | `a0.globalPos` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:254` |
| op-dcaff8f27ca8c1db | call/member | `a1.type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:788` |
| op-f8059a0a5a860aa3 | call/member | `acc.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:191` |
| op-0328ac004f608a9c | call/member | `acc.index` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:194` |
| op-fcb049d984e359f7 | call/member | `action.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:144` |
| op-c5cb1eab5d48c6cc | call/member | `action.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:326` |
| op-3c2ae2ab2b899ade | call/member | `action.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:326` |
| op-680032736ed7621d | call/member | `activity_formation.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:44` |
| op-c4afd0fcc69f8097 | call/member | `adb_addresses.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:106` |
| op-bce4d32c2d0000fe | call/member | `addDialog.exec_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:564` |
| op-1390f09b8ef166c5 | call/member | `addDialog.pathLineEdit.setFocus` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:563` |
| op-1fa6f27f28aa9698 | call/member | `addDialog.pathLineEdit.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:565` |
| op-bf3b6d7eb388cd66 | call/member | `addr.find` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:88` |
| op-a82be3db8ecdcedb | call/member | `agent_version.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:165` |
| op-f90e75c71fce1f88 | call/member | `all_sort_p.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:62` |
| op-58884c6c0894fe00 | call/member | `all_stage.remove` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:382` |
| op-b15b887df45e4fd7 | call/member | `all_strs.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/image.py:241` |
| op-1491496644e83f73 | call/member | `apk.write_bytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/device/test_uiautomator2_client.py:20` |
| op-2bf8a712584553a1 | call/member | `app.add_middleware` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:67` |
| op-2a84eed4aad9015b | call/member | `app.exec_` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:59` |
| op-da869fd096a7fcdd | call/member | `app.include_router` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:86` |
| op-9a6dd34e99eda5fe | call/member | `app.installTranslator` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:664` |
| op-f18b5d761aace921 | call/member | `app.middleware` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:40` |
| op-9dfe6d4bb440ec5c | call/member | `app_window.get_resolution` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1013` |
| op-b1dc90a85d5fd84d | call/member | `app_window.is_fullscreen_or_maximized` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1012` |
| op-dbe2fc7298916891 | call/member | `app_window.resize_client_area` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1022` |
| op-3ae08da45290264a | call/member | `appeared_episodes.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:215` |
| op-c9b43750723b33aa | call/member | `appeared_names.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:199` |
| op-bed0b582df4f87a8 | call/member | `arch_aliases.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/utils.py:45` |
| op-a918587d5bb7e100 | call/member | `archive.extractall` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:227` |
| op-c1bbacf5b581c20a | call/member | `archive.infolist` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:798` |
| op-aea606cb1e76e324 | call/member | `archive.read` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:826` |
| op-57c16bc3368cfb6c | call/member | `archive.write` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:786` |
| op-479a1635e632d3ac | call/member | `archive.writestr` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:224` |
| op-4f5386cbf99b0e81 | call/member | `archive_path.open` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:688` |
| op-0cf702151fdb34c3 | call/member | `archive_source.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:669` |
| op-0dd536a85e9ea36e | call/member | `areas.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:207` |
| op-262fa0591f291fc3 | call/member | `arg.decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:32` |
| op-551f30ee9364e32b | call/member | `arg.encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:29` |
| op-da7c657a62277f76 | call/member | `arg.strip` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_start_command.py:22` |
| op-19b9217499d06e2a | call/member | `arguments.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/process_api.py:30` |
| op-3f7c7d26c65f24d3 | call/member | `author.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:63` |
| op-41b002ddb7e1ef48 | call/member | `available.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:122` |
| op-20d0ec917eba5ead | call/member | `available_packages.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:263` |
| op-9408f417a483e9a8 | call/member | `bThread.init_all_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:123` |
| op-86dc49e1d0c8f7d6 | call/member | `bThread.set_ocr` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:122` |
| op-22d189b1db329c79 | call/member | `bThread.solve` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:141` |
| op-3aac0f7fcccc5c25 | call/member | `baas.init_all_data` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:55` |
| op-87da3af4d14dd274 | call/member | `baas.logger.info` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:13` |
| op-a7f3b15020326c12 | call/member | `baas.send` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:525` |
| op-0a44a88698e5a450 | call/member | `baas.set_ocr` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:54` |
| op-8db73628b30fd402 | call/member | `baas.solve` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:14` |
| op-9366b4f0883b100b | call/member | `baas.thread_starter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service.example.py:17` |
| op-edc02dee5bc6a554 | call/member | `bar_gen.__enter__` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:762` |
| op-0e8f0f7275500062 | call/member | `base.resolve` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:85` |
| op-ef7c1f54726a8cf4 | call/member | `base_messages.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:437` |
| op-8a95d6eb7e38db94 | call/member | `block_detect_student.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:525` |
| op-a51bb4a36cbfa417 | call/member | `block_detect_student.sort` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:532` |
| op-9481a4f819abe87b | call/member | `block_existing_names.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:526` |
| op-80d4d1ff02ccaca5 | call/member | `body.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:399` |
| op-281b0cf1afe4f3eb | call/member | `bthread.init_all_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test_explore_task_data.py:8` |
| op-0f1c1f38606caf0c | call/member | `btn.clicked.connect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:721` |
| op-f632cd05cdeac5e8 | call/member | `btn.disconnect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:482` |
| op-cf1820c65002ae6c | call/member | `btn.setSelected` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:535` |
| op-640f39f24752d0f1 | call/member | `btn.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:720` |
| op-bebf0d0aae4468d4 | call/member | `btnConfirm.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:141` |
| op-6338d78aedff2c89 | call/member | `buffer.getvalue` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:789` |
| op-3d71cafcb36d78fe | call/member | `builtins.dict.fromkeys` | 2 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/emulator_manager/auto_scan_simulator.py:108` |
| op-ccc1d0f30b49bee0 | call/member | `buy_list.any` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:8` |
| op-6dd20de9e1ff81ef | call/member | `buy_list.sum` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:92` |
| op-e31b290a7d2ac8ca | call/member | `callbacks.spinner.stop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:778` |
| op-f707d7cfc2132c1d | call/member | `calls.append` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/device/test_uiautomator2_client.py:31` |
| op-77a26238e25a2088 | call/member | `candidate.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:127` |
| op-fbbc551a3de7be91 | call/member | `candidate.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:565` |
| op-83b77118e4bd8680 | call/member | `candidate.open` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:130` |
| op-c6f3a844034d9ce8 | call/member | `candidate.stat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:180` |
| op-c931686517f26400 | call/member | `candidate_result.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:569` |
| op-8beac43bd8251409 | call/member | `candidates.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:614` |
| op-922c64d96f30ceb3 | call/member | `caplog.set_level` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:119` |
| op-a2ab1e982cd4d213 | call/member | `card_for_create.onToggleChangeSignal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:165` |
| op-4cf69d206c951be3 | call/member | `cbx_layout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:133` |
| op-798c420348cc8d1a | call/member | `cbx_layout.setContentsMargins` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:134` |
| op-a2cf626bf378d4bf | call/member | `cbx_wrapper.setLayout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:135` |
| op-6a1dcf3a2a1d1569 | call/member | `ccs.setFixedWidth` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:33` |
| op-2d0239c2748a2c8f | call/member | `cfbs_layout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:152` |
| op-ae2e6f0f6f92d42a | call/member | `cfbs_layout.setContentsMargins` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:151` |
| op-9650a6ff57f25dab | call/member | `cfbs_wrapper.setLayout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:153` |
| op-024540c5ccf9a9f2 | call/member | `cfg.save_value` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:385` |
| op-ec94ffda81ffafc2 | call/member | `cfg.selection.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:85` |
| op-6733ac2af5993795 | call/member | `challenge.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:47` |
| op-a9e1ee1c8fe7c151 | call/member | `challenge.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:47` |
| op-2e52a58309bc4531 | call/member | `changes.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:123` |
| op-3c985748ee697e82 | call/member | `changes_json_path.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:114` |
| op-f82bec77239124b6 | call/member | `changes_json_path.is_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:114` |
| op-bd0b1126754d63d5 | call/member | `channel.publish` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:40` |
| op-5002ef221f059b41 | call/member | `channel.subscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:39` |
| op-8224c4318b62cd59 | call/member | `char.isalnum` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:359` |
| op-f75555e11ffef0c0 | call/member | `char_set.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:38` |
| op-ec0dc7589ed07bae | call/member | `character_dict.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:153` |
| op-6551de093b648e82 | call/member | `check.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:763` |
| op-172dd32076a35227 | call/member | `check.stateChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:764` |
| op-df6dc2f4e44b8026 | call/member | `check_state.clear_exist_item` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:209` |
| op-ee45b325015c77db | call/member | `check_state.item_all_checked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:214` |
| op-4edeff23d8af13da | call/member | `check_state.item_quantity` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:230` |
| op-bbfcac25575b4dc6 | call/member | `check_state.list_swipe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:216` |
| op-7eb15ff2db3ffb1f | call/member | `check_state.next_possible_item_name` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:845` |
| op-7910202c7a7fe786 | call/member | `check_state.pop_checked_item` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:888` |
| op-8ec3a3d998ed802d | call/member | `check_state.try_choose_item` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:211` |
| op-0bd9e5e06acd2684 | call/member | `child.is_dir` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:77` |
| op-1bc33b264bda3609 | call/member | `child.palette` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:256` |
| op-30eb45bcfee8cccf | call/member | `child.setPalette` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:258` |
| op-6b1c42c0f279c891 | call/member | `chinese_strings.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/config_translation_generator.py:34` |
| op-d033694685b9dd10 | call/member | `chooseMultiEmulatorCombobox.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:87` |
| op-a6f71e5bcb180671 | call/member | `chooseMultiEmulatorCombobox.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:89` |
| op-1ee373e52b870c2d | call/member | `chooseMultiEmulatorCombobox.setCurrentIndex` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:88` |
| op-9915c2d4e30e9890 | call/member | `clear_response.json` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:145` |
| op-17fa98cd2bf3dc46 | call/member | `click_.join` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:140` |
| op-fb3b74ad4015de4d | call/member | `click_.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:138` |
| op-6a7e72878fc49117 | call/member | `click_centers.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:208` |
| op-533a9d3303f5a094 | call/member | `client._ScrcpyClient__init_server_connection` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_scrcpy_client.py:40` |
| op-5b18fdc13357ae4c | call/member | `client._prepare_android_runtime_folder` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/ocr/test_android_runtime.py:55` |
| op-4afe10b1594c6a9c | call/member | `client.decrypt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:118` |
| op-8a7261a87c1130c7 | call/member | `client.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_http_contract.py:55` |
| op-0b6c49c0653f0b56 | call/member | `client.init_model` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:293` |
| op-0361e309f55e7b58 | call/member | `client.post` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_http_contract.py:71` |
| op-a55c05ed331ff83c | call/member | `client.start_server` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:290` |
| op-7be81fd13771a158 | call/member | `client.swipe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy_client.py:138` |
| op-6d8520a6a0fe5f16 | call/member | `closeRequestBox.exec` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:606` |
| op-1ca6712261101040 | call/member | `cmd.command.startswith` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/commands.py:45` |
| op-35bfffe8dbd64a03 | call/member | `cmd.payload.get` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/commands.py:35` |
| op-1c426b925a629920 | call/member | `cmdline.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:226` |
| op-2b0bc9848b3929e1 | call/member | `cmdline.replace` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:86` |
| op-16c4692c84f6ee97 | call/member | `cmdline_dict.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:89` |
| op-cf83f47d7b6979d9 | call/member | `cmds.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:368` |
| op-1aae611dd6d1d218 | call/member | `codec.decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:252` |
| op-da96025474eef182 | call/member | `codec.parse` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:250` |
| op-02401579ef4ad6bd | call/member | `col.name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:175` |
| op-38e117ed7b26a155 | call/member | `color.check_sweep_availability` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:119` |
| op-e13df549781ee2dc | call/member | `color.match_rgb_feature` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:402` |
| op-679044e1bad5d859 | call/member | `color.rgb_in_range` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:409` |
| op-2cfd807cf0caea9e | call/member | `combo.addItems` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:51` |
| op-bc7ee11463987240 | call/member | `combo.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:750` |
| op-922de898a54ddd21 | call/member | `combo.currentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:60` |
| op-3d4abc3a9aa83bbb | call/member | `combo.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:53` |
| op-b6b1e7a7d2fdef6f | call/member | `combo.setCurrentIndex` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:749` |
| op-55be529b19401778 | call/member | `combo.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:52` |
| op-fb5b4d5d534fcd73 | call/member | `comboPosition.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:148` |
| op-cb0807966ba4e294 | call/member | `comboPosition.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:150` |
| op-452bd892bed4ae50 | call/member | `comboPosition.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:149` |
| op-01244000b1613834 | call/member | `comboStudent.addItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:126` |
| op-a2d12ba092b0c6bc | call/member | `comboStudent.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:131` |
| op-f2701c5c3047474e | call/member | `comboStudent.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:139` |
| op-50c170cf31094604 | call/member | `comboTip.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:253` |
| op-1412db2a99873084 | call/member | `command.payload.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:20` |
| op-5592419e72237c49 | call/member | `commit.author.name.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:79` |
| op-1285cce8da125208 | call/member | `commit.message.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:81` |
| op-6e6ad2f7224bc24d | call/member | `common_pop_ups.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:176` |
| op-21f3c0c3dfe995c9 | call/member | `common_pop_ups.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:175` |
| op-eb7f0a9d74095a89 | call/member | `common_task_img_reactions.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:215` |
| op-30e5dace28d10956 | call/member | `common_task_img_reactions.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:214` |
| op-cd2cfa51d0bdd276 | call/member | `comp.config_updated` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:83` |
| op-8b5065d704d8ed5d | call/member | `conf.add_signal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:412` |
| op-449fe24ac9e6a257 | call/member | `config.add_signal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:122` |
| op-265bdc2db76dedc8 | call/member | `config.get` | 16 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:279` |
| op-6fb48df65ed2dfaa | call/member | `config.get_main_thread` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:34` |
| op-f31480b440cf3b34 | call/member | `config.get_signal` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/notification.py:11` |
| op-5684d8e89d718cd5 | call/member | `config.inject` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:56` |
| op-17a9b773b14b5066 | call/member | `config.save` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:277` |
| op-bb57ac7849565da5 | call/member | `config.set` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:49` |
| op-62b18a29c4cf1350 | call/member | `config.set_and_save` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:163` |
| op-4c0d088808af2527 | call/member | `config.set_window` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:424` |
| op-8ac6772e93a31c87 | call/member | `config.update_create_quantity_entry` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:36` |
| op-e6f34be78bb5c8eb | call/member | `config_data.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:762` |
| op-7d979ebd59e57ccc | call/member | `config_dir.is_dir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:300` |
| op-8f8e0738ea90fa93 | call/member | `config_dir.mkdir` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:32` |
| op-14f6cd09344f7433 | call/member | `config_dir_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:304` |
| op-20947ca48d6c3ce3 | call/member | `config_file.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:246` |
| op-3404594cb72a58ce | call/member | `config_general.get` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:117` |
| op-8fb59776f76199a8 | call/member | `config_path.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:777` |
| op-ebc8ec9d70b3bb30 | call/member | `config_path.read_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:761` |
| op-4453f2d5e28aea8c | call/member | `config_path.write_text` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:764` |
| op-e7485b4946e01482 | call/member | `config_root.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:743` |
| op-9afa0de7c4f94fb3 | call/member | `config_root.iterdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:746` |
| op-99623478370fab10 | call/member | `config_root.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:297` |
| op-9602b1379dc3d7a6 | call/member | `config_root.resolve` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/paths.py:44` |
| op-1db0a2bef03814cd | call/member | `confirmButton.clicked.connect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:60` |
| op-1c935c269b7590ce | call/member | `confirmButton.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:59` |
| op-7aa6bf42ac48b5a7 | call/member | `connected_urls.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_scrcpy_client.py:34` |
| op-61467dafef65b868 | call/member | `console.print` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:94` |
| op-83904cdd83a08a83 | call/member | `container.setLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:148` |
| op-d872017e83c6341c | call/member | `content.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:720` |
| op-2b77d5ca8f64c92d | call/member | `content_type.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:149` |
| op-2e65ea992394b8c8 | call/member | `context.auth_manager.authenticate_control` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:157` |
| op-ebbe2d197f01d0e3 | call/member | `context.auth_manager.build_preauth_channel` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:107` |
| op-6038b19d5dfa6f21 | call/member | `context.auth_manager.change_password` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/ws_control.py:68` |
| op-d2c9d8bfed28c118 | call/member | `context.auth_manager.initialize_password` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:152` |
| op-3f0aa03bc7c56479 | call/member | `context.auth_manager.issue_remember_token` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/http.py:30` |
| op-5caa029bfe5e2583 | call/member | `context.auth_manager.issue_resume_ticket` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:63` |
| op-81fc4f12036bb5e4 | call/member | `context.auth_manager.issue_server_hello` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:105` |
| op-bfbbd9e5efe35ed5 | call/member | `context.auth_manager.open_control_session_after_initialize` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:153` |
| op-354593cd8d1b4634 | call/member | `context.auth_manager.password_state.as_public_dict` | 2 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:66` |
| op-a584d40650c68e30 | call/member | `context.auth_manager.resume_business_session` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:222` |
| op-72b7797f39f9c0d6 | call/member | `context.auth_manager.resume_control_session` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:131` |
| op-8d6f1606b42a8c3d | call/member | `context.auth_manager.server_public_key_b64` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/http.py:68` |
| op-c925828830f3ae1b | call/member | `context.auth_manager.subscribe_control` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/ws_control.py:45` |
| op-f30f7232397cc25a | call/member | `context.auth_manager.unsubscribe_control` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/ws_control.py:84` |
| op-98e649fb4e2cd239 | call/member | `context.auth_manager.verify_remember_proof` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/http.py:29` |
| op-2781a33d6b539819 | call/member | `context.config_manager.scan_once` | 4 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/api/commands.py:60` |
| op-8b9ec977ab016587 | call/member | `context.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:98` |
| op-1366d61473f50057 | call/member | `context.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:103` |
| op-330c2b9a997c3686 | call/member | `context.runtime.add_config` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:59` |
| op-5e911849f275225f | call/member | `context.runtime.check_for_update` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:112` |
| op-6f96302e2c116fc5 | call/member | `context.runtime.control_device_` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:136` |
| op-1d6faf97d80e3ce0 | call/member | `context.runtime.copy_config` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:75` |
| op-2453b57050cb7cff | call/member | `context.runtime.current_status` | 2 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:140` |
| op-90672354c32217ea | call/member | `context.runtime.detect_adb` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:95` |
| op-bfbc2ff87d8f7a1f | call/member | `context.runtime.export_config` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:83` |
| op-99a04c127544e285 | call/member | `context.runtime.import_config` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:90` |
| op-c55ce25dd02b8473 | call/member | `context.runtime.remove_config` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:67` |
| op-f184a4dd451fc7d0 | call/member | `context.runtime.restart_backend` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:124` |
| op-9a0268c00bdba8d3 | call/member | `context.runtime.set_android_active_config` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/http.py:105` |
| op-d948073a07f2fec0 | call/member | `context.runtime.solve_task` | 2 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:38` |
| op-68f023b2bed20159 | call/member | `context.runtime.start_scheduler` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:20` |
| op-11e23b6bc890f8d1 | call/member | `context.runtime.stop_all_tasks` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:128` |
| op-919329b63303d3f4 | call/member | `context.runtime.stop_scheduler` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:29` |
| op-11913f0818d3ced1 | call/member | `context.runtime.test_all_sha` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:103` |
| op-8ca4f84aa249d6ce | call/member | `context.runtime.toggle_android_active_config` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/http.py:112` |
| op-94a94cc87075acae | call/member | `context.runtime.update_setup_toml` | 2 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:111` |
| op-d7e37356292f68d3 | call/member | `context.runtime.update_to_latest` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:120` |
| op-9a187cd597549e28 | call/member | `context.runtime.valid_cdk` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/commands.py:99` |
| op-963a45ed41519d38 | call/member | `context.send` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/exception.py:65` |
| op-44e95e4d296cc270 | call/member | `context.shutdown` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:34` |
| op-56f1415a01fb135c | call/member | `context.startup` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:23` |
| op-d5c876673c6feec8 | call/member | `control_channel.decrypt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:61` |
| op-f9062d3cff93162c | call/member | `control_channel.encrypt` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:176` |
| op-02d5831ec5f1fccf | call/member | `coords.sort` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:224` |
| op-ef4a4f4d5b49a5ad | call/member | `copied.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:207` |
| op-f16a5b65d51023b7 | call/member | `core.config.config_set.ConfigSet._init_static_config` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `core/config/config_set.py:19` |
| op-3f0cfdbb2514832e | call/member | `core.device.nemu_client.NemuClient.connections.items` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/nemu_client.py:155` |
| op-7e1b19de0e47ca18 | call/member | `core.device.nemu_client.NemuClient.get_instance` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/nemu_client.py:422` |
| op-30108c8a5b1ea3b7 | call/member | `core.device.scrcpy.core.Client.get_scrcpy_jar_path` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/scrcpy/core.py:304` |
| op-8ff06d2d086c4f70 | call/member | `core.device.scrcpy_client.ScrcpyClient.get_instance` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/scrcpy_client.py:125` |
| op-3f588f9f973c7a57 | call/member | `core.scheduler.Scheduler.convert_to_seconds` | 4 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/scheduler.py:116` |
| op-b5acf31425389a74 | call/member | `count.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/clear_special_task_power.py:133` |
| op-3f226dc808da842e | call/member | `css.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:301` |
| op-8c92daa9619c6d0c | call/member | `current.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:115` |
| op-5c0d531d5e811856 | call/member | `current.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:524` |
| op-547998425bb2a9ab | call/member | `current_general.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:88` |
| op-79eed0bf38629e5d | call/member | `current_paths.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:145` |
| op-d502bd1c6a41d2d4 | call/member | `current_python.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:161` |
| op-2f5fa600a2e89372 | call/member | `current_repositories.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:173` |
| op-4860b22a2c7955c4 | call/member | `current_time.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:21` |
| op-8abcbec68cfa7e26 | call/member | `current_url.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:254` |
| op-1f601e2824069b9e | call/member | `cursor.position` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:80` |
| op-cfdde77d8e30ee3a | call/member | `cursor.setPosition` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:84` |
| op-eb46e2921080c833 | call/member | `cv2.imread` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `tests/module/test_cafe_reward_match.py:28` |
| op-39e08daccb3e2603 | call/member | `cv2.rectangle` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `tests/module/test_cafe_reward_match.py:85` |
| op-43a94515602a1daa | call/member | `d.app_current` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:332` |
| op-2f5c155e57c40fde | call/member | `d.app_stop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:387` |
| op-0f30a851c0182693 | call/member | `d.getprop` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:76` |
| op-271de8985fe13ddb | call/member | `d.shell` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:229` |
| op-e7807da19476d581 | call/member | `data.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/update_student_name.py:39` |
| op-757878af6b2d9e62 | call/member | `data.get` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/device_config.py:67` |
| op-983babcb3cb05bcd | call/member | `data.insert` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:67` |
| op-c26866775d530856 | call/member | `data.items` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/device_config.py:27` |
| op-ad2fe19e254ef259 | call/member | `data.removeprefix` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:74` |
| op-0ed5d60366502a28 | call/member | `data.replace` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:741` |
| op-83a7083751225b44 | call/member | `data.setdefault` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:675` |
| op-2132e093e6abe990 | call/member | `decoder.feed` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:28` |
| op-cb086795e05fa926 | call/member | `deduped.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:527` |
| op-495a41b7d7b5dcb7 | call/member | `defaults.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:192` |
| op-999f5f0ce41778ce | call/member | `del_button.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:91` |
| op-48b89cf445e82f28 | call/member | `dels.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:61` |
| op-684f9c7d607e5ffa | call/member | `dependency_paths.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:289` |
| op-ae229a803bb57b9d | call/member | `dependency_paths.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:292` |
| op-e8b701d774a625b5 | call/member | `deploy.installer._installer.Utils.copy_directory_structure` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:222` |
| op-755afcb8f521c072 | call/member | `deploy.installer._installer.Utils.download_file` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:632` |
| op-bebd6e55c7c970bc | call/member | `deploy.installer._installer.Utils.get_remote_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1059` |
| op-1843aff2032a4ac2 | call/member | `deploy.installer._installer.Utils.get_remote_sha_once` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:155` |
| op-13ff2cb26cd32124 | call/member | `deploy.installer._installer.Utils.github_api_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:172` |
| op-287747719fb15d05 | call/member | `deploy.installer._installer.Utils.mirrorc_api_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:176` |
| op-e2ebf68c0936022e | call/member | `deploy.installer._installer.Utils.pygit2_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:174` |
| op-34916f944e799d0f | call/member | `deploy.installer._installer.Utils.sudo` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:388` |
| op-83c80361eec4384a | call/member | `deploy.installer._installer.Utils.unzip_file` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:651` |
| op-fd9706c626e64955 | call/member | `deploy.installer.installer.FileSystemUtils.copy_directory_structure` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:287` |
| op-88d90f24bacc5bc9 | call/member | `deploy.installer.installer.FileSystemUtils.download_file` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:623` |
| op-5a686968fe932b68 | call/member | `deploy.installer.installer.FileSystemUtils.sudo` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:673` |
| op-3fcd0941429cb2d4 | call/member | `deploy.installer.installer.FileSystemUtils.unzip_file` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:651` |
| op-c60be35b7dfeb046 | call/member | `deploy.installer.installer.Utils.error_tackle` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:716` |
| op-6fc4937e1720d64d | call/member | `desktop.height` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:505` |
| op-45def3f11ebe3d4e | call/member | `desktop.width` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:505` |
| op-035978d9e3b1e981 | call/member | `dest_file.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:131` |
| op-4157b136bc2a6912 | call/member | `dest_file.is_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:132` |
| op-d7fe8054c3330550 | call/member | `dest_file.parent.mkdir` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:151` |
| op-c16111878cbe729a | call/member | `dest_file.unlink` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:133` |
| op-39812701a7b21bcf | call/member | `detailMessageBox.exec_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:256` |
| op-94d68fcff2801eba | call/member | `detailed_widgets.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:166` |
| op-135f71c4d9aa105c | call/member | `detect_episode_list.copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:177` |
| op-70cf018172d4f400 | call/member | `detect_episode_list.index` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:196` |
| op-f0e30cb4f36e2874 | call/member | `detect_episode_list.remove` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:174` |
| op-274286b77628a1b0 | call/member | `detected_name.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:447` |
| op-d46a1111f19cffff | call/member | `detected_student_pos.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:540` |
| op-e3d53b6afa10b672 | call/member | `dft.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:124` |
| op-92b6777d7b39877f | call/member | `dialog.exec_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:463` |
| op-642187575cd67ae8 | call/member | `dict2.copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:305` |
| op-6e037b14c8b11455 | call/member | `docs.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:38` |
| op-c8fe0cfe886a6658 | call/member | `download_f.write` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:195` |
| op-1af064c34eca8df1 | call/member | `dst.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:815` |
| op-9e58a3903d8f1c6a | call/member | `dst.is_dir` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:816` |
| op-3327c0c65ceb3d32 | call/member | `dst.unlink` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:819` |
| op-1429ecfb1a258156 | call/member | `dt.isoformat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:372` |
| op-2e4c2e5b21eabca6 | call/member | `e.__str__` | 20 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:317` |
| op-62e8bad64b433b17 | call/member | `e.stderr.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:174` |
| op-7f88d12c638b5171 | call/member | `edit.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:734` |
| op-535056be28985e52 | call/member | `edit.textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:735` |
| op-db5e0bcee2ffb739 | call/member | `employ_pos.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:445` |
| op-68f31c6610bfe89b | call/member | `emulator_path.endswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/process_api.py:19` |
| op-452f9a5ec0bcad59 | call/member | `encoded_image.tobytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:282` |
| op-bb6cb63c67d3cac6 | call/member | `endpoint.configure_binary_encryption` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/remote.py:15` |
| op-d9b2d9bf82936113 | call/member | `endpoint.incoming.put` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:97` |
| op-5277a14e2733cfb6 | call/member | `endpoint.recv_bytes` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:21` |
| op-edc8bf4b681f2bff | call/member | `endpoint.recv_json` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:34` |
| op-c9ce165d25c3552d | call/member | `endpoint.send_bytes` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:76` |
| op-05152d5be6c1120d | call/member | `endpoint.send_json` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:23` |
| op-be25e6d5cae0cf5e | call/member | `entries.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:61` |
| op-839a95cd3a2af2b3 | call/member | `entry.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:133` |
| op-f1f3d607c2193978 | call/member | `env_mgr.check_env_patch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1134` |
| op-07cfb920eb40dd52 | call/member | `env_mgr.check_pip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1132` |
| op-21bdbd5b9ff57379 | call/member | `env_mgr.check_pth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1133` |
| op-91f8d92ab3d53542 | call/member | `env_mgr.check_python` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1131` |
| op-f8efd7e4c4d24296 | call/member | `env_mgr.fix_shebangs` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1142` |
| op-2d7ddd8cf2faf5f8 | call/member | `env_mgr.install_requirements` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1141` |
| op-fd7da3395cb4bcba | call/member | `errorfile.write` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:129` |
| op-6492c9a16017650a | call/member | `event.accept` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:352` |
| op-88a1bea5e4167e06 | call/member | `event.button` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:210` |
| op-9b5fc2181637d885 | call/member | `event.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:887` |
| op-360394e48584f494 | call/member | `event.key` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:801` |
| op-9dd226f581e56fa0 | call/member | `event.type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:800` |
| op-b7efbc375a935e26 | call/member | `event_file.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:81` |
| op-3b5d222247090f05 | call/member | `events.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:323` |
| op-042bdf5cbb19a8ef | call/member | `exe_files.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:698` |
| op-6c919dec069543f1 | call/member | `extracted_port.group` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/bluestacks_module.py:52` |
| op-c6875b4f9e49f148 | call/member | `f.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:529` |
| op-297590d10d26fbdd | call/member | `f.read` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:512` |
| op-c6842478591dfaed | call/member | `f.readlines` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:456` |
| op-c6a266c40d37018e | call/member | `f.seek` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:510` |
| op-4ac6b0e84ef5eab9 | call/member | `f.setBold` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:164` |
| op-8ad6d955c81a1546 | call/member | `f.write` | 28 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/exception.py:90` |
| op-ba0255f0a8c1ba39 | call/member | `f.writelines` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:462` |
| op-738ea6273e9903ca | call/member | `f2.setBold` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:170` |
| op-6b34978cd367d2ba | call/member | `fallback_results.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:568` |
| op-e40d3b0d0b90a061 | call/member | `favor_student.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:160` |
| op-45ab1f1d20f4f678 | call/member | `file.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:35` |
| op-511786caf4118abe | call/member | `file_dialog.exec_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:45` |
| op-cc5a323b5c8b0146 | call/member | `file_dialog.selectedFiles` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:46` |
| op-2ab173dc93f98b8f | call/member | `file_dialog.setFileMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:42` |
| op-a6dfeb95b603695a | call/member | `file_dialog.setNameFilter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:43` |
| op-d03d26c29b94045e | call/member | `file_dialog.setOption` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:44` |
| op-29863a8f93affdda | call/member | `file_err.fileno` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:60` |
| op-af0b8756c1891d25 | call/member | `file_handler.setFormatter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:80` |
| op-cb624e7615a5a250 | call/member | `file_handler.setLevel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:79` |
| op-4fb848169d67e023 | call/member | `file_out.fileno` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:59` |
| op-1e49260b8a9f6598 | call/member | `file_path.endswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:35` |
| op-cdc9981f3cd22de1 | call/member | `file_path.parent.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:255` |
| op-8cb9a6438d4f503b | call/member | `file_path.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:39` |
| op-81bfd8ed58494a97 | call/member | `filename.endswith` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:38` |
| op-d087171b977a7ebc | call/member | `filename.lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:697` |
| op-79346562d8861832 | call/member | `filename.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:41` |
| op-937c2e9ba331d6a6 | call/member | `files.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:109` |
| op-3e14e9a1a0e4e575 | call/member | `fluent.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:108` |
| op-e139833f0d6d1659 | call/member | `font.setBold` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:156` |
| op-2f5f8c54817023b3 | call/member | `forced.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:54` |
| op-947aca66bc77b826 | call/member | `formatter.format` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:49` |
| op-d25b099410fa3abe | call/member | `fp.read` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:137` |
| op-05688967ff61b6ef | call/member | `fp.write` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:174` |
| op-f9bb3504717663de | call/member | `fr.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/default_config.py:5174` |
| op-d577dfd5f9d02bb5 | call/member | `fr.read` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/default_config.py:5173` |
| op-8a13db9f0e62a089 | call/member | `fragments.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:80` |
| op-89b4b37f36bc621c | call/member | `frame.setLayout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:223` |
| op-409270f84bc01805 | call/member | `frame.setMinimumWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:220` |
| op-9924c3ecebe93489 | call/member | `frame.to_ndarray` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:254` |
| op-08478a56d9544e98 | call/member | `frames.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/framing.py:48` |
| op-a1e8f0423b3bb0b7 | call/member | `fullMissionList.append` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_normal_task.py:65` |
| op-6f4e55fc7d7d525f | call/member | `full_gui.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:100` |
| op-a17358d1aa453e94 | call/member | `full_mission_list.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:174` |
| op-d03ad5f56d677a6c | call/member | `fw.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/default_config.py:5178` |
| op-4d692a5151019785 | call/member | `fw.write` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/default_config.py:5177` |
| op-82b4b965e2a8eba0 | call/member | `general.get` | 21 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:42` |
| op-d1bded862d99393c | call/member | `general.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:88` |
| op-2b9df7af11660bed | call/member | `git_dir.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:791` |
| op-f13e9957a32f48e8 | call/member | `git_ops.get_local_head_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:463` |
| op-57137a3e92ec5b37 | call/member | `git_ops.get_remote_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:226` |
| op-98bd41a26a377c27 | call/member | `github_button.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:145` |
| op-a6ed7fb1e42b0e77 | call/member | `group.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:230` |
| op-013ee66d873a1099 | call/member | `groups.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:234` |
| op-215294e9810e7566 | call/member | `guard.activate` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:32` |
| op-64901630517f0b50 | call/member | `guard.force_restore` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:108` |
| op-55f5cff77e16564b | call/member | `guard.release` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:33` |
| op-2bd34165f7794884 | call/member | `gui.components.expand.baasUpdateConfig.TestGetRemoteShaMethodWorker.github_api_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:571` |
| op-fdec61553278f091 | call/member | `gui.components.expand.baasUpdateConfig.TestGetRemoteShaMethodWorker.pygit2_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:573` |
| op-8c60bccfedadeaff | call/member | `h_layout.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:276` |
| op-bdc71cd7d968caf5 | call/member | `h_layout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:275` |
| op-c9733af8cd85d0bf | call/member | `handler.acquire` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:150` |
| op-eef5cb5b6b69c157 | call/member | `handler.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:126` |
| op-024e6f8cb0fce861 | call/member | `handler.flush` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:155` |
| op-742cefec9a5101df | call/member | `handler.get_remote_latest_sha` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:40` |
| op-fb29067275044af3 | call/member | `handler.handle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:87` |
| op-fe60b2f72b0f91c2 | call/member | `handler.release` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:158` |
| op-0b97b1ff0cae1609 | call/member | `handler.setFormatter` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:41` |
| op-50f88014d5072d68 | call/member | `handler.setLevel` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:42` |
| op-4f0f56a325aa2907 | call/member | `handler.stream.seek` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:153` |
| op-80cb001d3e47c401 | call/member | `handler.stream.truncate` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:154` |
| op-6cceac804c6722d1 | call/member | `handler1.setFormatter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:62` |
| op-755c177ee01d47fd | call/member | `handler_for_logger.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:102` |
| op-bb6331d0675f7df1 | call/member | `handler_for_logger.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:100` |
| op-ca45eb0bda39d0df | call/member | `handler_for_logger.setSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:99` |
| op-c27bd4f87f0b5e48 | call/member | `head.get` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:310` |
| op-4c1bec565e1fd10d | call/member | `head_times.font` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:169` |
| op-0d52fa4b4f9237a7 | call/member | `head_times.setFont` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:171` |
| op-39159969fcce4ba9 | call/member | `header.height` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:455` |
| op-ec75cb2052f37479 | call/member | `header.setSectionResizeMode` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:285` |
| op-393399c62f33669c | call/member | `hello.get` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:217` |
| op-030dc8aa7747130c | call/member | `helpModal.resize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:519` |
| op-25d714424ad64063 | call/member | `helpModal.setFocus` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:520` |
| op-e5ac063fa5d3c3d9 | call/member | `helpModal.setWindowIcon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:518` |
| op-d7a1597ee0bf765b | call/member | `helpModal.setWindowTitle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:517` |
| op-19867acac96c12ee | call/member | `helpModal.show` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:521` |
| op-4e4877a003d8bcfa | call/member | `helpModal.tr` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:517` |
| op-e7f1b871dc4e06fe | call/member | `historyModal.resize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:529` |
| op-50b457d99df74ba9 | call/member | `historyModal.setFocus` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:530` |
| op-1d395c042a23aaca | call/member | `historyModal.setWindowIcon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:528` |
| op-0907115746e328f7 | call/member | `historyModal.setWindowTitle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:527` |
| op-8db85a5dd46fedf2 | call/member | `historyModal.show` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:531` |
| op-6e566755fe24ad0e | call/member | `host_value.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:45` |
| op-e36151e35963aa20 | call/member | `host_value.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:42` |
| op-1b92a636888b40b0 | call/member | `hotkey_layout.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:144` |
| op-df2c6e43451209b2 | call/member | `hotkey_layout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:143` |
| op-90aa696f2c7b2050 | call/member | `hotkey_layout.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:142` |
| op-70546f105a1c4711 | call/member | `hotkey_str.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:214` |
| op-ffb910de40a2112c | call/member | `i.find` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:13` |
| op-4461568db8f01be2 | call/member | `i.isdigit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:58` |
| op-44ba8278e7cb9d09 | call/member | `i.islower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:58` |
| op-ea46b15cf3f9dd86 | call/member | `i.isupper` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:63` |
| op-31c94908e5f9f8ef | call/member | `i.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:14` |
| op-435e38bf2bdbd995 | call/member | `i.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:310` |
| op-7b5e676aae9176e8 | call/member | `ids.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:82` |
| op-eaabaf16de86c75d | call/member | `image_dic.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:30` |
| op-b9f1d78c0bbccb59 | call/member | `image_x_y_range.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:31` |
| op-24eada5c092e7f15 | call/member | `img.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:58` |
| op-e3271dcb3ff1b94f | call/member | `img_possible.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group.py:28` |
| op-e55c716a574d9f97 | call/member | `img_possibles.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_daily_free_power.py:90` |
| op-532f21be0e5cefd3 | call/member | `img_possibles.update` | 17 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/arena.py:126` |
| op-9058f1ec84f7b5f9 | call/member | `img_reactions.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:133` |
| op-9a34d1a9a49b7672 | call/member | `img_reactions.update` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:687` |
| op-82df1266a7010c7e | call/member | `imported.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:233` |
| op-5eaf3f7226800f14 | call/member | `indexes.copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:337` |
| op-c8f97adb63b8eba2 | call/member | `info.copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:645` |
| op-4f2efe4ce75b8eeb | call/member | `info.filename.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:799` |
| op-f9562ec401561743 | call/member | `info.is_dir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:805` |
| op-9014f7aa0e1b00e3 | call/member | `init.install` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:430` |
| op-a20e335b98d702af | call/member | `init.uninstall` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1134` |
| op-14db62558329b2c7 | call/member | `initializer.check_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:303` |
| op-c46b0950b4752167 | call/member | `inputComponent.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:80` |
| op-35d0bd5f83b5d685 | call/member | `inputComponent.checkedChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:75` |
| op-634c05c6420771b9 | call/member | `inputComponent.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:99` |
| op-2983107deb3a15c6 | call/member | `inputComponent.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:93` |
| op-b75c7d1605921fe6 | call/member | `inputComponent.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:74` |
| op-1e8907e471d4227d | call/member | `inputComponent.setCurrentIndex` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:85` |
| op-39f1e79594f244a9 | call/member | `inputComponent.setFixedWidth` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:56` |
| op-273853af9bf20287 | call/member | `inputComponent.setMaximum` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:129` |
| op-fcf289cde577259b | call/member | `inputComponent.setMinimum` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:128` |
| op-9d421f743168cd2e | call/member | `inputComponent.setMinimumWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:243` |
| op-8e780bc353451df3 | call/member | `inputComponent.setReadOnly` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:104` |
| op-65af116a7cc2b1fb | call/member | `inputComponent.setText` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:57` |
| op-135182d3435372a9 | call/member | `inputComponent.setValue` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:126` |
| op-d73d7d61f3ac8c94 | call/member | `inputComponent.textChanged.connect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:119` |
| op-cc87b5128abe96c1 | call/member | `inputComponent.valueChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:127` |
| op-cd3cbaeb36f5de12 | call/member | `input_content.split` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:100` |
| op-971d4bee7e8f5a23 | call/member | `input_for_count.editingFinished.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:117` |
| op-f0929b74d0534488 | call/member | `input_for_count.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:114` |
| op-e218b63ccd4dabca | call/member | `input_for_count.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:116` |
| op-3cbd6f492078dca7 | call/member | `input_for_count.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:118` |
| op-bef53f95cc4035aa | call/member | `input_for_create_method.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:250` |
| op-18d955bf43023139 | call/member | `input_for_create_method.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:253` |
| op-72d13b5bc0acbb0c | call/member | `input_for_create_method.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:252` |
| op-df7b85e3ffd6fe16 | call/member | `input_for_layer_count.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:126` |
| op-9087a5e84bbfe86b | call/member | `input_for_layer_count.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:129` |
| op-1d6232eba11641e4 | call/member | `input_for_layer_count.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:128` |
| op-23ad1ef87f3d3ef6 | call/member | `input_for_rc_create_priority.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:270` |
| op-6215ebfddf831c2e | call/member | `input_for_rc_create_priority.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:271` |
| op-67580aebf4ebd35d | call/member | `inst.get_latest_version` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:422` |
| op-b1fc7139ed415b07 | call/member | `item.closed.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:291` |
| op-41eab9c033b3d8bd | call/member | `item.count` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:100` |
| op-68736e8b8be133e8 | call/member | `item.get` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:152` |
| op-c4a1059c3699b862 | call/member | `item.is_dir` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:220` |
| op-ca10032af8612506 | call/member | `item.is_file` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:223` |
| op-09e263b5c7123eca | call/member | `item.isdigit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:95` |
| op-63a2ceab97e917e6 | call/member | `item.layout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:231` |
| op-8db7f8336f1fa9c2 | call/member | `item.pressed.connect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:290` |
| op-9b2ce27d0433cb19 | call/member | `item.relative_to` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:218` |
| op-8d14ef4aa99e096a | call/member | `item.setCloseButtonDisplayMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:287` |
| op-2576017dfe968e10 | call/member | `item.setFlags` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:481` |
| op-ae4fd6046302a5ee | call/member | `item.setMaximumWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:285` |
| op-cd315f9316890202 | call/member | `item.setMinimumWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:284` |
| op-0e4317fca9329bbf | call/member | `item.setRouteKey` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:282` |
| op-06b3fbeaea5dc703 | call/member | `item.setSelectedBackgroundColor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:288` |
| op-0a5a9f33ce513a2d | call/member | `item.setShadowEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:286` |
| op-3fe5649fc0394961 | call/member | `item.setTextAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:480` |
| op-12b53042f2434bbc | call/member | `item.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:101` |
| op-b0c1296996bafc02 | call/member | `item.strip` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:36` |
| op-771aa3ea6e2b6ebf | call/member | `item.widget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:227` |
| op-a77ce92b9e0119f7 | call/member | `item_icon_widget.setFixedSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:367` |
| op-553b62de1116de59 | call/member | `item_icon_widget.setToolTip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:368` |
| op-bfe3f537c07ab999 | call/member | `item_template.copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:1005` |
| op-cb692e065b856a82 | call/member | `item_time_label.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:381` |
| op-7470c876873ba8d8 | call/member | `item_time_label.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:380` |
| op-cf26e1dba8022c51 | call/member | `item_value_label.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:375` |
| op-b8f1e4d10c331ece | call/member | `item_value_label.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:374` |
| op-10b8615020ef1fdb | call/member | `items.remove` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:200` |
| op-18e92b0c8b61d63d | call/member | `itm.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:87` |
| op-8c93c58d64626682 | call/member | `json.load` | 1 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `module/activities/activity_utils.py:14` |
| op-1349105f52e53141 | call/member | `json_data.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/generate_dataclass_code.py:16` |
| op-d83fd66b0f3fa279 | call/member | `k.capitalize` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:161` |
| op-3e444b54f4898ec9 | call/member | `key.find` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:134` |
| op-190faef75b88d354 | call/member | `key.split` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/toml_config.py:70` |
| op-73639e7f8851968c | call/member | `key.startswith` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:126` |
| op-d84834f1ab346700 | call/member | `key.strip` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:41` |
| op-f812eecb9945285a | call/member | `key_map.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:392` |
| op-284aa045f0290e39 | call/member | `key_path.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:396` |
| op-0b779ef980f6d68e | call/member | `keys.remove` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:70` |
| op-1c5487ccef4248ec | call/member | `kwargs.get` | 20 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:24` |
| op-e59cee626ca7d157 | call/member | `kwargs.keys` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:242` |
| op-c019b8f401d1da90 | call/member | `kwargs.pop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:243` |
| op-249aa2647df548d6 | call/member | `label.font` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:163` |
| op-2a3658b00b476f4d | call/member | `label.setFont` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:165` |
| op-efd5efc25eebf7f2 | call/member | `label.setProperty` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:383` |
| op-686c306e126a0a92 | call/member | `label.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:382` |
| op-67cc66780a9b3c61 | call/member | `label.setToolTip` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:723` |
| op-8847449c99f4400f | call/member | `label.style` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:385` |
| op-d37fbf8e194e2771 | call/member | `labelComponent.setStyleSheet` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:231` |
| op-8bd8ac783a6d531f | call/member | `labelComponent.setToolTip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:68` |
| op-1a3e9e7f57c70e3a | call/member | `labelTarget.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:151` |
| op-ad42799269dee479 | call/member | `label_region.font` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:155` |
| op-c1a8a31469a4ce1a | call/member | `label_region.setFont` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:157` |
| op-5988fed185dcbe30 | call/member | `language.value.name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:74` |
| op-9499aa5fe60dbf3d | call/member | `last_detected_ep.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:186` |
| op-cdc1825dc431a397 | call/member | `launch_exec_args.insert` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:981` |
| op-0c0d91f3c3c53080 | call/member | `launcher.run_app` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1146` |
| op-2bae07f70896dfd5 | call/member | `layInput.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:137` |
| op-cd834d922d159631 | call/member | `laySelect.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:135` |
| op-2432c71497d22480 | call/member | `layout.addLayout` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:291` |
| op-f8cc20736efdd5d2 | call/member | `layout.addStretch` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:108` |
| op-4ee3fcab385f0b57 | call/member | `layout.addWidget` | 45 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:45` |
| op-5e8f770413487e56 | call/member | `layout.count` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:237` |
| op-5fa00f16170c9b97 | call/member | `layout.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:106` |
| op-24fb666b78476ad2 | call/member | `layout.setColumnStretch` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:201` |
| op-7ba1199fb318f674 | call/member | `layout.setContentsMargins` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:16` |
| op-701ddac58aef2c9a | call/member | `layout.setSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:109` |
| op-826aa611cdf685af | call/member | `layout.setStyleSheet` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:211` |
| op-51193f24e6b8a30e | call/member | `layout.setVerticalSpacing` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:17` |
| op-0e0300d931367ac2 | call/member | `layout.takeAt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:238` |
| op-54de9e4252be12d4 | call/member | `layout_for_acc_ticket.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:107` |
| op-3db12ed0f3df0040 | call/member | `layout_for_count.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:119` |
| op-5f5961de02373503 | call/member | `layout_for_create_method.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:255` |
| op-e76a2c6185411488 | call/member | `layout_for_create_priority.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:283` |
| op-7a23ee80e94f4511 | call/member | `layout_for_create_priority_list.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:299` |
| op-5c92f68cc9ee5043 | call/member | `layout_for_layer_count.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:130` |
| op-1c2dcd11e4a41ea2 | call/member | `layout_for_line_extra.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:275` |
| op-f81a414893fcb5c0 | call/member | `layout_for_line_one.addLayout` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:109` |
| op-ae7e1780aa3b4d37 | call/member | `layout_for_line_one.addSpacing` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:110` |
| op-563c2d6fde51aa44 | call/member | `layout_for_line_three.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:301` |
| op-b0f9bffe8440554a | call/member | `layout_for_line_two.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:285` |
| op-aef36b6d712d521f | call/member | `layout_for_rc_create_priority.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:272` |
| op-7b1019d46767f8de | call/member | `layout_wrapper.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:421` |
| op-dd2b20c1b5148737 | call/member | `layout_wrapper.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:208` |
| op-9728dad55c5f3073 | call/member | `layout_wrapper.setContentsMargins` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:206` |
| op-7ebc0618c3cc9e2c | call/member | `layout_wrapper.setSpacing` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:207` |
| op-7a9d6ee19cb46201 | call/member | `legacy_general.get` | 19 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:44` |
| op-db94cc8972e225f6 | call/member | `legacy_paths.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:149` |
| op-6c79f3a184390e8b | call/member | `legacy_urls.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:113` |
| op-535c99d9bbaa606b | call/member | `level.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:119` |
| op-897fe7cfef947fc0 | call/member | `level_str.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:736` |
| op-4c97adc72852b167 | call/member | `levels_label.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:21` |
| op-ef227854c21c6e80 | call/member | `line.lstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:63` |
| op-04b9da654476355c | call/member | `line.replace` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:459` |
| op-69203f52dc5e9483 | call/member | `line.rstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:81` |
| op-1b8cee1ac7cc16a6 | call/member | `line.split` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:40` |
| op-adf5d02df1130f23 | call/member | `line.startswith` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:458` |
| op-1b49f36607e488e0 | call/member | `line.strip` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:38` |
| op-d237f38b29523fe8 | call/member | `lineEdit.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:163` |
| op-c9c1d902da9a3b21 | call/member | `lineEdit.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:170` |
| op-4b7470056a52b05e | call/member | `lineEditStudent.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:128` |
| op-6357c8cd884375f8 | call/member | `lineEditStudent.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:134` |
| op-2ff588a334107731 | call/member | `line_edit.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:49` |
| op-ab6081f480b0f3f6 | call/member | `lines.append` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/generate_dataclass_code.py:9` |
| op-fbcfc87cb4ea55f4 | call/member | `lnk_path.endswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:217` |
| op-a0004b6559ed9b39 | call/member | `lo.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:149` |
| op-404cd846079d82b3 | call/member | `loaded.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:299` |
| op-6214e405a90b1126 | call/member | `local.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:365` |
| op-71c24c4d9c8cd169 | call/member | `log_name_list.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:882` |
| op-c33232cd1dadb90d | call/member | `log_quantity_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:887` |
| op-a29afe768e66115c | call/member | `log_queue.put_nowait` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:59` |
| op-7f6f11104deee1d1 | call/member | `logger.debug` | 2 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/app.py:45` |
| op-03fa37acc5f2a5eb | call/member | `logger.error` | 12 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/ocr/ocr.py:223` |
| op-16b488f96b6010a0 | call/member | `logger.exception` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `service/app.py:49` |
| op-ddc7a75c7fc17fe0 | call/member | `logger.info` | 47 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:208` |
| op-443f91c331512a2b | call/member | `logger.log` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `tests/service/test_service_injection.py:53` |
| op-f3bfd3d5971f5b32 | call/member | `logger.log_collector.get_nowait` | 2 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `tests/service/test_service_injection.py:35` |
| op-7c781ed77ff64eed | call/member | `logger.setLevel` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `tests/service/test_system_logging.py:84` |
| op-0c2ce18d566bad21 | call/member | `logger.success` | 3 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:134` |
| op-bead2bb727369355 | call/member | `logger.warning` | 25 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:179` |
| op-e6d72c02744d5cda | call/member | `logger_attached.__setitem__` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:111` |
| op-8b9d11947f98529c | call/member | `loop.call_soon_threadsafe` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:870` |
| op-ced5b6fd446e8f2c | call/member | `loop.create_pipe_connection` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:28` |
| op-72131b9ef15d8ca2 | call/member | `loop.create_unix_server` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:130` |
| op-f1bf37d7d2a61f02 | call/member | `loop.get_exception_handler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:95` |
| op-9a4e690b9090e8ab | call/member | `loop.run_in_executor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:596` |
| op-59a0c31d25092663 | call/member | `loop.set_exception_handler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:109` |
| op-f4d716e061c1164e | call/member | `los.append` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/clear_special_task_power.py:81` |
| op-685b62681bc1a432 | call/member | `lower_url.find` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:235` |
| op-e8e64a80585505c4 | call/member | `m.group` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:289` |
| op-566a663bca264177 | call/member | `main_key_str.upper` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:235` |
| op-b1ec3891997ddbc8 | call/member | `main_story.auto_fight` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:292` |
| op-051099fb41fc53e9 | call/member | `main_thread.get_baas_thread` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:117` |
| op-b0b5bfaecdce78df | call/member | `main_window.get` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:104` |
| op-b66c94ef89c40be3 | call/member | `manager._check_resource` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:193` |
| op-d6e4e7a42b1c2733 | call/member | `manager._classify_config_change` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:105` |
| op-0ddac61e4c1fb99a | call/member | `manager._handle_watch_batch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:117` |
| op-36d84bcac81e2edd | call/member | `manager._merge_setup_toml` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:366` |
| op-554954924596879a | call/member | `manager._new_session_from_secrets` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:39` |
| op-6469d7b96fc8c31d | call/member | `manager._project_setup_toml` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:363` |
| op-0ec2a1ea255fe02f | call/member | `manager.apply_patch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:235` |
| op-b3598301c5cd644d | call/member | `manager.change_password` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:46` |
| op-1c28408bad6de192 | call/member | `manager.clone` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:400` |
| op-0a4f3288760ffb65 | call/member | `manager.determine_update_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:638` |
| op-d0cb5d83d5870cb0 | call/member | `manager.execute_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:641` |
| op-df817da9f46858b0 | call/member | `manager.get_local_sha` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:407` |
| op-59065492ee2596e8 | call/member | `manager.get_remote_sha` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:416` |
| op-72487ef95f13ad73 | call/member | `manager.get_session` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:51` |
| op-b06a5bc4edbbacd4 | call/member | `manager.get_snapshot` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:184` |
| op-33472f939f322cc5 | call/member | `manager.initialize_password` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:38` |
| op-ff3a8e45506a81d0 | call/member | `manager.issue_remember_token` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:107` |
| op-179d4b28cace0007 | call/member | `manager.scan_once` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:141` |
| op-3c113846845019e7 | call/member | `manager.server_public_key_b64` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:62` |
| op-94a33351e4789242 | call/member | `manager.set_loop` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:115` |
| op-fe55dba415d22d7a | call/member | `manager.subscribe_control` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:44` |
| op-edf307560297d225 | call/member | `manager.subscribe_updates` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:139` |
| op-4e7cc15935843c19 | call/member | `manager.update` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:433` |
| op-3229d69d86af2941 | call/member | `manager.verify_remember_proof` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:106` |
| op-a93d41e5613c3efe | call/member | `match.group` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:98` |
| op-da7dacf7496aea92 | call/member | `match.groups` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:98` |
| op-f3ede52246b9bbd5 | call/member | `member.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:813` |
| op-b64b698e9fb815f7 | call/member | `member.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:835` |
| op-25666a50268e5f74 | call/member | `members.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:806` |
| op-5dfe681a72842a85 | call/member | `menu.addAction` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:251` |
| op-3873d5a8cd9b6f26 | call/member | `menu.exec` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:254` |
| op-c55ca89fe7a95b2d | call/member | `merged.setdefault` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:210` |
| op-b9cc2f4c133c376d | call/member | `message.get` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:62` |
| op-d81363a2394ece73 | call/member | `message.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:186` |
| op-c51e3d7a8c1ad644 | call/member | `message.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/exception.py:67` |
| op-412513ff58329a6f | call/member | `message.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:65` |
| op-aeb7afac16455e1d | call/member | `messages.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:418` |
| op-c380e98b6edbeeaa | call/member | `method.get` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/const.py:157` |
| op-424087c4ac94a032 | call/member | `method_config.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:565` |
| op-e4722c20d9586412 | call/member | `mirrorc_inst.get_latest_version` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:103` |
| op-d7a4ccd157ebf72f | call/member | `mirrorc_inst.set_version` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1058` |
| op-929fdf00a80d3d29 | call/member | `mission.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:57` |
| op-63b4ec93187f271b | call/member | `mission.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:57` |
| op-b458a2bae9180e51 | call/member | `mode_select.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:107` |
| op-81e7c2b2e6edcf44 | call/member | `mode_select.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:111` |
| op-5a0358d9b2ffc34e | call/member | `mode_select.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:108` |
| op-bb21bd77c0f11dff | call/member | `mode_select_layout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:109` |
| op-a0da0a9fc7fe382a | call/member | `modifiers.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:220` |
| op-64ab5ee618292163 | call/member | `monkeypatch.chdir` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/module/test_cafe_reward_match.py:24` |
| op-9fd218431ffc9079 | call/member | `monkeypatch.delenv` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:29` |
| op-4f5bf16562311595 | call/member | `monkeypatch.setattr` | 60 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/device/test_uiautomator2_client.py:23` |
| op-509341cefa4e5f1d | call/member | `monkeypatch.setenv` | 20 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/ocr/test_android_runtime.py:26` |
| op-fca9dbcd0211a65b | call/member | `monkeypatch.setitem` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:46` |
| op-01e6cf03992cf316 | call/member | `mostPossibleMaterial.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:95` |
| op-b9eb5ac3818843ff | call/member | `multiInstanceNumberInputComponent.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:90` |
| op-5ca2810836bb6318 | call/member | `multiInstanceNumberInputComponent.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:82` |
| op-c66b41b2ccec59d4 | call/member | `multiInstanceNumberInputComponent.textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:83` |
| op-d989dfaaa154ede2 | call/member | `name.format` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:116` |
| op-1f85faf9531c5a30 | call/member | `name.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:72` |
| op-07948785a8738baf | call/member | `name.replace` | 15 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:517` |
| op-6e2ea7eef31e3e81 | call/member | `name.rsplit` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:92` |
| op-d7f51bd59c62b433 | call/member | `name.split` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:71` |
| op-58ee3ce80ed41aae | call/member | `name.startswith` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:772` |
| op-1f7ebc5225f74a6b | call/member | `name.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:196` |
| op-f5d5555b0f479a65 | call/member | `need_buy_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:116` |
| op-64c0da1778a4f9b6 | call/member | `nemu.down` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:425` |
| op-90be22e719662077 | call/member | `nemu.up` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:427` |
| op-8302e311043418f2 | call/member | `new.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:268` |
| op-3e8971daff3457d0 | call/member | `new.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:110` |
| op-e5c0ffc2342e23f7 | call/member | `new_name.encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:188` |
| op-a473c78d3f581eaa | call/member | `new_origin.fetch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:878` |
| op-aebae9418dd76b30 | call/member | `new_password.strip` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:238` |
| op-caaa2b7fdde33716 | call/member | `new_value.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:48` |
| op-d62d37fe48cf9afb | call/member | `node.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:148` |
| op-0f332849153fdd53 | call/member | `normalized_path.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:178` |
| op-caa5798f61108d9a | call/member | `notification.success` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/exploreConfig.py:46` |
| op-f48e7ce5480ca70f | call/member | `np.full` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/module/test_cafe_reward_match.py:31` |
| op-8413766ce37b52e8 | call/member | `obj_name.endswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:546` |
| op-d5b72bb01fa2fcf7 | call/member | `ocr.Baas_ocr` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:179` |
| op-8cb944d633676f11 | call/member | `ocr_dict.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:368` |
| op-475f769a93dd25ee | call/member | `ocr_res.lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:45` |
| op-4d495d9c087f2d82 | call/member | `ocr_res.split` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:769` |
| op-533d9ea464922412 | call/member | `old.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:109` |
| op-d91a3d87e7f76a49 | call/member | `old_name.encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:187` |
| op-fc3a19d5640bd7cd | call/member | `op.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:54` |
| op-24aeb3dd49ca7651 | call/member | `op.model_dump` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:347` |
| op-daf544115a4eef89 | call/member | `operation.startswith` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:343` |
| op-c69d216b57799a57 | call/member | `ops.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:114` |
| op-1577d2e991c50ebd | call/member | `ops.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:122` |
| op-386c14642710c0ac | call/member | `optionPanel.addStretch` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:70` |
| op-f9eb148eb4f70d1c | call/member | `optionPanel.addWidget` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:40` |
| op-e66d52dda487ef20 | call/member | `origin.fetch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:300` |
| op-7cb1ceac1a3c05a2 | call/member | `origin_image.tobytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:241` |
| op-4076d6a29b65a50d | call/member | `origin_lst.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:113` |
| op-8ef95dff4eb369f9 | call/member | `original_ap.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:423` |
| op-6de9fc0b7131acd8 | call/member | `original_bounty_coin.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:458` |
| op-9becc4e5b29c0b8f | call/member | `original_creditpoints.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:433` |
| op-fb70d068263f1e54 | call/member | `original_pass.get` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:476` |
| op-9ef390a080d49adf | call/member | `original_pyroxene.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:441` |
| op-7f45ae95e047b1b7 | call/member | `original_tactical_challenge_coin.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:449` |
| op-6a01d02a7e9a34d7 | call/member | `out.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:439` |
| op-317210893cc4ae8e | call/member | `output.append` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:42` |
| op-ff7a703152d09791 | call/member | `output.decode` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_start_command.py:19` |
| op-11d65aa241e94dfc | call/member | `output.split` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:299` |
| op-47ab0572b0a1e2f6 | call/member | `output.splitlines` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:201` |
| op-bee264411de2f68a | call/member | `output.strip` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_start_command.py:21` |
| op-a1d3f51548d74f5c | call/member | `p.exe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:86` |
| op-b5d2c955f72af321 | call/member | `p.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:215` |
| op-9e62fac2705f3762 | call/member | `p.terminate` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/process_api.py:77` |
| op-563586d8e19dd747 | call/member | `p.wait` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:21` |
| op-611d8e0b41f1dd3d | call/member | `painter.drawPixmap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:204` |
| op-0e5ab09bb273cd90 | call/member | `painter.drawText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:173` |
| op-4a277074ec525cc7 | call/member | `painter.end` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:205` |
| op-67916bf0b8b231eb | call/member | `painter.fillRect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:832` |
| op-555195d673ee0b15 | call/member | `painter.setClipPath` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:203` |
| op-9dd766bf7fa89904 | call/member | `painter.setCompositionMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:831` |
| op-3dc1177a2102c75e | call/member | `painter.setPen` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:168` |
| op-fe7ea7f5e574b31e | call/member | `painter.setRenderHint` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:189` |
| op-092e1dba174f548f | call/member | `palette.setColor` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:257` |
| op-acc8d3273899df94 | call/member | `parent.insert` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:90` |
| op-dc3180285585569a | call/member | `parent.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:98` |
| op-d8d05da4919e8095 | call/member | `parsed.netloc.lower` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:240` |
| op-a22a9de307b27a80 | call/member | `parsed.path.lstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:242` |
| op-c34c232f6da1386a | call/member | `parser.add_argument` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:32` |
| op-8058ab7b98cac0f2 | call/member | `parser.parse_args` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:51` |
| op-6f199b989b1f3fed | call/member | `part.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:214` |
| op-8047e6a34149a036 | call/member | `password.encode` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:51` |
| op-e5749dd7b39ca97a | call/member | `password.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:137` |
| op-b5632f39f68fb928 | call/member | `path.arcTo` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:195` |
| op-b087872b61cd970b | call/member | `path.exists` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:595` |
| op-16d5bb765a95a184 | call/member | `path.is_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:785` |
| op-d2facdfa19f6e6e6 | call/member | `path.lineTo` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:196` |
| op-9082d32206526543 | call/member | `path.lstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:177` |
| op-9e1ca86cedb2d5ea | call/member | `path.moveTo` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:194` |
| op-a60d63d41acb2889 | call/member | `path.open` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:262` |
| op-55d53de69f797f21 | call/member | `path.parent.mkdir` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:62` |
| op-0e695f4f11521a87 | call/member | `path.read_bytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:735` |
| op-96d2a3696a38ae04 | call/member | `path.read_text` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:58` |
| op-13bb62cce07453c4 | call/member | `path.relative_to` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:526` |
| op-4309ce9edad4d75f | call/member | `path.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:24` |
| op-4a503e4c19cc7afe | call/member | `path.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:22` |
| op-2fff28dd498b75c1 | call/member | `path.stat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/timestamps.py:12` |
| op-672a68af80ed7685 | call/member | `path.unlink` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:94` |
| op-31fc1cbe99fa6b9e | call/member | `path.write_bytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:742` |
| op-f3e9f02e4c470c5e | call/member | `path.write_text` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:42` |
| op-e82a484e3cd23e35 | call/member | `paths.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:125` |
| op-bd1506941cad98d1 | call/member | `paths.get` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:147` |
| op-053ad55500dbc3f7 | call/member | `paths.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:145` |
| op-c60924d5e896ccf3 | call/member | `pattern.search` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:709` |
| op-d8a286bae029ab59 | call/member | `payload.decode` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:398` |
| op-bc458eaafdeafd80 | call/member | `payload.encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:510` |
| op-b3be88c4476c1816 | call/member | `payload.get` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:27` |
| op-5c3e19de6e3ad6fa | call/member | `payload.model_dump` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:386` |
| op-74f1685a27e8b4b1 | call/member | `payload.removeprefix` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_remote_proxy.py:13` |
| op-6eda8a19227b7ca3 | call/member | `payload.setdefault` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_sync.py:32` |
| op-5d24c2fbadaa11f2 | call/member | `phase_text.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:65` |
| op-0d7f2a82d353bce1 | call/member | `picture.co_detect` | 16 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:50` |
| op-7ec13d72ca23f925 | call/member | `pid_dict.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:106` |
| op-7fb6c23f4253e33c | call/member | `pid_file.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1087` |
| op-f6ec876e8d222c88 | call/member | `pid_file.read_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1089` |
| op-7d1f3fbb17352794 | call/member | `pid_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:40` |
| op-13cdd936c9333c14 | call/member | `pid_list.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:45` |
| op-4da6359387b3f577 | call/member | `pids.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:437` |
| op-719785f1736bffc1 | call/member | `pids.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:273` |
| op-1d8afb80d5fcdb70 | call/member | `pipe_server.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:33` |
| op-33a6bfd44e207af6 | call/member | `pipe_server.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:28` |
| op-ec38568e7185d7b4 | call/member | `pixmap.fill` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:827` |
| op-d8ebcb9db5375ac4 | call/member | `pixmap.height` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:193` |
| op-903f4a65d2dc90d7 | call/member | `pixmap.rect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:832` |
| op-5c8f1df40eebf353 | call/member | `pixmap.size` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:184` |
| op-1f0dd46d37d8ce1e | call/member | `pixmap.width` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:193` |
| op-29fea9ddc84cd0f7 | call/member | `plaintext.decode` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:86` |
| op-2aae400998486c21 | call/member | `point.astype` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/nemu.py:62` |
| op-054af13e75a50017 | call/member | `points.append` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/nemu.py:66` |
| op-163253c9e2bba203 | call/member | `pop_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:128` |
| op-5d40667115b6aeff | call/member | `pos.setX` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:385` |
| op-33f39d36237222ac | call/member | `pos.x` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:385` |
| op-960aa28135248337 | call/member | `position.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:216` |
| op-47493615dac937aa | call/member | `position_id.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/lesson/lesson_affection_student_image_extractor.py:43` |
| op-75402d7c9603a547 | call/member | `positions.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/friend.py:20` |
| op-b09152b77b46b9ed | call/member | `possibleMaterial.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:59` |
| op-2a573c814f9275be | call/member | `possible_strs.index` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/image.py:246` |
| op-ff9fb59271d726f7 | call/member | `preauth_channel.decrypt` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:145` |
| op-921d282b38d2f713 | call/member | `preauth_channel.encrypt` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:138` |
| op-11b4df5856380852 | call/member | `pressed_modifiers.add` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:97` |
| op-00ec7e4b631c346d | call/member | `price_label.setFixedWidth` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:36` |
| op-91f8d5bfab15d035 | call/member | `printer.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:324` |
| op-e764f2d6ab7b0a4f | call/member | `printer.stop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:338` |
| op-37fb738ef6e0c9a1 | call/member | `proc.cmdline` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_start_command.py:13` |
| op-0e1461744e3f1fe1 | call/member | `proc.communicate` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:92` |
| op-f13db7fbf1977bb3 | call/member | `proc.stdout.strip` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:276` |
| op-8bcb98d4f7df5551 | call/member | `proc.wait` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:47` |
| op-a7b83e838308ba6c | call/member | `process_info.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/process_api.py:59` |
| op-20b7d8ad29821de5 | call/member | `process_input.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:11` |
| op-efee49b6c64a80bf | call/member | `process_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:20` |
| op-dace8864ecb652d9 | call/member | `process_name.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:26` |
| op-4546338a97c88e79 | call/member | `projection.get` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:212` |
| op-cf0f27ce6b375d98 | call/member | `proxy.close` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/remote.py:24` |
| op-88bb6e4650c1a71f | call/member | `proxy.run` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_remote_proxy.py:43` |
| op-3f82c0724aecbcd5 | call/member | `proxy.run_endpoint` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/remote.py:21` |
| op-0763da8c664f29db | call/member | `public_key.verify` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:610` |
| op-54dad2f425f6a112 | call/member | `pwhash.argon2id.kdf` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:49` |
| op-31f1a3e85db56881 | call/member | `python.get` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:163` |
| op-43ef0d815014201d | call/member | `python.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:161` |
| op-0dff71cc3e35099f | call/member | `python_path.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:663` |
| op-2de84e886b6c4e72 | call/member | `q.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:155` |
| op-abd9ad1f3cb5b886 | call/member | `qq_combo.addItem` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:149` |
| op-803dabba3792ae8d | call/member | `qq_combo.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:171` |
| op-0b4266289735b8ce | call/member | `qq_combo.itemText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:162` |
| op-8a2b11e4f438d869 | call/member | `qt_language.value.name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:45` |
| op-d0ea8dd1a5830ddd | call/member | `query.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:120` |
| op-3603dbdefde916a7 | call/member | `queue.get` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_provider.py:32` |
| op-f2157a040a3bd758 | call/member | `queue.get_nowait` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/broadcast.py:36` |
| op-3d7c68f0556aac59 | call/member | `queue.put` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:493` |
| op-eea38f5957bee3e8 | call/member | `queue.put_nowait` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/broadcast.py:33` |
| op-f4b83c9ad721c8ab | call/member | `queue_obj.put_nowait` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:87` |
| op-73f8925485ebd934 | call/member | `queues.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:477` |
| op-82163384461f65b8 | call/member | `raw_entry.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:60` |
| op-f9d385e95f4fbc62 | call/member | `raw_entry.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:57` |
| op-d52497b639e8c0c4 | call/member | `raw_line.rstrip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:194` |
| op-ee20eeb4adbe69e3 | call/member | `read_file.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:460` |
| op-e84599c032150de6 | call/member | `reader.readexactly` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:16` |
| op-23b6ce49e1768b0f | call/member | `ready.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:243` |
| op-9982f83aec3617b1 | call/member | `record.get` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:480` |
| op-ccdffdfe0a3c9371 | call/member | `record.getMessage` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:23` |
| op-2df83ae989f5ed4b | call/member | `recorded_y.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:1002` |
| op-e21de465fdb999db | call/member | `records.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:137` |
| op-89e96fa4892b3c07 | call/member | `rect.height` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:196` |
| op-23d71da2e444b6d8 | call/member | `rect.width` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:197` |
| op-b134304e078e2297 | call/member | `ref.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:399` |
| op-4c79b382d57a2a6f | call/member | `ref.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:876` |
| op-2d554a28f86ae005 | call/member | `ref_widget.parent` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:179` |
| op-214524c7bdde7bca | call/member | `region.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:533` |
| op-1313f725b05ad602 | call/member | `region_data.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:67` |
| op-7815f02265b54807 | call/member | `region_data.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:65` |
| op-3ebe300b588cb810 | call/member | `rel_path.as_posix` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:530` |
| op-18132729cfa1bed6 | call/member | `release.set` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_runtime_exit_status.py:37` |
| op-d482292bb3e542fb | call/member | `release.wait` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_runtime_exit_status.py:28` |
| op-13009c33162b2a63 | call/member | `remote.fetch` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:371` |
| op-db7203c5da128a70 | call/member | `remote.list_heads` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:449` |
| op-1993c9bb436573d7 | call/member | `remote.ls_remotes` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:309` |
| op-c7d6128b85f5a04a | call/member | `rename_action.setCheckable` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:252` |
| op-aa541e1313d3eb41 | call/member | `rename_action.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:253` |
| op-c6849cd7075e9a5e | call/member | `rename_dialog.exec_` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:218` |
| op-9a332e09a5942383 | call/member | `rename_dialog.name_input.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:259` |
| op-67abd77588d25136 | call/member | `repo.checkout` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:948` |
| op-d0170163c596855f | call/member | `repo.checkout_tree` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:380` |
| op-1384989c1628f9f0 | call/member | `repo.endswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:249` |
| op-52494bd8fa8896fa | call/member | `repo.lookup_reference` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:945` |
| op-1033960bf2197e80 | call/member | `repo.remotes.create` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:367` |
| op-dec39326fc47580d | call/member | `repo.remotes.create_anonymous` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:307` |
| op-768d6cf3baabf213 | call/member | `repo.remotes.delete` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:536` |
| op-a6116e99ebc1e9f1 | call/member | `repo.remotes.names` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:367` |
| op-babc1dc9cfd92a64 | call/member | `repo.reset` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:379` |
| op-fb98fcd0f3868bb0 | call/member | `repo.revparse_single` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:374` |
| op-ee20fd9832a209b9 | call/member | `repo.walk` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:76` |
| op-afdec2624e929174 | call/member | `repo_result.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:562` |
| op-086df941c1baa66d | call/member | `repositories.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:176` |
| op-029ccfb19826ba0c | call/member | `repositories.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:174` |
| op-76c3bcb94a53568e | call/member | `request.get` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:112` |
| op-b77e284be387cc0b | call/member | `request.handlers.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:12` |
| op-8eded48cfb3487b0 | call/member | `request.translate_html` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:142` |
| op-464cd5bdb9a1b447 | call/member | `request.translate_text` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:93` |
| op-4ff2d19d1774297e | call/member | `request_en.process` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:170` |
| op-9c2ae56d8dcf8aa8 | call/member | `request_ja.process` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:173` |
| op-16efd0fd21ffa7ff | call/member | `request_ko.process` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:176` |
| op-2273209a4c1546df | call/member | `requested_urls.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:70` |
| op-47cfcb5f0708bc69 | call/member | `res.append` | 25 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:524` |
| op-132d210e19f92466 | call/member | `res.argmin` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:80` |
| op-94d09c4640830772 | call/member | `res.count` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:591` |
| op-4fd2f8ef1fd69f01 | call/member | `res.group` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:97` |
| op-957fff6b30cc21fc | call/member | `res.pop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:178` |
| op-ac2d7a328419a3e8 | call/member | `res.sort` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:182` |
| op-fe9df34ccf160307 | call/member | `res.split` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:593` |
| op-761188443a087dc5 | call/member | `res_priority.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:342` |
| op-e34cb9fab67846b2 | call/member | `resource_changes.add` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:533` |
| op-2c08e4712c8ca4f8 | call/member | `resp.json` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/pushkit.py:65` |
| op-fe85ad9cb6a3d542 | call/member | `response.delete_cookie` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:48` |
| op-e128e14a4cd0450e | call/member | `response.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:45` |
| op-e53ac63551b68149 | call/member | `response.headers.get` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:186` |
| op-de6bb38d0a2e47c2 | call/member | `response.iter_content` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:172` |
| op-d08cd707aa8b79fe | call/member | `response.json` | 15 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:157` |
| op-c441350e0ca1604b | call/member | `response.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:63` |
| op-8ee5d4670d260957 | call/member | `response.raise_for_status` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:156` |
| op-3660cd32b48ceea9 | call/member | `response.read` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/update_student_name.py:12` |
| op-15fc50c34e87e9be | call/member | `response.set_cookie` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:34` |
| op-4714d3984e8c4bfb | call/member | `response.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:65` |
| op-9233ce4c3174e60c | call/member | `response_json.get` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:123` |
| op-d4eeeaa89ea702d4 | call/member | `restored_origin.fetch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:903` |
| op-227e657011194911 | call/member | `result.append` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:289` |
| op-e3a1fabe14a71582 | call/member | `result.extend` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:527` |
| op-d925057ff24214e8 | call/member | `result.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/pushkit.py:66` |
| op-3af7b9b3545e7b21 | call/member | `result.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/commands.py:84` |
| op-b793c5c660551c79 | call/member | `result.stdout.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:56` |
| op-833331ca46b6ac72 | call/member | `result.stdout.strip` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:200` |
| op-216c5279a0f0abda | call/member | `results.append` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:474` |
| op-482d7d975f5bffba | call/member | `ret.append` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:265` |
| op-2aca56796bc4f783 | call/member | `ret.startswith` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:17` |
| op-12f897f70b66dd6f | call/member | `ret_queue.put` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:138` |
| op-72ddf3869b7c1a8b | call/member | `revoke_queue.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:175` |
| op-b7a38df0746b78d5 | call/member | `rgb_possibles.pop` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:336` |
| op-2643dc2314be77d7 | call/member | `rgb_reactions.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/picture.py:115` |
| op-a1826c21e7a63a20 | call/member | `root.addHandler` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:81` |
| op-9034eda949d8ce5d | call/member | `root.iter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:81` |
| op-e782b8bd7038e65d | call/member | `root.mkdir` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:24` |
| op-df00d5659c6d89da | call/member | `root.removeHandler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:81` |
| op-0c78e5d28867eaab | call/member | `root.setLevel` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:45` |
| op-a275ece3975aea72 | call/member | `rotated.unlink` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:167` |
| op-506df95ea550909b | call/member | `rounded_pixmap.fill` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:185` |
| op-9a01e2b7c019cde7 | call/member | `router.get` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:58` |
| op-966b85384ab7c3d7 | call/member | `router.post` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:24` |
| op-f2deaa9ac589a0b8 | call/member | `router.websocket` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:25` |
| op-1ada2898bbe1f098 | call/member | `runtime._copy_config_sync` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:204` |
| op-8ff98acffe7b42de | call/member | `runtime._handle_update_signal` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:86` |
| op-b8620a64395926dd | call/member | `runtime._import_config_sync` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:228` |
| op-e70008088b461eff | call/member | `runtime._update_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:86` |
| op-5b8f5165f3919276 | call/member | `runtime.current_status` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:77` |
| op-75653d212e3fb5e1 | call/member | `runtime.remove_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:128` |
| op-e4914519c2a9c12e | call/member | `runtime.require_remote_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:183` |
| op-9409966aa403a585 | call/member | `runtime.solve_task` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_runtime_exit_status.py:94` |
| op-672c56544e548d55 | call/member | `runtime.start_scheduler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_runtime_exit_status.py:35` |
| op-3b518bc01346fd3b | call/member | `runtime.test_all_sha_stream` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:272` |
| op-8e5ff17eb99e7c96 | call/member | `runtime.toggle_android_active_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:110` |
| op-1466d6aa2cb5b05e | call/member | `runtime.update_to_latest` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:301` |
| op-e0ef34eadf5e492b | call/member | `runtime.update_to_latest_stream` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:322` |
| op-ddf0e43f623bfe7e | call/member | `runtime_lib.parent.mkdir` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/ocr/test_android_runtime.py:21` |
| op-fb219089738b9747 | call/member | `runtime_lib.write_bytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/ocr/test_android_runtime.py:22` |
| op-9e2b0ca09f701d89 | call/member | `s.bind` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:90` |
| op-7ea2a41a01a8f8f3 | call/member | `s.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/wsa_support.py:13` |
| op-d009fcbea8337fb8 | call/member | `s.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/wsa_support.py:8` |
| op-4fd21f52fa53b983 | call/member | `s.getsockname` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:194` |
| op-0f5ab8bb84cbfcf2 | call/member | `s.listen` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:193` |
| op-9523ca799cee9dd8 | call/member | `s.recv` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:158` |
| op-c4c0c6e0aae07b7b | call/member | `s.send` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:165` |
| op-d04b4045df05a7ba | call/member | `s.setblocking` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:155` |
| op-386ff4c92bb83143 | call/member | `s_letter_dict.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:178` |
| op-7829ae8715824582 | call/member | `saved.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:198` |
| op-38e56560c2e4fc10 | call/member | `scheduled.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:297` |
| op-cff36918c1e8648b | call/member | `scheduler.getWaitingTaskList` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:72` |
| op-dd939a550fa566a5 | call/member | `scheduler.heartbeat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:73` |
| op-a22d01c27e531220 | call/member | `scheduler.update_valid_task_queue` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:69` |
| op-d26e53f530455b08 | call/member | `scopes.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:135` |
| op-7df4deb7dc666b6e | call/member | `screenshot.screenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/HinaSummerVacationAudioGame.py:68` |
| op-9b5061aface2990e | call/member | `scroll_area.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:231` |
| op-5af40883e3f6563f | call/member | `scroll_area.setHorizontalScrollBarPolicy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:233` |
| op-d868d06c5bff2071 | call/member | `scroll_area.setStyleSheet` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:226` |
| op-8b5337f15dd1a340 | call/member | `scroll_area.setWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:230` |
| op-e2deb677a7e68b2b | call/member | `scroll_area.setWidgetResizable` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:232` |
| op-9444e67728724c4a | call/member | `search_dir.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:723` |
| op-7db2e952273dd038 | call/member | `secure_channel.decrypt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:242` |
| op-39ff994151022bf0 | call/member | `secure_channel.encrypt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:231` |
| op-d2084cff6041d5a6 | call/member | `secure_channel.set_rx_seq` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:241` |
| op-0c50a43c99cd6e8b | call/member | `seen.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:967` |
| op-1fb0cc0347a99158 | call/member | `segment.replace` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:12` |
| op-44fc619c8778b14c | call/member | `selectButton.clicked.connect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:63` |
| op-aae85a26e9472143 | call/member | `selectButton.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:62` |
| op-3b1df9129aa7ca10 | call/member | `selected.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:551` |
| op-fc2fc3f44ac6118f | call/member | `sender.cancel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:30` |
| op-ed5ccb52a23a8574 | call/member | `seq.to_bytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:94` |
| op-70519c85a8964ac3 | call/member | `serial.replace` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:80` |
| op-62012669548935ea | call/member | `serial.rsplit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:351` |
| op-75d6a5a49f43160d | call/member | `serial.split` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:107` |
| op-0cc0e92eaac824a1 | call/member | `serial.startswith` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:205` |
| op-0fddbe9018c7404b | call/member | `server.close` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:140` |
| op-91b4d4607af2d798 | call/member | `server.encrypt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:116` |
| op-528ff54650225df1 | call/member | `server.run` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:78` |
| op-59216e66f5b246b8 | call/member | `server.start` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:58` |
| op-3301717d50ce41da | call/member | `server_file_path.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:307` |
| op-9cf0de5c3023e366 | call/member | `server_lib.start_server` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:312` |
| op-c5bfc7126be2c923 | call/member | `server_private.exchange` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:110` |
| op-2798b733d1bb9a2c | call/member | `server_private.public_key` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:81` |
| op-f6c84a1119d4dad0 | call/member | `serverchan_url.format` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/pushkit.py:26` |
| op-32943efbd9ba1b9e | call/member | `service.conf.initializer.ConfigInitializer._merge_missing_keys` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/initializer.py:126` |
| op-0ef2f35183874c53 | call/member | `service.runtime._AndroidDisplayResizeGuard._serial` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:106` |
| op-a1864ecf574bff96 | call/member | `service.update.repository.FileSystemUtils.copy_directory_structure` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:122` |
| op-949bc3b57e26b4e4 | call/member | `service.update.repository.FileSystemUtils.download_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:544` |
| op-790fc4e22f1a5e58 | call/member | `service.update.repository.FileSystemUtils.unzip_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:547` |
| op-d5c4690cdccaf107 | call/member | `session.baas.send` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:394` |
| op-97742ffb667c8276 | call/member | `session.control_queues.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:228` |
| op-d1843548fb008258 | call/member | `session.control_queues.discard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:235` |
| op-37fae3632881300f | call/member | `session.scrcpy_client.control.swipe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:600` |
| op-aa9bfee4ea4591e2 | call/member | `session.scrcpy_client.control.touch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:597` |
| op-75c798340562371a | call/member | `session.session_id.encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:375` |
| op-537ec122d236f012 | call/member | `session.thread.is_alive` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:379` |
| op-0ff959fc8432522e | call/member | `setup_path.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:33` |
| op-01953684c7cf2cc9 | call/member | `setup_path.open` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:36` |
| op-9e8c2cb170907d0b | call/member | `setup_path.write_text` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:262` |
| op-bfa9db145656c1d3 | call/member | `setup_toml.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:492` |
| op-0ef552d07db2f9f8 | call/member | `shadow.setBlurRadius` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:135` |
| op-8f8aa89b1d03a943 | call/member | `shadow.setColor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:136` |
| op-af6d21ab89e67fdc | call/member | `shadow.setOffset` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:137` |
| op-78b35b0ce810f3ba | call/member | `shell.CreateShortCut` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:226` |
| op-7302c6aaddea8264 | call/member | `signing_file.parent.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:73` |
| op-cf03a6bb6f56e7bd | call/member | `signing_file.read_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:79` |
| op-f7fb638977f85d16 | call/member | `signing_file.write_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:74` |
| op-d661d15176221417 | call/member | `simulator.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:92` |
| op-42102940653d9762 | call/member | `simulator_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:41` |
| op-31ce8c7d1cc10b52 | call/member | `simulator_lists.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:25` |
| op-5b56a87105d623f1 | call/member | `size.isValid` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:823` |
| op-9a8d98c6514284a1 | call/member | `socket_path.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:103` |
| op-6c1e0956d6d0f891 | call/member | `socket_path.parent.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:128` |
| op-36bed1aaeafaa09d | call/member | `socket_path.unlink` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:129` |
| op-0c54bba593de069e | call/member | `socket_path.write_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:100` |
| op-b7ec04d61447b425 | call/member | `sodium_bindings.crypto_secretstream_xchacha20poly1305_init_pull` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:70` |
| op-b040b2f192122936 | call/member | `sodium_bindings.crypto_secretstream_xchacha20poly1305_init_push` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:66` |
| op-384695c6706d63af | call/member | `sodium_bindings.crypto_secretstream_xchacha20poly1305_pull` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:85` |
| op-e12e300c5e177808 | call/member | `sodium_bindings.crypto_secretstream_xchacha20poly1305_push` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:80` |
| op-b2ec751b1d30d0ee | call/member | `sodium_bindings.crypto_secretstream_xchacha20poly1305_state` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:64` |
| op-eb4e6db450c92e31 | call/member | `soup.prettify` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:144` |
| op-b04aeadee909298c | call/member | `source.getparent` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:84` |
| op-7d575feab5c53303 | call/member | `source.is_dir` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:753` |
| op-844916d2692bb79e | call/member | `source.iterdir` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:217` |
| op-aef0c568ecabb957 | call/member | `source.rglob` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:784` |
| op-c87b3a529aaeb2bf | call/member | `source_dir.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:111` |
| op-d60b88cec88b64eb | call/member | `source_dir.is_dir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:111` |
| op-0b936e811247a3cc | call/member | `source_file.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:152` |
| op-c4dbc6a873d1948c | call/member | `source_root.iterdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:639` |
| op-8d587129306b5faf | call/member | `sources.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:254` |
| op-2501736aed4935a3 | call/member | `spinner.fail` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:955` |
| op-f132cae55b3d8690 | call/member | `spinner.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:936` |
| op-e22994135866c856 | call/member | `spinner.succeed` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:952` |
| op-fe5c7c5b0c13b58a | call/member | `st.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:473` |
| op-add32fd933473df8 | call/member | `st.replace` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:7` |
| op-d79767a8d68ffab4 | call/member | `st.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:12` |
| op-1688bf0d66dc92f8 | call/member | `stage_data.items` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:120` |
| op-bbdbb076b2c4e19e | call/member | `state.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:1005` |
| op-abb69803c20f2e9c | call/member | `status.count` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:296` |
| op-e840939aeaddb4ee | call/member | `status.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:980` |
| op-329e126ea489b352 | call/member | `status_item.setForeground` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:516` |
| op-9105f4c4ab5223ef | call/member | `status_item.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:515` |
| op-afe9b82dd5d73047 | call/member | `story.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:52` |
| op-d90810b3e76d5a09 | call/member | `story.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:52` |
| op-0feb3886c37ecb63 | call/member | `stream.decrypt` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:85` |
| op-6b30291851b81887 | call/member | `stream.encrypt` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:76` |
| op-40c62180dc7bf5ab | call/member | `stream.getvalue` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:76` |
| op-a3c590c09850641f | call/member | `stream.write` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:34` |
| op-2992d3c0218014bf | call/member | `stream_box.init_pull` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:245` |
| op-4b663c87d8d42e23 | call/member | `string.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/process_api.py:11` |
| op-7a59d8bb783aaa14 | call/member | `string_len.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:154` |
| op-1f6f49e9a9b67cc7 | call/member | `string_letter_dict.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:153` |
| op-482006574d5aa5d6 | call/member | `sub_layout.count` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:104` |
| op-de31f083a3f635c6 | call/member | `sub_layout.removeItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:110` |
| op-47dfde422079d5ab | call/member | `sub_layout.takeAt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:105` |
| op-7b73c300bb9e8907 | call/member | `sub_layout_0.addStretch` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:137` |
| op-5a8cbce73fd99915 | call/member | `sub_layout_0.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:146` |
| op-a7733f788ee2b60d | call/member | `sub_layout_1.addStretch` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:117` |
| op-92a7d526a5c36be6 | call/member | `sub_layout_1.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:118` |
| op-ea9bb327c2c20792 | call/member | `subtitle.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:132` |
| op-51008665d6090eec | call/member | `subtitle.setStyleSheet` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:133` |
| op-ba43bcb0ea16ae1e | call/member | `subtitle.setWordWrap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:131` |
| op-41225a9bf0db83eb | call/member | `switchBtn.checkedChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:213` |
| op-234938076ae7af45 | call/member | `switchBtn.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:210` |
| op-c30696254f8e6a6c | call/member | `sync.pull_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:390` |
| op-1ca878cb315bdaaa | call/member | `sync.stat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:386` |
| op-05b8f61b7126a763 | call/member | `t.append` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:131` |
| op-7c059030462e58a2 | call/member | `t.isdigit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:42` |
| op-9db024534cbac975 | call/member | `t.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:52` |
| op-54face09e44efc7e | call/member | `t.set_ocr` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:54` |
| op-fdb7405dd25d1d80 | call/member | `t.sort` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:522` |
| op-aa1af6892bad388e | call/member | `t.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:66` |
| op-4e26e70a027b7162 | call/member | `t1.join` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:154` |
| op-ee99f163b6ea97df | call/member | `t1.start` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:144` |
| op-daa8fa94e79f26e2 | call/member | `t_cbx.setChecked` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:31` |
| op-de703872b8345982 | call/member | `t_cbx.stateChanged.connect` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:46` |
| op-0b4e6a98a9c96ae5 | call/member | `t_cfbs.clicked.connect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:148` |
| op-26b30e4eeb1eb686 | call/member | `t_daemon.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:94` |
| op-9a77341619d9882c | call/member | `t_ncs.setClearButtonEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:138` |
| op-ae7e038a771ef6c7 | call/member | `t_ncs.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:139` |
| op-d8d1c1775162961b | call/member | `t_ncs.textChanged.connect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:140` |
| op-726fcb124141dca2 | call/member | `tableView.horizontalHeader` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:70` |
| op-8ffe8673ae14141a | call/member | `tableView.setAlternatingRowColors` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:83` |
| op-9d16a6e6930df51d | call/member | `tableView.setCellWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:92` |
| op-d290bc04eb430373 | call/member | `tableView.setColumnCount` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:69` |
| op-f87cd1cc86c6f796 | call/member | `tableView.setColumnWidth` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:73` |
| op-90b3676b80e9d20d | call/member | `tableView.setCornerButtonEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:86` |
| op-6a6abfb225e2c50e | call/member | `tableView.setEditTriggers` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:79` |
| op-a023ca0ad46dd25f | call/member | `tableView.setFixedHeight` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:87` |
| op-8a031081f9314bb6 | call/member | `tableView.setGridStyle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:85` |
| op-90e7c745cab1c034 | call/member | `tableView.setHorizontalHeaderLabels` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:71` |
| op-d45dc8c83c4deeab | call/member | `tableView.setItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:78` |
| op-dba042a3c694f89a | call/member | `tableView.setRowCount` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:68` |
| op-a0485a516bfb895c | call/member | `tableView.setSelectionBehavior` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:80` |
| op-8dd524ac55fd8b31 | call/member | `tableView.setSelectionMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:81` |
| op-faa76a581fe56e57 | call/member | `tableView.setShowGrid` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:84` |
| op-2e052fa207a35179 | call/member | `tableView.setSortingEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:82` |
| op-4f04a4fbbc161476 | call/member | `tableView.setWordWrap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:67` |
| op-8e6d5b4d42b92d8b | call/member | `tail.split` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:244` |
| op-ce04dd5209955042 | call/member | `target.currentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:145` |
| op-77da344891552881 | call/member | `target.endswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:145` |
| op-2eeefa5c6a874cb5 | call/member | `target.exists` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:85` |
| op-58cc648d791aefbc | call/member | `target.isChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:147` |
| op-cf0e1ecc03a44a19 | call/member | `target.is_dir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:941` |
| op-04ce0f02999ab0ac | call/member | `target.mkdir` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:216` |
| op-431acb788c5df1b7 | call/member | `target.text` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:149` |
| op-098d5fc388718496 | call/member | `target.unlink` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:86` |
| op-3f41c2e21b4c246b | call/member | `target_cmdline.replace` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:103` |
| op-a56b4a83a04393df | call/member | `target_cwd.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:318` |
| op-361c600be9d4f1d2 | call/member | `target_dict.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:67` |
| op-f0c0033c5f45babf | call/member | `target_dir.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:120` |
| op-06b26267b9c401d2 | call/member | `target_file.parent.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:841` |
| op-6fa5fa2a5322e47f | call/member | `target_file.write_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:842` |
| op-4a079d984273507a | call/member | `target_path.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:221` |
| op-a046d39c1e65f2bb | call/member | `task.add_done_callback` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:49` |
| op-be6e1a0844f8fd2b | call/member | `task.cancel` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:87` |
| op-080e3cb8e91b0d6d | call/member | `task.done` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:663` |
| op-054ac0504f50b659 | call/member | `task.exception` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:556` |
| op-f38005c11ea70140 | call/member | `task.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:33` |
| op-113eb952660e3e96 | call/member | `task.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:30` |
| op-d4efe6f56e45c2bd | call/member | `task_data_name.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:68` |
| op-c8ba7e1aa238d041 | call/member | `task_string.count` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/sweep_task.py:183` |
| op-28b08ec13f582120 | call/member | `task_string.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/sweep_task.py:190` |
| op-f1a5e01ccb720d48 | call/member | `task_with_log_info.append` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:465` |
| op-ced486c852c7d1d5 | call/member | `tasklist.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:67` |
| op-3096bc5591d24161 | call/member | `tasks.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:592` |
| op-08ffd12dc72462a8 | call/member | `tasks.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:27` |
| op-0f4f1e541129e913 | call/member | `team.endswith` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:148` |
| op-e961f4c628821277 | call/member | `team_config.items` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:421` |
| op-146a7902574102fd | call/member | `teams.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:48` |
| op-f692a1ecbd176e04 | call/member | `temp.addWidget` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:195` |
| op-7c71451b4dd4ff5f | call/member | `temp.append` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:135` |
| op-81335ff2eb58731f | call/member | `temp.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/position.py:40` |
| op-03318d4d7d71786f | call/member | `temp.sort` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:185` |
| op-e0ba7cce4c3a38fe | call/member | `temp.split` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:929` |
| op-5109684d1262cf84 | call/member | `temp1.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:667` |
| op-214bad440184107a | call/member | `temp1.sort` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:669` |
| op-62b827454bfdd7b6 | call/member | `temp2.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:660` |
| op-7b46e23d3748812e | call/member | `temp2.sort` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:670` |
| op-7391f9663918a693 | call/member | `temp_clone_path.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:800` |
| op-1ff5b4088843be8a | call/member | `temp_clone_path.iterdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:813` |
| op-1cd646500dbcc0b0 | call/member | `temp_handler.clone` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:553` |
| op-2853ac1165e1cb8f | call/member | `temp_path.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:549` |
| op-9cb505e229370109 | call/member | `temp_price.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:21` |
| op-97a52281036a4e0f | call/member | `temp_repo_path.iterdir` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:484` |
| op-4bd2ac9fa45d9900 | call/member | `temp_repo_path.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:323` |
| op-d7c1f487f2b03674 | call/member | `template.format` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:167` |
| op-5df089426f0d8dfb | call/member | `templates.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:513` |
| op-c39f1338ecccc78d | call/member | `text.encode` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:58` |
| op-6d12e5423d6a2966 | call/member | `text.replace` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:120` |
| op-8c67667744751b78 | call/member | `text.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:171` |
| op-340f9c7eba8c3b51 | call/member | `text_dict.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:329` |
| op-a5462c980aeb7d18 | call/member | `text_layout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:377` |
| op-3ae6e94609d804af | call/member | `theme.value.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/style_sheet.py:14` |
| op-40228fb9e3c1a909 | call/member | `then_signal.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:108` |
| op-1ec00bc14dc43f70 | call/member | `thread.is_alive` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy_client.py:54` |
| op-f6d35544fcf14e20 | call/member | `thread.join` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:581` |
| op-f4049f99f4e3a3f2 | call/member | `thread.last_frame.copy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy_client.py:49` |
| op-a1f3c56cad3453e6 | call/member | `thread.start` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:578` |
| op-fc268bdfbdaced11 | call/member | `time.sleep` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `module/activities/activity_utils.py:476` |
| op-c22c6a83f528662b | call/member | `time_deltas.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:499` |
| op-349dca557c108277 | call/member | `timer.cancel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:24` |
| op-f09c8731ff21942f | call/member | `timer.is_alive` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:23` |
| op-4413c6e9f2e70fb7 | call/member | `timer.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:27` |
| op-737d8eb3f5b17579 | call/member | `times.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:551` |
| op-22127e7974e0759a | call/member | `timestamp.astimezone` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:119` |
| op-9053a4c29d931b41 | call/member | `title.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:125` |
| op-8f1efd3a63f46b0f | call/member | `title.setStyleSheet` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:126` |
| op-8fb1bd1b4b92bcbc | call/member | `tmp.iterdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:700` |
| op-6dc1f934151ce6d2 | call/member | `token.split` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:390` |
| op-1c5b25eeeac8892c | call/member | `token.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:110` |
| op-ed04beee169b001d | call/member | `token_id.encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:437` |
| op-ce1a075716dbf893 | call/member | `top_card_widget.adjustSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:153` |
| op-df93cb3da813e924 | call/member | `top_card_widget.height` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:185` |
| op-d0fdfd2cbb8ec8cf | call/member | `top_card_widget.setFixedHeight` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:185` |
| op-621718af0348828f | call/member | `total_list.extend` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:48` |
| op-3d7209b7cf31bacc | call/member | `tp.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:22` |
| op-cbf8f150dcce00a7 | call/member | `tp.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:119` |
| op-f2317ec4a7db177f | call/member | `translations.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:119` |
| op-d5564ccb79360d1b | call/member | `tree.getroot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:78` |
| op-173b28fdebdf8b55 | call/member | `tree.write` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:99` |
| op-ae5b8f917b4ed22f | call/member | `triangle.x_bounds` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/geometry/triangle.py:67` |
| op-c9a34d895f9aa38f | call/member | `u2.app_current` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:420` |
| op-7811ec1e20bc78ff | call/member | `u2.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:473` |
| op-0ea31ee23999caf4 | call/member | `u2_client.get_connection` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_local_device.py:12` |
| op-f9229952829da54f | call/member | `uiautomator.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_local_device.py:15` |
| op-031659ef1e98a48e | call/member | `unable_to_fight_formation.all` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:132` |
| op-833be2ddd9ce10e4 | call/member | `unavailable.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:125` |
| op-886446102a9bed41 | call/member | `unchecked.remove` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:224` |
| op-aa4e09d05399be9a | call/member | `unit_component.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:676` |
| op-67e095dfb36d8107 | call/member | `unit_component.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:678` |
| op-a15508368990094a | call/member | `unit_component.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:686` |
| op-9e6571950e077d62 | call/member | `unit_component.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:677` |
| op-a6bfc60886e703fe | call/member | `unit_component.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:682` |
| op-a7a21532a61fe1c7 | call/member | `unit_component.stateChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:687` |
| op-7a0b54a028159c9e | call/member | `unit_component.textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:683` |
| op-5e6b2f3cebfe629f | call/member | `unit_layout.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:385` |
| op-298031e5f3bc98ed | call/member | `unit_layout.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:369` |
| op-21efb0895d11c7e8 | call/member | `unit_layout.setContentsMargins` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:364` |
| op-44ad59607b090c82 | call/member | `unit_layout.setSpacing` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:363` |
| op-c9427a29130d9f5d | call/member | `unit_widget.setLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:390` |
| op-9f23f28b246591d1 | call/member | `unread_location.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:178` |
| op-fa360f92dc51d4ef | call/member | `updated.insert` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/update_student_name.py:47` |
| op-16169d7612616cf4 | call/member | `updater.get_latest_version` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:387` |
| op-b570aebd4bbe4d87 | call/member | `updater.run` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1138` |
| op-4179408f3f17285f | call/member | `upstream.headers.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:141` |
| op-f81353b79cf54edb | call/member | `upstream.raise_for_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:137` |
| op-3f356ddfefdd15b4 | call/member | `upstream_backup.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:906` |
| op-a659aa26ce931b17 | call/member | `upstream_ref.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:911` |
| op-092633677b49550b | call/member | `url.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:233` |
| op-c1bce9e9f2efe4f9 | call/member | `url.split` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:182` |
| op-dfd1f034a94e2d61 | call/member | `use_acc_ticket_switch.checkedChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:105` |
| op-b13dfa748b0f6f29 | call/member | `use_acc_ticket_switch.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:104` |
| op-bdf24e9a3043c6b4 | call/member | `user32.SystemParametersInfoA` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:69` |
| op-b1b50b68aa82af35 | call/member | `v.disconnect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:166` |
| op-e0c2e96ecf318167 | call/member | `vBox.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:23` |
| op-2a35cc5b1539649e | call/member | `value.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:185` |
| op-af84cb144e361069 | call/member | `value.encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/crypto.py:26` |
| op-003572b267747c93 | call/member | `value.endswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/bluestacks_module.py:16` |
| op-23c6a80c84395564 | call/member | `value.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:194` |
| op-66980f250c0fdc14 | call/member | `value.isdigit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:719` |
| op-58d49359b1894acf | call/member | `value.lstrip` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_native.py:42` |
| op-7b47131a220f72f2 | call/member | `value.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:198` |
| op-2a12b6c492cc19e8 | call/member | `value.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:397` |
| op-13b581659a28199e | call/member | `value.startswith` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:184` |
| op-10f41c384dc91778 | call/member | `value.strip` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:17` |
| op-07d34ebba5fe9739 | call/member | `venv_path.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:658` |
| op-6051e3566d6a3369 | call/member | `w.setFocus` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:671` |
| op-f7fc4251f0efe386 | call/member | `w.setMicaEffectEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:668` |
| op-9db0ffd613c65f09 | call/member | `w.show` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:634` |
| op-af78005908f6c248 | call/member | `waiting.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:968` |
| op-f3d2eeeeb2441bcd | call/member | `watch_paths.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:493` |
| op-b82732aef744fd4b | call/member | `websocket.accept` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:103` |
| op-2f79854f4b7de381 | call/member | `websocket.close` | 11 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:101` |
| op-26900fba9871a89e | call/member | `websocket.cookies.get` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:127` |
| op-28ddbae19bf53f78 | call/member | `websocket.headers.get` | 2 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:100` |
| op-27ececae4abfe98c | call/member | `websocket.receive` | 1 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/remote/scrcpy.py:476` |
| op-6b2d028e6e0e7a73 | call/member | `websocket.receive_bytes` | 2 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:84` |
| op-a633cb8247a0cafb | call/member | `websocket.receive_json` | 6 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:104` |
| op-0908b4f33ac7d9f9 | call/member | `websocket.send_bytes` | 3 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:76` |
| op-2a51646a6f18801f | call/member | `websocket.send_json` | 8 | service.transport-auth | Service Platform | `baas::service::ProtocolHost` | PARITY-SERVICE-PROTOCOL | INVENTORIED | `service/api/security.py:106` |
| op-17f1daffc7dad360 | call/member | `what_is_mirrorc_button.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:241` |
| op-c144d4462930c551 | call/member | `widget.adjustSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:190` |
| op-857e7d5b5de38b95 | call/member | `widget.children` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:254` |
| op-a1237fc4c064c908 | call/member | `widget.deleteLater` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:229` |
| op-8c8d1ecc25ada53b | call/member | `window_info.is_valid_window` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:214` |
| op-97dc59d51cf627d4 | call/member | `worker.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:507` |
| op-4264f018d97fecb0 | call/member | `worker.test_completed.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:506` |
| op-fa19bf55a02bbd83 | call/member | `wrapper.addLayout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:42` |
| op-8f1f505dde8abdab | call/member | `wrapper.addStretch` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:181` |
| op-882abb9f9bc7347f | call/member | `wrapper.addWidget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:43` |
| op-62081daf5e26431f | call/member | `wrapper.widget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/shopPriority.py:45` |
| op-ac3e210a35d1713a | call/member | `wrapper_input.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:187` |
| op-23c3d49c398c18bb | call/member | `wrapper_input.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:188` |
| op-abf3a85cb59ec6b8 | call/member | `wrapper_widget.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:256` |
| op-cfda6039fc706883 | call/member | `wrapper_widget.setLayout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:44` |
| op-8ba4a7c0cf1d7a2d | call/member | `writer.close` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:50` |
| op-9994559fa98c2fe1 | call/member | `writer.drain` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:32` |
| op-df19953d57ac377a | call/member | `writer.wait_closed` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:120` |
| op-8536e4faefb757d3 | call/member | `writer.write` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:31` |
| op-d356ea095409c3fa | call/member | `ws.received_json.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:155` |
| op-1954595078ba0c0d | call/member | `x.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:560` |
| op-7c1c91e3850ddb3d | call/member | `x.local.startswith` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:358` |
| op-a9b143b9e061b97c | call/member | `x.text` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:87` |
| op-eef61324558cb1fc | call/member | `x_y_range.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:615` |
| op-6c2c03f7908a371c | call/member | `zip_ref.extractall` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:206` |
| op-aec17c102c025462 | call/name | `ActiveSession` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:361` |
| op-5cfe87ad7a8e5314 | call/name | `AuthenticationError` | 34 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:41` |
| op-72af42f96e8c8103 | call/name | `BroadcastChannel` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:199` |
| op-e7e4280119370dab | call/name | `ChannelClosed` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:29` |
| op-5dcce9f9c58ead9b | call/name | `Component` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:205` |
| op-6818fd159df2a9ed | call/name | `ConfigInitializer` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:296` |
| op-042648cc941a7fb8 | call/name | `ConfigManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:55` |
| op-f86405de795d939b | call/name | `ControlSender` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:97` |
| op-0703172e297143e7 | call/name | `EndpointAdapter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy_proxy.py:49` |
| op-e3c973337174e143 | call/name | `FakeAdb` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:164` |
| op-3b238c673d35ff13 | call/name | `FakeBaas` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/module/test_cafe_reward_match.py:107` |
| op-bf5aec1769bb59e4 | call/name | `FakeClient` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:89` |
| op-c04507a569a282a2 | call/name | `FakeRemote` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:22` |
| op-88be459717b7ca15 | call/name | `FakeRemotes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:25` |
| op-551d052e38537416 | call/name | `FakeRepo` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:33` |
| op-1d508f991f5d7656 | call/name | `FakeResponse` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_update_checks.py:71` |
| op-55c2ff3cde8074a3 | call/name | `FrameDecoder` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:36` |
| op-92a162e638066497 | call/name | `HandshakeContext` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:114` |
| op-6872d7ab65152ca0 | call/name | `JsonChaChaChannel` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:131` |
| op-42df8e4ba41707ef | call/name | `LogManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:57` |
| op-6d830100b5bbbd9c | call/name | `Logger` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_injection.py:31` |
| op-47945ccdd633efc5 | call/name | `PasswordState` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:466` |
| op-4a0e4ded9dccd58f | call/name | `PipeChannelEndpoint` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:42` |
| op-2b8b93365999def7 | call/name | `PipeTransportServer` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:27` |
| op-6d429e8e3aaa0a04 | call/name | `Progress` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:300` |
| op-09a4aa7161f103e5 | call/name | `RememberedLogin` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:198` |
| op-2d8b475707a70396 | call/name | `ResourcePathResolver` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:37` |
| op-2c439460581d1cb0 | call/name | `RichFormatter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:41` |
| op-e4a7d1040d06732b | call/name | `RichHandler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:40` |
| op-f0e215615e580793 | call/name | `ScrcpyClient` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:574` |
| op-4010cd6a9487b3d8 | call/name | `SecretStreamBox` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:299` |
| op-56efd37ee4819b10 | call/name | `ServiceAuthManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:54` |
| op-02ffe1bd08df88e1 | call/name | `ServiceRuntime` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:56` |
| op-059928f0d11dcba1 | call/name | `_LowLevelKeyboardProc` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:87` |
| op-fb7c3b2c4faf1634 | call/name | `_Translator` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:50` |
| op-a4d78b97427e0b2b | call/name | `_dedupe_happy_face_points` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:560` |
| op-415bebb4c02c1d01 | call/name | `_get_happy_face_templates` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:539` |
| op-261a1defb73b978f | call/name | `_notify` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/notification.py:28` |
| op-8bcf66c302ea5452 | call/name | `_resize_for_happy_face_match` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:516` |
| op-c4f0ad00fecc2a1c | call/name | `_toast` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/notification.py:38` |
| op-28fdab4d1bdda4b9 | call/name | `adb_path` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:329` |
| op-7e3b5d19c8e58ec9 | call/name | `apply_service_injections` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:285` |
| op-a0cc5abae47d0e16 | call/name | `argon2` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:465` |
| op-3994aae0c5309f73 | call/name | `auto_scan_simulators` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/start_simulator.py:15` |
| op-c155a711ed695a38 | call/name | `b64d` | 15 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:48` |
| op-cd9d1979eb521a93 | call/name | `b64e` | 14 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:36` |
| op-94bf197cb0fde7b5 | call/name | `bar` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:704` |
| op-71fe3e0d2a9859a2 | call/name | `begin_server_hello` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:32` |
| op-be025887a7c4c612 | call/name | `bst_read_registry_key` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/bluestacks_module.py:84` |
| op-dfed804ad20449b6 | call/name | `builtins.AssertionError` | 7 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `tests/core/ocr/test_android_runtime.py:33` |
| op-d09eb5da46ae2242 | call/name | `builtins.ConnectionError` | 8 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/scrcpy/core.py:125` |
| op-77a1a41e5e91fc96 | call/name | `builtins.EnvironmentError` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:422` |
| op-236e5f7eef656d49 | call/name | `builtins.Exception` | 24 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:310` |
| op-84e0a0ce90dde2cf | call/name | `builtins.FileNotFoundError` | 24 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/config/config_set.py:27` |
| op-a11226732fa28d29 | call/name | `builtins.ImportError` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `gui/util/hotkey_manager.py:42` |
| op-6b47500183d2570f | call/name | `builtins.KeyError` | 4 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `gui/util/hotkey_manager.py:275` |
| op-59bfd070a3a92fa7 | call/name | `builtins.NotImplementedError` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `deploy/installer/_installer.py:601` |
| op-2ad0cc349355f4ed | call/name | `builtins.OSError` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `deploy/installer/installer.py:68` |
| op-63599bbede8769cf | call/name | `builtins.RuntimeError` | 47 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:415` |
| op-9b56327c46cde389 | call/name | `builtins.TypeError` | 3 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/conf/initializer.py:70` |
| op-c54cdef2d68dd9dc | call/name | `builtins.ValueError` | 70 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/Control.py:42` |
| op-e058f7dbdfd24246 | call/name | `builtins.abs` | 33 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:1084` |
| op-672c0bd59dca262a | call/name | `builtins.any` | 11 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `module/explore_tasks/explore_task.py:65` |
| op-902882ec9c588561 | call/name | `builtins.bool` | 13 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/api/http.py:359` |
| op-230aa6eda0ee296c | call/name | `builtins.bytearray` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/transport/framing.py:30` |
| op-98f33f6c5807d480 | call/name | `builtins.bytes` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/transport/framing.py:46` |
| op-44685e234223d992 | call/name | `builtins.callable` | 2 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `gui/util/hotkey_manager.py:264` |
| op-e739c65bfb0b7fda | call/name | `builtins.chr` | 2 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `gui/util/hotkey_manager.py:251` |
| op-a258eadc89b4ac66 | call/name | `builtins.dict` | 23 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/nemu_client.py:151` |
| op-7941e7a10b554ca8 | call/name | `builtins.divmod` | 2 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/nemu_client.py:396` |
| op-c604db7bd786b9e5 | call/name | `builtins.enumerate` | 29 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/connection.py:119` |
| op-2f699c039076eb8a | call/name | `builtins.eval` | 3 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `gui/components/expand/expandTemplate.py:182` |
| op-489d4e4420dbac49 | call/name | `builtins.filter` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/remote/scrcpy.py:361` |
| op-0e74284728e0d1f8 | call/name | `builtins.float` | 18 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:722` |
| op-42f868d94a07e224 | call/name | `builtins.frozenset` | 2 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `gui/util/hotkey_manager.py:105` |
| op-8aa28f2470c320e8 | call/name | `builtins.getattr` | 62 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:193` |
| op-bb3ab6213ea91f46 | call/name | `builtins.hasattr` | 7 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/config/config_set.py:63` |
| op-d5c84ce9cf6aa0dc | call/name | `builtins.input` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `develop_tools/explore_task_data_generator.py:14` |
| op-f785a2d2163e72cc | call/name | `builtins.int` | 192 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:163` |
| op-1ffab04152221519 | call/name | `builtins.isinstance` | 71 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/connection.py:42` |
| op-4d698f1cd5c3247b | call/name | `builtins.iter` | 3 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `module/lesson.py:570` |
| op-080d709e28674f47 | call/name | `builtins.len` | 344 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:143` |
| op-f331fa7959f384a0 | call/name | `builtins.list` | 45 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/connection.py:290` |
| op-ad93d9f9afea7dcc | call/name | `builtins.map` | 8 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/uiautomator2_client.py:165` |
| op-0acece8c59c9ce7e | call/name | `builtins.max` | 51 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:161` |
| op-a33b19e7f60bee03 | call/name | `builtins.min` | 46 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:163` |
| op-1b1b4457737394ea | call/name | `builtins.next` | 11 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `deploy/installer/_installer.py:151` |
| op-0c8733a3b5164cd5 | call/name | `builtins.object` | 7 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/transport/pipe_endpoint.py:11` |
| op-8b07a7ece927e419 | call/name | `builtins.open` | 94 | resource.filesystem-json | Runtime Resources | `baas::script::host::ResourceHost` | PARITY-RESOURCE-HOST | INVENTORIED | `core/Baas_thread.py:403` |
| op-062886aa0f19c0ba | call/name | `builtins.ord` | 7 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/ocr/ocr.py:79` |
| op-9328c31c2328b611 | call/name | `builtins.print` | 78 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:898` |
| op-740d1b958622082a | call/name | `builtins.range` | 260 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `cli.example.py:44` |
| op-2560305e4aa0323c | call/name | `builtins.repr` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `service/system_logging.py:103` |
| op-f16dd2637f2256e3 | call/name | `builtins.round` | 13 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/color.py:18` |
| op-8ebb449867c62cc3 | call/name | `builtins.set` | 19 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `develop_tools/config_translation_generator.py:39` |
| op-5cdd38e1bb2342dc | call/name | `builtins.setattr` | 5 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:720` |
| op-94a6c0d63fa33f3e | call/name | `builtins.sorted` | 14 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/connection.py:290` |
| op-1933dee04bf5947f | call/name | `builtins.staticmethod` | 2 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `tests/service/test_android_display_resize.py:121` |
| op-35d1c462221bde19 | call/name | `builtins.str` | 433 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:143` |
| op-5d39a8d2efaa59ea | call/name | `builtins.sum` | 2 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `module/activities/activity_utils.py:103` |
| op-be988ebae9cb4cec | call/name | `builtins.super` | 101 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/config/config_set.py:17` |
| op-1092eb919f4e303e | call/name | `builtins.tuple` | 1 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `develop_tools/explore_task_data_generator.py:77` |
| op-f3a1ead91712767f | call/name | `builtins.type` | 57 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/Baas_thread.py:717` |
| op-7daaeee1303283c1 | call/name | `builtins.zip` | 8 | language.intrinsic | Script Runtime | `baas::script::runtime::ValueIntrinsic` | PARITY-LANGUAGE-INTRINSIC | INVENTORIED | `core/device/emulator_manager/auto_scan_simulator.py:84` |
| op-85af595f8e511688 | call/name | `call_next` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/app.py:47` |
| op-5e07ee8a412548f8 | call/name | `callback` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:266` |
| op-8cdba5634c4ecc07 | call/name | `canonical_dumps` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:30` |
| op-f999f783ec33843d | call/name | `cli.example.arena_tasks` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:57` |
| op-ef53b8791e287f4a | call/name | `cli.example.execute_task` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:40` |
| op-8b942498128afc14 | call/name | `cli.example.main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:60` |
| op-2d07eecd783dd085 | call/name | `cli.example.regular_tasks` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `cli.example.py:57` |
| op-685bdf1a13625733 | call/name | `configure_system_logging` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:47` |
| op-24af64b3c85f2717 | call/name | `control_heartbeat_sender` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:54` |
| op-fa721d0c7c58655a | call/name | `control_sender` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:47` |
| op-3a099c5419c1d258 | call/name | `convert_team_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:339` |
| op-a2a0489710640cc8 | call/name | `cookie_secure` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:39` |
| op-e8c11b041df68866 | call/name | `core.color._get_rgb_at_index` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/color.py:44` |
| op-4f20563a3e71f7e6 | call/name | `core.color._pixel_in_rgb_range` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/color.py:47` |
| op-d76bf83319dd5711 | call/name | `core.color.calcTotalRGBDiff` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/color.py:141` |
| op-fbaa719e9659a18d | call/name | `core.color.match_any_rgb_in_feature` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/color.py:124` |
| op-0b1d781e43334539 | call/name | `core.color.match_rgb_feature` | 7 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/color.py:16` |
| op-bc3c07a27cfdfb03 | call/name | `core.color.rgb_in_range` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/color.py:72` |
| op-a975f97907f7f6f8 | call/name | `core.device.control.nemu.insert_swipe` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/nemu.py:121` |
| op-54bf7ece05795ae9 | call/name | `core.device.control.nemu.random_normal_distribution` | 3 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/nemu.py:18` |
| op-266a2f504e67c967 | call/name | `core.device.control.nemu.random_rho` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/nemu.py:45` |
| op-eb1128a3e29ad1d9 | call/name | `core.device.control.nemu.random_theta` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/nemu.py:45` |
| op-961c19e23c253a26 | call/name | `core.device.control.pyautogui.PyautoguiControlError` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/pyautogui.py:50` |
| op-942ee487cad951e9 | call/name | `core.device.control.pyautogui._init` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/pyautogui.py:15` |
| op-8b1f46f974192f77 | call/name | `core.device.control.pyautogui.get_mouse_sensitivity` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/control/pyautogui.py:32` |
| op-a7600bc2486a6c24 | call/name | `core.device.emulator_manager.auto_scan_simulator.auto_scan_simulators` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/auto_scan_simulator.py:80` |
| op-b1900ba2c2dbf8cf | call/name | `core.device.emulator_manager.auto_scan_simulator.check_simulator` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/auto_scan_simulator.py:38` |
| op-dacfacb30eac8651 | call/name | `core.device.emulator_manager.auto_scan_simulator.get_running_processes` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/auto_scan_simulator.py:34` |
| op-1a6f2a7d69caa733 | call/name | `core.device.emulator_manager.bluestacks_module.find_display_name` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/bluestacks_module.py:42` |
| op-a1c70a70c1d2860d | call/name | `core.device.emulator_manager.bluestacks_module.read_registry_key` | 3 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/bluestacks_module.py:42` |
| op-b7a43d66bf9bb61d | call/name | `core.device.emulator_manager.process_api.extract_args` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/process_api.py:41` |
| op-d2eb7cbcd041db69 | call/name | `core.device.emulator_manager.process_api.is_running` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/process_api.py:74` |
| op-d9f2b34ff2cc2b9f | call/name | `core.device.emulator_manager.process_api.match_lists` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/process_api.py:65` |
| op-f46a2c3788571c88 | call/name | `core.device.emulator_manager.stop_simulator.bst_read_registry_key` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/emulator_manager/stop_simulator.py:25` |
| op-6c70b906ccec9c15 | call/name | `core.device.nemu_client.CaptureNemuIpc` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/nemu_client.py:294` |
| op-671fda4117e2fcd7 | call/name | `core.device.nemu_client.NemuClient` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/nemu_client.py:159` |
| op-c4d2828ed9d1c1b7 | call/name | `core.device.nemu_client.NemuIpcError` | 7 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/nemu_client.py:140` |
| op-38d89f23a524201f | call/name | `core.device.nemu_client.NemuIpcIncompatible` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/nemu_client.py:135` |
| op-6e9e0080cd194758 | call/name | `core.device.scrcpy.control.inject` | 11 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/scrcpy/control.py:35` |
| op-1f78574cd77d8f1c | call/name | `core.device.scrcpy_client.ScrcpyClient` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/scrcpy_client.py:32` |
| op-6e90c8909f04e7cd | call/name | `core.device.scrcpy_client.ScrcpyError` | 2 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/scrcpy_client.py:26` |
| op-34585dace6394a8a | call/name | `core.device.screenshot.mss.MssScreenshotError` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/screenshot/mss.py:23` |
| op-5141a32dcc3b68fc | call/name | `core.device.screenshot.pyautogui.PyautoguiScreenshotError` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/screenshot/pyautogui.py:23` |
| op-8eb62fdefd3dcdd4 | call/name | `core.device.uiautomator2_client.U2Client` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/uiautomator2_client.py:33` |
| op-b5554a934b59856d | call/name | `core.device.uiautomator2_client.app_uiautomator_apk_local_path` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/uiautomator2_client.py:183` |
| op-94d0a48c019359aa | call/name | `core.device.window_capture.windows.window_info.MONITORINFO` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/window_capture/windows/window_info.py:165` |
| op-604a5eb066c5755d | call/name | `core.device.window_capture.windows.window_info.win32_WindowInfo` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/window_capture/windows/window_info.py:211` |
| op-390b052463d64e2b | call/name | `core.geometry.triangle.Triangle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/geometry/triangle.py:65` |
| op-f09907575bfb2780 | call/name | `core.image.compare_image` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:75` |
| op-aff3624941080ebd | call/name | `core.image.compare_image_rgb` | 3 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:28` |
| op-33e667f86f8a6d44 | call/name | `core.image.compare_rgb` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:102` |
| op-411b26b0f826e3ad | call/name | `core.image.get_image_all_appear_position` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:207` |
| op-eaf16b6eb84edcf0 | call/name | `core.image.img_cut` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:57` |
| op-15f6bd78d2f3c608 | call/name | `core.image.resize_ss_image` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:47` |
| op-b0c3564ed072e1c3 | call/name | `core.image.screenshot_cut` | 4 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:27` |
| op-7f9fc2836a28a23c | call/name | `core.image.search_image_in_area` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/image.py:123` |
| op-28219f28e1bccac9 | call/name | `core.ipc_manager.SharedMemory` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ipc_manager.py:14` |
| op-fb2b9c4ddfa25d7b | call/name | `core.notification.get_root_path` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/notification.py:23` |
| op-fceb99a8924c870e | call/name | `core.ocr.baas_ocr_client.Client.BaasOcrClient` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/Client.py:289` |
| op-e3c1d30ef1908eb4 | call/name | `core.ocr.baas_ocr_client.Client.ServerConfig` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/Client.py:58` |
| op-95c0558398a017ea | call/name | `core.ocr.baas_ocr_client.server_installer.OcrRepoManager` | 2 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:396` |
| op-dcb51bc9ed2171f0 | call/name | `core.ocr.baas_ocr_client.server_installer._android_internal_binary_path` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:206` |
| op-72a17fa1333b1b5b | call/name | `core.ocr.baas_ocr_client.server_installer._android_internal_runtime_root` | 2 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:121` |
| op-89f0631b282437ed | call/name | `core.ocr.baas_ocr_client.server_installer._android_internal_version_file` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:143` |
| op-d0fb3c930a48dfdf | call/name | `core.ocr.baas_ocr_client.server_installer._android_library_abi_dir` | 3 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:109` |
| op-4cbd2b5121abf35d | call/name | `core.ocr.baas_ocr_client.server_installer._android_ocr_branch` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:64` |
| op-2943c135100de5dd | call/name | `core.ocr.baas_ocr_client.server_installer._download_android_archive` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:223` |
| op-0ed73d9d8124b5cf | call/name | `core.ocr.baas_ocr_client.server_installer._find_android_archive_root` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:228` |
| op-b3aa6813b10059fe | call/name | `core.ocr.baas_ocr_client.server_installer._get_android_remote_sha` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:202` |
| op-2723cc7a6b03dfc8 | call/name | `core.ocr.baas_ocr_client.server_installer._install_android_prebuild` | 2 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:393` |
| op-3c87c1abc5c2309d | call/name | `core.ocr.baas_ocr_client.server_installer._is_android_runtime` | 2 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:392` |
| op-bca6163f801d2cac | call/name | `core.ocr.baas_ocr_client.server_installer._read_android_installed_sha` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:203` |
| op-9d7cab265a824dd1 | call/name | `core.ocr.baas_ocr_client.server_installer._read_android_internal_sha` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:205` |
| op-2e11e238292f9ff6 | call/name | `core.ocr.baas_ocr_client.server_installer._server_binary_path` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:204` |
| op-775feb9f23f156a6 | call/name | `core.ocr.baas_ocr_client.server_installer.noninteractive_git_env` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/ocr/baas_ocr_client/server_installer.py:274` |
| op-185c95f636c8b903 | call/name | `core.picture.choose_buff` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/picture.py:219` |
| op-ec538f6ddf5672f1 | call/name | `core.picture.co_detect` | 2 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/picture.py:190` |
| op-a984b58ce902e840 | call/name | `core.picture.deal_with_pop_ups` | 1 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/picture.py:150` |
| op-3792c5f7719cf33b | call/name | `core.picture.match_img_feature` | 3 | image.detect | Runtime Vision | `baas::script::host::VisionHost` | PARITY-VISION-HOST | INVENTORIED | `core/picture.py:108` |
| op-b48508ac9c6fd6dd | call/name | `core.pushkit.push_feishu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/pushkit.py:28` |
| op-6bdbd0b601e2a51e | call/name | `core.pushkit.push_json` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/pushkit.py:24` |
| op-050fd2614e7559fa | call/name | `core.pushkit.push_serverchan` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/pushkit.py:26` |
| op-9b6934e1d77a7cb5 | call/name | `core.utils.is_nearby_group` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:229` |
| op-ba959750c4ae13fa | call/name | `create_executable` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:561` |
| op-9c1c0d9c9ae63872 | call/name | `default_factory` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:597` |
| op-0de9703a515a9bb4 | call/name | `deploy.installer._installer.BAASGitCallbacks` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:776` |
| op-7fb6440292d9558a | call/name | `deploy.installer._installer.check_env_patch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1024` |
| op-09bc1b85dd1551b0 | call/name | `deploy.installer._installer.check_pdm` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1020` |
| op-f2fb4a56191aa3b2 | call/name | `deploy.installer._installer.check_pip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1022` |
| op-3268d047eb13c92b | call/name | `deploy.installer._installer.check_pth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1023` |
| op-f159b82b66345e2f | call/name | `deploy.installer._installer.check_python` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1018` |
| op-e307b7affadb8d8c | call/name | `deploy.installer._installer.check_repo_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:931` |
| op-f011856eeb462635 | call/name | `deploy.installer._installer.check_requirements` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1027` |
| op-9b78ebcc0df83757 | call/name | `deploy.installer._installer.check_upx` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:543` |
| op-0a149cb14c10d862 | call/name | `deploy.installer._installer.clean_up` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1192` |
| op-55a5ef1c627fdebf | call/name | `deploy.installer._installer.clone_repo` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:805` |
| op-361000f8537fb149 | call/name | `deploy.installer._installer.dynamic_update_installer` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1197` |
| op-c908390f8858591e | call/name | `deploy.installer._installer.error_tackle` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:379` |
| op-549a1c05e083b312 | call/name | `deploy.installer._installer.fix_exe_shebangs` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1029` |
| op-6c49fc7c3c22f229 | call/name | `deploy.installer._installer.get_update_type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1074` |
| op-7807fed0f0e393bb | call/name | `deploy.installer._installer.git_install_baas` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1091` |
| op-29c76ebff79a005b | call/name | `deploy.installer._installer.git_update_baas` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1089` |
| op-3908ba4b68625f0d | call/name | `deploy.installer._installer.insert_new_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:273` |
| op-47be57932e73d9a5 | call/name | `deploy.installer._installer.install_or_update_BAAS_repo_to_latest` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1026` |
| op-f4afcc5e7f20ebdc | call/name | `deploy.installer._installer.install_package` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:596` |
| op-9d65858f77270441 | call/name | `deploy.installer._installer.mirrorc_install_baas` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1114` |
| op-7d309247d1d2950c | call/name | `deploy.installer._installer.mirrorc_update_baas` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1116` |
| op-c7a6350753c8f087 | call/name | `deploy.installer._installer.pre_check` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1191` |
| op-550558537f7f5a03 | call/name | `deploy.installer._installer.repair_broken_git_repo` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:962` |
| op-345170527a25ac16 | call/name | `deploy.installer._installer.run_app` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:993` |
| op-5d923a49d178cf58 | call/name | `deploy.installer._installer.start_app` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:527` |
| op-5809411a318114b1 | call/name | `deploy.installer._installer.try_git_install_or_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1083` |
| op-e12687fd86505ad3 | call/name | `deploy.installer._installer.try_mirrorc_install_or_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:1081` |
| op-91ce19c0caf26834 | call/name | `deploy.installer.const.get_remote_sha_methods_for_channel` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/const.py:156` |
| op-0b16d11359cb64bd | call/name | `deploy.installer.const.normalize_update_channel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/const.py:152` |
| op-e1d13aa894cf5909 | call/name | `deploy.installer.installer.AppLauncher` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1145` |
| op-e9e2d37af2c30c02 | call/name | `deploy.installer.installer.BAASGitCallbacks` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:475` |
| op-e889b30ee506f45a | call/name | `deploy.installer.installer.DotPrinter` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:310` |
| op-ea2d044684fc32a2 | call/name | `deploy.installer.installer.EnvironmentManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1130` |
| op-e9d6e1d4dd71e812 | call/name | `deploy.installer.installer.GitOperationHandler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:552` |
| op-81ab616a8e79c719 | call/name | `deploy.installer.installer.GlobalConfig` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1121` |
| op-6921099e80e03966 | call/name | `deploy.installer.installer.UpdateOrchestrator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1137` |
| op-57d8a30a881bd03f | call/name | `deploy.installer.installer.main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1154` |
| op-ecfb58037afe4415 | call/name | `deploy.installer.installer.noninteractive_git_env` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:321` |
| op-a411dda14f7b96a9 | call/name | `deploy.installer.mirrorc_update.mirrorc_updater.RequestReturn` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:105` |
| op-95a3d1adf1f70fcf | call/name | `deploy.installer.mirrorc_update.mirrorc_updater.ServerInternalError` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:67` |
| op-8fae561e8b1d8cbf | call/name | `deploy.installer.mirrorc_update.utils.detect_system_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/utils.py:51` |
| op-f26139cb7eea84b3 | call/name | `detect_major_version` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/mumu_manager_api.py:39` |
| op-48cde33c33ca7c39 | call/name | `develop_tools.auto_translate.HtmlHandler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:164` |
| op-9ba32b0ef792ea51 | call/name | `develop_tools.auto_translate.LreleaseHandler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:165` |
| op-01d53183a27fa126 | call/name | `develop_tools.auto_translate.Pylupdate5Handler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:162` |
| op-cc69ee2c8506fa90 | call/name | `develop_tools.auto_translate.Request` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:169` |
| op-4852f78cfdf5b95d | call/name | `develop_tools.auto_translate.XmlHandler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:163` |
| op-20fe53b173248acb | call/name | `develop_tools.config_translation_generator.contains_chinese` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/config_translation_generator.py:33` |
| op-3e73fbe27d1f1fe9 | call/name | `develop_tools.config_translation_generator.create_translation_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/config_translation_generator.py:73` |
| op-1eede816fc69d6c6 | call/name | `develop_tools.config_translation_generator.find_chinese_strings` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/config_translation_generator.py:29` |
| op-c8877f82cdc09761 | call/name | `develop_tools.config_translation_generator.remove_duplicates` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/config_translation_generator.py:72` |
| op-6676b64a47d083b5 | call/name | `develop_tools.explore_task_data_generator.get_actions` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:166` |
| op-1910903fc1d184c8 | call/name | `develop_tools.explore_task_data_generator.get_input` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:21` |
| op-5fe396d64ef38aaa | call/name | `develop_tools.explore_task_data_generator.get_one_position` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:51` |
| op-f261523d44f6b545 | call/name | `develop_tools.explore_task_data_generator.get_stage_name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:163` |
| op-b28024bb851c8d84 | call/name | `develop_tools.explore_task_data_generator.get_team_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:165` |
| op-75675c99321237b1 | call/name | `develop_tools.explore_task_data_generator.get_y_n` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:88` |
| op-e113c841e0d2fea9 | call/name | `develop_tools.explore_task_data_generator.has_position` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/explore_task_data_generator.py:134` |
| op-bad44c496d734181 | call/name | `develop_tools.generate_dataclass_code.generate_dataclass_code` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/generate_dataclass_code.py:33` |
| op-676685a0912f5940 | call/name | `develop_tools.generate_dataclass_code.save_generated_code` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/generate_dataclass_code.py:34` |
| op-426d3609e82e63f0 | call/name | `develop_tools.lesson.lesson_affection_student_image_extractor.img_cut` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/lesson/lesson_affection_student_image_extractor.py:59` |
| op-c73c28a5e22afb4e | call/name | `develop_tools.taskDataFixer.main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/taskDataFixer.py:42` |
| op-810a9d924738cd41 | call/name | `develop_tools.update_student_name.download_json` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/update_student_name.py:24` |
| op-6c309feb56a93242 | call/name | `device_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/simulator_api.py:21` |
| op-140888169334a310 | call/name | `emit` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:659` |
| op-ca4f65b4efe9f458 | call/name | `employ_units` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:339` |
| op-4f92c707379e123d | call/name | `encode_frame` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:79` |
| op-c6f406cf480f8bc2 | call/name | `encode_json` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:76` |
| op-a9e619c50c517a87 | call/name | `ensure_safe_config_id` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:30` |
| op-0e76ea55182c7b49 | call/name | `execute_grid_task` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:288` |
| op-1e19503a82652c42 | call/name | `f` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:20` |
| op-680069d09376334d | call/name | `finalize_control_auth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_control.py:36` |
| op-be4b12758213c4df | call/name | `find_display_name` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/start_simulator.py:36` |
| op-952ff7a022949cc1 | call/name | `fun` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:298` |
| op-b5df3010004218d9 | call/name | `func` | 13 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:876` |
| op-5dcf4300e1a88d76 | call/name | `get_bluestacks_nxt_adb_port` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_adb_address.py:12` |
| op-ba48c82aa0000f77 | call/name | `get_bluestacks_nxt_adb_port_id` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:101` |
| op-2bc686bf4a875122 | call/name | `get_monitor_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:167` |
| op-96f384bbf28db9c0 | call/name | `get_mumu_adb_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_adb_address.py:42` |
| op-52fdcb94b95cec5f | call/name | `get_pid_by_cmdline` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/stop_simulator.py:26` |
| op-7e6978effe45ddf6 | call/name | `get_simulator_port` | 13 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:105` |
| op-e02f076f55695716 | call/name | `gui.components.expand.baasUpdateConfig.MirrorCCDKTestThread` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:398` |
| op-548dbaba98b7bfdb | call/name | `gui.components.expand.baasUpdateConfig.TestGetRemoteShaMethodWorker` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:505` |
| op-26ddcbeca13fb272 | call/name | `gui.components.expand.createPriority.WordWrapTextEdit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:293` |
| op-5e795c10cedbf729 | call/name | `gui.components.expand.eventMapConfig.formation_attr_to_cn` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:123` |
| op-afb158e5f87a251c | call/name | `gui.components.expand.expandTemplate.ComboBoxCustom` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:252` |
| op-3a5a3d626e6c3b16 | call/name | `gui.components.expand.expandTemplate.ConfigItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:56` |
| op-ee1d14bbccfdee75 | call/name | `gui.components.expand.expandTemplate.ConfigItemV2` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:236` |
| op-84e56d383b9589e0 | call/name | `gui.components.expand.featureSwitch.DetailSettingMessageBox` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:254` |
| op-669f22920642378e | call/name | `gui.components.expand.schedulePriority.StateButton` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:84` |
| op-df5094e12b05cdbb | call/name | `gui.fragments.history._noninteractive_git_env` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:53` |
| op-fd917851b1f64857 | call/name | `gui.fragments.history._read_history_with_git` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:89` |
| op-d9f2830c1e36dce3 | call/name | `gui.fragments.history._read_history_with_pygit2` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:91` |
| op-2bf145656b59d18c | call/name | `gui.fragments.history.read_history_entries` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:123` |
| op-22ae634c83d5252e | call/name | `gui.fragments.home.MainThread` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:115` |
| op-e89f232124b60a88 | call/name | `gui.fragments.readme.ReadMeInterface` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:41` |
| op-dbe002d03de19a9b | call/name | `gui.fragments.readme.ReadMeWindow` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:58` |
| op-fb035ec83d69d039 | call/name | `gui.util.config_gui.ConfigGui` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:99` |
| op-e705cb6ccb7ab852 | call/name | `gui.util.config_gui.LanguageSerializer` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:92` |
| op-d1d464f199340ed3 | call/name | `gui.util.config_gui.isWin11` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/config_gui.py:88` |
| op-81a86fff720f8cde | call/name | `gui.util.hotkey_manager.HotkeyCaptureInput` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:411` |
| op-8676c07d5c6a582a | call/name | `gui.util.hotkey_manager.HotkeyInputDialog` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:462` |
| op-25ac35d8ff88e636 | call/name | `gui.util.hotkey_manager._KeyboardHookThread` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:307` |
| op-1d60b2df531218d0 | call/name | `gui.util.translator.Translator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:94` |
| op-b63e70fc13f72a4a | call/name | `handler_type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:81` |
| op-80b7b8ee2e052d21 | call/name | `hkdf_sha256` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:112` |
| op-394a9d14dfee90ff | call/name | `hmac_sha256` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:150` |
| op-dbb5366fa246861f | call/name | `install_asyncio_exception_handler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:66` |
| op-13600e84a976a446 | call/name | `load_data` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/device_display.py:6` |
| op-fb25b912c0c93933 | call/name | `main.Main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:119` |
| op-8c5d9fba9fd836f6 | call/name | `main.service._env_bool` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:37` |
| op-60384aeafc1f281f | call/name | `main.service.delete_pid_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:81` |
| op-d8a6e91588ab702a | call/name | `main.service.main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:85` |
| op-aa16b94d6be3ec5f | call/name | `main.service.parse_args` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:57` |
| op-aca6ff4bfb5bd7d9 | call/name | `main.service.save_pid` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.service.py:77` |
| op-53d2ab5f4eda097f | call/name | `match_currency` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:182` |
| op-990a6f1f331aab31 | call/name | `migrate_to_current_schema` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:674` |
| op-f88deffe0dc2ac81 | call/name | `module.activities.activity_utils.build_activity_task_name_list` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:192` |
| op-eaef63e8838d974a | call/name | `module.activities.activity_utils.check_sweep_availability` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:173` |
| op-ec44ef9599e23963 | call/name | `module.activities.activity_utils.continue_exchange` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:477` |
| op-5dcc28f0a1ee0d31 | call/name | `module.activities.activity_utils.get_exchange_assets` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:480` |
| op-24073dde167da9f4 | call/name | `module.activities.activity_utils.get_stage_data` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:99` |
| op-6ffe94e61d391508 | call/name | `module.activities.activity_utils.preprocess_activity_region` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:91` |
| op-b862ceb9ed43a307 | call/name | `module.activities.activity_utils.preprocess_activity_sweep_times` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:90` |
| op-e8173582372123f0 | call/name | `module.activities.activity_utils.refresh_exchange_times` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:482` |
| op-34107c779c5f4b82 | call/name | `module.activities.activity_utils.start_fight` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:340` |
| op-2d07502044b1833c | call/name | `module.activities.activity_utils.start_mission` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:183` |
| op-4f5078680616ca49 | call/name | `module.activities.activity_utils.start_story` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:239` |
| op-610579ce854889d4 | call/name | `module.activities.activity_utils.start_sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:122` |
| op-5eae3588bb1269c3 | call/name | `module.activities.activity_utils.to_activity` | 15 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:97` |
| op-6dacf206848708c7 | call/name | `module.activities.activity_utils.to_challenge_task_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:273` |
| op-25d669b733017614 | call/name | `module.activities.activity_utils.to_exchange` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:465` |
| op-d1510c2da550bd9e | call/name | `module.activities.activity_utils.to_mission_task_info` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:118` |
| op-5634dfc895cc41aa | call/name | `module.activities.activity_utils.to_set_exchange_times_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:466` |
| op-6d0222b2ac61f96b | call/name | `module.activities.activity_utils.to_story_task_info` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:228` |
| op-5f04c8039ed79835 | call/name | `module.activities.bunnyChaserOnTheShip.chooseCardFourOfFour` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:378` |
| op-e615d220cf969c99 | call/name | `module.activities.bunnyChaserOnTheShip.drawCard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:16` |
| op-c61fca318aa15f50 | call/name | `module.activities.bunnyChaserOnTheShip.drawCardFourOfFour` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:379` |
| op-92ea93ff6983a182 | call/name | `module.activities.bunnyChaserOnTheShip.reshuffleCard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:377` |
| op-a62211b250ce0d1f | call/name | `module.activities.bunnyChaserOnTheShip.start_fight` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:105` |
| op-1b73f20d6900a510 | call/name | `module.activities.bunnyChaserOnTheShip.start_story` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:86` |
| op-e77876936fdd174e | call/name | `module.activities.bunnyChaserOnTheShip.start_sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:43` |
| op-4b1aa00a7bfe68f3 | call/name | `module.activities.bunnyChaserOnTheShip.sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:15` |
| op-681db7c7fcaaac75 | call/name | `module.activities.bunnyChaserOnTheShip.toCardStore` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:370` |
| op-747e8520d7011c38 | call/name | `module.activities.bunnyChaserOnTheShip.to_activity` | 15 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:22` |
| op-5c1973634423983d | call/name | `module.activities.bunnyChaserOnTheShip.to_challenge_task_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:189` |
| op-16fa45446c4fe02c | call/name | `module.activities.bunnyChaserOnTheShip.to_formation_edit_i` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:192` |
| op-7417c71fa6733064 | call/name | `module.activities.bunnyChaserOnTheShip.to_mission_task_info` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:39` |
| op-e59d57766a14a0af | call/name | `module.activities.bunnyChaserOnTheShip.to_story_task_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip.py:63` |
| op-3ad7c990ef731df6 | call/name | `module.activities.bunnyChaserOnTheShip2.continue_exchange` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:342` |
| op-245d30d95b43bbe4 | call/name | `module.activities.bunnyChaserOnTheShip2.get_exchange_assets` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:345` |
| op-fbdb0ac943098533 | call/name | `module.activities.bunnyChaserOnTheShip2.refresh_exchange_times` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:347` |
| op-ca9e35141c2fed18 | call/name | `module.activities.bunnyChaserOnTheShip2.start_fight` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:107` |
| op-9c1409ea0e04645f | call/name | `module.activities.bunnyChaserOnTheShip2.start_story` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:87` |
| op-9b713aca86b3bb2e | call/name | `module.activities.bunnyChaserOnTheShip2.start_sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:42` |
| op-a2be5fa5c317185a | call/name | `module.activities.bunnyChaserOnTheShip2.sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:15` |
| op-603d4182b4ac942a | call/name | `module.activities.bunnyChaserOnTheShip2.to_activity` | 13 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:21` |
| op-c64ce208653c859c | call/name | `module.activities.bunnyChaserOnTheShip2.to_challenge_task_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:161` |
| op-92a24159d0e1d53b | call/name | `module.activities.bunnyChaserOnTheShip2.to_exchange` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:330` |
| op-4f4672f715523c6d | call/name | `module.activities.bunnyChaserOnTheShip2.to_formation_edit_i` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:140` |
| op-29eef2a58791e49a | call/name | `module.activities.bunnyChaserOnTheShip2.to_mission_task_info` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:38` |
| op-ede9f25d454020be | call/name | `module.activities.bunnyChaserOnTheShip2.to_set_exchange_times_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:331` |
| op-6a2fcf2758a42edd | call/name | `module.activities.bunnyChaserOnTheShip2.to_story_task_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/bunnyChaserOnTheShip2.py:63` |
| op-91688c4437d85009 | call/name | `module.arena.check_skip_button` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/arena.py:37` |
| op-d847d0f12fbe6266 | call/name | `module.arena.choose_enemy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/arena.py:22` |
| op-50726ac6317c6570 | call/name | `module.arena.collect_tactical_challenge_reward` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/arena.py:10` |
| op-5b19a6f3de337518 | call/name | `module.arena.fight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/arena.py:38` |
| op-24d4751d078c0040 | call/name | `module.arena.get_tickets` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/arena.py:7` |
| op-5a41415c1eb651d0 | call/name | `module.arena.to_tactical_challenge` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/arena.py:6` |
| op-a574c352c3a364d6 | call/name | `module.cafe_reward.cafe_to_gift` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:211` |
| op-88fcebaa4766019b | call/name | `module.cafe_reward.calc_y_difference` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:548` |
| op-3a8d7a8f77089567 | call/name | `module.cafe_reward.change_invitation_ticket_up_down_order` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:269` |
| op-260f2c4539f7fc3b | call/name | `module.cafe_reward.change_order_type` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:268` |
| op-ed55fe4408e3ac85 | call/name | `module.cafe_reward.checkConfirmInvite` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:273` |
| op-947a1edbe5e53fe3 | call/name | `module.cafe_reward.collect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:17` |
| op-641566921da2ab96 | call/name | `module.cafe_reward.confirm_invite` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:254` |
| op-6baae7f44a2afc42 | call/name | `module.cafe_reward.find_student_position` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:212` |
| op-61f3933ed52c43b7 | call/name | `module.cafe_reward.get_cafe_earning_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:16` |
| op-3a7f9e1bad6eeb65 | call/name | `module.cafe_reward.get_invitation_ticket_next_time` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:23` |
| op-6d4f62b9ef1e7737 | call/name | `module.cafe_reward.get_invitation_ticket_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:22` |
| op-c898f5363659b94e | call/name | `module.cafe_reward.gift_to_cafe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:174` |
| op-7819ef42a29392b3 | call/name | `module.cafe_reward.interaction_for_cafe_solve_method3` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:26` |
| op-2ab6e60aefa588da | call/name | `module.cafe_reward.invite_by_affection` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:377` |
| op-a7306419ecc0405d | call/name | `module.cafe_reward.invite_girl` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:25` |
| op-0fb4b72da80f6e5b | call/name | `module.cafe_reward.invite_starred` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:384` |
| op-1d937814fc5af217 | call/name | `module.cafe_reward.is_english` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:534` |
| op-d0e7ec3f3c9aa2d5 | call/name | `module.cafe_reward.is_lower_english` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:577` |
| op-c8a7375a73ef60bc | call/name | `module.cafe_reward.is_upper_english` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:577` |
| op-fad49daa57607e56 | call/name | `module.cafe_reward.match` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:160` |
| op-74385fec93d83fe5 | call/name | `module.cafe_reward.operate_name` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:412` |
| op-5ce2f76a6a9ed570 | call/name | `module.cafe_reward.swipe_gift_and_screenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:158` |
| op-5c6d15919f0d7952 | call/name | `module.cafe_reward.to_cafe` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:15` |
| op-e70f676cfdfd1fc5 | call/name | `module.cafe_reward.to_cafe_earning_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:487` |
| op-b47b40e186bc9365 | call/name | `module.cafe_reward.to_confirm_invite` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:242` |
| op-93a40ebc7c16840a | call/name | `module.cafe_reward.to_invitation_ticket` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:252` |
| op-f547071f8b91c60d | call/name | `module.cafe_reward.to_no2_cafe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:29` |
| op-99072557ea9880ec | call/name | `module.cafe_reward.to_revise_order_type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:294` |
| op-aa3fa22236f20f97 | call/name | `module.cafe_reward.zoom_out` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:207` |
| op-0e977600580a206b | call/name | `module.clear_special_task_power.commissions_common_operation` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/clear_special_task_power.py:23` |
| op-4a52e784798ad0dc | call/name | `module.clear_special_task_power.get_los` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/clear_special_task_power.py:90` |
| op-99fdab2f7c8687af | call/name | `module.clear_special_task_power.get_task_count` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/clear_special_task_power.py:7` |
| op-9769152b5710588f | call/name | `module.clear_special_task_power.one_detect` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/clear_special_task_power.py:116` |
| op-7835e113aede9c30 | call/name | `module.clear_special_task_power.start_sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/clear_special_task_power.py:109` |
| op-16dca6d0a7eb6827 | call/name | `module.clear_special_task_power.to_commissions` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/clear_special_task_power.py:22` |
| op-1b30842326ce26b2 | call/name | `module.collect_daily_free_power.collect_daily_free_power` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_daily_free_power.py:13` |
| op-67de10d35c1131d6 | call/name | `module.collect_daily_free_power.detect_free_power_availability` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_daily_free_power.py:12` |
| op-86b0d67ac8bf62ab | call/name | `module.collect_daily_free_power.return_to_main_page` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_daily_free_power.py:17` |
| op-7c5d54764e523a7c | call/name | `module.collect_daily_free_power.to_purchase_pyroxenes_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_daily_free_power.py:10` |
| op-2a3a477627b38ef1 | call/name | `module.collect_daily_free_power.to_purchase_type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_daily_free_power.py:11` |
| op-093f343448ff943b | call/name | `module.collect_daily_task_power.to_tasks` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_daily_task_power.py:8` |
| op-4c67ed943cda5eb6 | call/name | `module.collect_pass_reward.collect_reward` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_pass_reward.py:15` |
| op-419c6dbb42855ca4 | call/name | `module.collect_pass_reward.detect_pass_level` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_pass_reward.py:76` |
| op-0ee1e8d2f8f02ae2 | call/name | `module.collect_pass_reward.detect_pass_next_level_point` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_pass_reward.py:77` |
| op-0f4a57873bf440f5 | call/name | `module.collect_pass_reward.detect_pass_weekly_point` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_pass_reward.py:78` |
| op-2b9bb154256c34d5 | call/name | `module.collect_pass_reward.detect_statistics` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_pass_reward.py:21` |
| op-b6c04bb1b90b8388 | call/name | `module.collect_pass_reward.main_page_to_pass_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_pass_reward.py:11` |
| op-a54d71934de3a656 | call/name | `module.collect_pass_reward.to_page_pass_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_pass_reward.py:18` |
| op-1c8aaee1975b8943 | call/name | `module.collect_pass_reward.to_page_pass_mission` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/collect_pass_reward.py:14` |
| op-0b8dd9e610ff6178 | call/name | `module.create.CreateItemCheckState` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:206` |
| op-0c0fd61661ba9eac | call/name | `module.create.check_crafting_list_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:35` |
| op-53e8b3b5a566c93e | call/name | `module.create.collect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:295` |
| op-5c761d95429757fc | call/name | `module.create.confirm_filter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:378` |
| op-fc4eb73a5142b235 | call/name | `module.create.confirm_select_node` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:42` |
| op-0ed8e68b0f6db178 | call/name | `module.create.confirm_sort` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:413` |
| op-46d7903b6526f222 | call/name | `module.create.create_phase` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:37` |
| op-461e224dc84ac792 | call/name | `module.create.filter_list_ensure_choose` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:377` |
| op-874e2a1c1d2e171f | call/name | `module.create.get_display_setting` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:202` |
| op-a6bcbc0a5dffbf5d | call/name | `module.create.get_item_holding_quantity` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:868` |
| op-a78a8a81d83c26fa | call/name | `module.create.get_item_holding_quantity_region` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:843` |
| op-c62c1cbe22b7cd8c | call/name | `module.create.get_item_image_region` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:840` |
| op-02ae50cdaa9d30d3 | call/name | `module.create.get_item_position` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:823` |
| op-0c5040792c7491b9 | call/name | `module.create.get_item_selected_quantity` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:248` |
| op-259681b4c3252e72 | call/name | `module.create.get_item_selected_quantity_region` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:235` |
| op-2a29a746facbc89d | call/name | `module.create.get_next_execute_time` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:59` |
| op-146ed9a2adc52c34 | call/name | `module.create.item_order_list_builder` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:204` |
| op-27fe1be89386636d | call/name | `module.create.item_recognize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:210` |
| op-63e74724d3017b41 | call/name | `module.create.judge_item_state` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:989` |
| op-1b081b07ae8adee2 | call/name | `module.create.log_detect_information` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:217` |
| op-cf7b5287efcdf395 | call/name | `module.create.preprocess_node_info` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:121` |
| op-b75abd76d322e2b6 | call/name | `module.create.priority_language_convert` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:118` |
| op-3d158fe15f112cde | call/name | `module.create.receive_objects_and_check_crafting_list_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:24` |
| op-e3717d6891787623 | call/name | `module.create.select_item` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:717` |
| op-7097c1c2ff7bb72b | call/name | `module.create.select_node` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:68` |
| op-ba5e8c7fe1938293 | call/name | `module.create.set_display_setting` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:203` |
| op-1354709235696b85 | call/name | `module.create.set_display_setting_filter_list` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:365` |
| op-b824c51282db1b69 | call/name | `module.create.set_display_setting_sort_arrow_direction` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:369` |
| op-84ede9a73170a450 | call/name | `module.create.set_display_setting_sort_type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:367` |
| op-882b3d0e62fbe223 | call/name | `module.create.set_sort_type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:412` |
| op-33ef7aa68d30527f | call/name | `module.create.solve_unfinished_create` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:18` |
| op-864ed97130e6052a | call/name | `module.create.start_phase` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:67` |
| op-c8f945f722ce35bd | call/name | `module.create.swipe_item_list_get_y_diff` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:701` |
| op-31ede39f75b53bee | call/name | `module.create.to_filter_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:373` |
| op-6ebd0663d8c6b224 | call/name | `module.create.to_manufacture_store` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:15` |
| op-d67a6a6ae460954b | call/name | `module.create.to_phase1` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:32` |
| op-b4ee55b14d0e7903 | call/name | `module.create.to_sort_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:411` |
| op-cf8728a8571c0fa0 | call/name | `module.dailyGameActivities.HinaSummerVacationAudioGame.blue_judge` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/HinaSummerVacationAudioGame.py:72` |
| op-91549172a209c754 | call/name | `module.dailyGameActivities.HinaSummerVacationAudioGame.game_end_to_game_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/HinaSummerVacationAudioGame.py:18` |
| op-9ab5a8ee0faf7bcb | call/name | `module.dailyGameActivities.HinaSummerVacationAudioGame.start_play` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/HinaSummerVacationAudioGame.py:17` |
| op-573375c0b721a201 | call/name | `module.dailyGameActivities.HinaSummerVacationAudioGame.to_game` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/HinaSummerVacationAudioGame.py:16` |
| op-1f994014ed723e7a | call/name | `module.dailyGameActivities.HinaSummerVacationAudioGame.yellow_judge` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/HinaSummerVacationAudioGame.py:77` |
| op-88d35b031ce303c9 | call/name | `module.dailyGameActivities.serikaSummerRamenStall.chooseLoop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:13` |
| op-9ff1236a93c239fd | call/name | `module.dailyGameActivities.serikaSummerRamenStall.collectDailyReward` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:14` |
| op-3f72009340583673 | call/name | `module.dailyGameActivities.serikaSummerRamenStall.returnToMainPage` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:15` |
| op-ca5cb44956a4f901 | call/name | `module.dailyGameActivities.serikaSummerRamenStall.startGame` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/dailyGameActivities/serikaSummerRamenStall.py:12` |
| op-66ad4fd8fd2df4eb | call/name | `module.explore_tasks.explore_task.extract_first_team` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:228` |
| op-1e91ad8531d2af50 | call/name | `module.explore_tasks.explore_task.need_fight` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:198` |
| op-61cc9d9fa4c244b9 | call/name | `module.explore_tasks.explore_task.set_explore_task_mode` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:197` |
| op-e6219a8cc5435658 | call/name | `module.explore_tasks.explore_task.validate_and_add_task` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/explore_task.py:142` |
| op-a5a30e123a536f44 | call/name | `module.explore_tasks.sweep_task.print_task_list` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/sweep_task.py:37` |
| op-60eacc34c4206798 | call/name | `module.explore_tasks.sweep_task.start_sweep` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/sweep_task.py:68` |
| op-dcd913d408e879ac | call/name | `module.explore_tasks.task_utils.confirm_teleport` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:347` |
| op-eaf97ac0acea44bb | call/name | `module.explore_tasks.task_utils.convert_team_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:300` |
| op-8841ee3d805214df | call/name | `module.explore_tasks.task_utils.employ_units` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:300` |
| op-b5accf7f2896a332 | call/name | `module.explore_tasks.task_utils.end_turn` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:362` |
| op-69393f6b397bafbe | call/name | `module.explore_tasks.task_utils.get_formation_index` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:202` |
| op-bde4fe7137fefb77 | call/name | `module.explore_tasks.task_utils.handle_task_pop_ups` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:196` |
| op-2d21fca8c65453bc | call/name | `module.explore_tasks.task_utils.retreat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:388` |
| op-90629c58739b518b | call/name | `module.explore_tasks.task_utils.run_task_action` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:316` |
| op-a99fae71581122ff | call/name | `module.explore_tasks.task_utils.set_skip_and_auto_status` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:315` |
| op-2f2f8db7ef1d8b49 | call/name | `module.explore_tasks.task_utils.switch_formation` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:351` |
| op-922a4929d1eb20c5 | call/name | `module.explore_tasks.task_utils.to_hard_event` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:39` |
| op-d3d6438ed51e7014 | call/name | `module.explore_tasks.task_utils.to_mission_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:157` |
| op-0bdce65093a6594f | call/name | `module.explore_tasks.task_utils.to_normal_event` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:37` |
| op-4a02ec6d3fb7b77f | call/name | `module.explore_tasks.task_utils.wait_over` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/explore_tasks/task_utils.py:313` |
| op-adbe6cc5a809a70a | call/name | `module.friend.check_name_in_white_list` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/friend.py:36` |
| op-a66a511afc9c4a6e | call/name | `module.friend.delete_friend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/friend.py:47` |
| op-55d405e26bdddf15 | call/name | `module.friend.get_possible_friend_positions` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/friend.py:14` |
| op-68bf52c587a5afe0 | call/name | `module.friend.to_friend_management` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/friend.py:8` |
| op-d2ee2517ea4fd646 | call/name | `module.friend.to_player_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/friend.py:32` |
| op-f7035b434e90de1e | call/name | `module.group.to_group` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group.py:6` |
| op-0858756ce90ad004 | call/name | `module.group_story.check_current_episode_cleared` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group_story.py:17` |
| op-9a708a0bfa2b4abc | call/name | `module.group_story.clear_current_plot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group_story.py:24` |
| op-199a28f31650862b | call/name | `module.group_story.judge_need_check_next_page` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group_story.py:12` |
| op-e2e336fe9dd95965 | call/name | `module.group_story.one_detect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group_story.py:18` |
| op-95021b54cd56aadd | call/name | `module.group_story.to_episode_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group_story.py:23` |
| op-cab609e1cded8495 | call/name | `module.group_story.to_group_story` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group_story.py:8` |
| op-56f05f9d69659f3e | call/name | `module.group_story.to_region` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/group_story.py:16` |
| op-619354f6d952cc7b | call/name | `module.joint_firing_drill.check_drill_state` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:12` |
| op-f4b4dc78158fe351 | call/name | `module.joint_firing_drill.enter_fight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:164` |
| op-0165c653159e7a39 | call/name | `module.joint_firing_drill.fight_one_drill` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:94` |
| op-3f8ecb36a44541da | call/name | `module.joint_firing_drill.finish_existing_drill` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:84` |
| op-7cfa897bc42143ba | call/name | `module.joint_firing_drill.get_drill_ticket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:81` |
| op-51751064c2e7ccf4 | call/name | `module.joint_firing_drill.select_formation` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:162` |
| op-52c7bb125df05a02 | call/name | `module.joint_firing_drill.solve_drill` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:16` |
| op-ddda8ee7a11732f9 | call/name | `module.joint_firing_drill.start_drill` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:153` |
| op-a0ea7da572f96516 | call/name | `module.joint_firing_drill.sweep_drill` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:87` |
| op-e370e878936be487 | call/name | `module.joint_firing_drill.to_drill` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:80` |
| op-ade82a9288482ca9 | call/name | `module.joint_firing_drill.to_drill_information` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:161` |
| op-9c519616bc19c342 | call/name | `module.joint_firing_drill.to_joint_firing_menu` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:11` |
| op-df91179e5fe663ec | call/name | `module.joint_firing_drill.wait_drill_fight_finish` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/joint_firing_drill.py:166` |
| op-b5a01d9d98c8304b | call/name | `module.lesson.check_region_availability` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:387` |
| op-a20f9ff8d4c92476 | call/name | `module.lesson.choose_lesson` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:54` |
| op-91a131caf92a175d | call/name | `module.lesson.cn_get_lesson_each_region_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:354` |
| op-18a850b5e2f2a132 | call/name | `module.lesson.execute_lesson` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:57` |
| op-733dca3c35d66fa0 | call/name | `module.lesson.get_favor_student_detect_region` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:517` |
| op-c4b7412a16d5f01c | call/name | `module.lesson.get_favor_student_image_template_names` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:488` |
| op-7dc01f4ee7c8a17e | call/name | `module.lesson.get_lesson_each_region_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:52` |
| op-dbf34442d2e701b0 | call/name | `module.lesson.get_lesson_region_num` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:42` |
| op-502e6abf5118eb37 | call/name | `module.lesson.get_lesson_relationship_counts` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:52` |
| op-58cb179f68c1ee02 | call/name | `module.lesson.get_lesson_tickets` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:31` |
| op-e9b4f837d4a0b02e | call/name | `module.lesson.global_jp_get_lesson_each_region_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:356` |
| op-22bf3817f5e96200 | call/name | `module.lesson.invite_favor_student` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:38` |
| op-f468292f627f7177 | call/name | `module.lesson.is_lower_english` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:296` |
| op-a85dc2e96d5e4b35 | call/name | `module.lesson.is_upper_english` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:296` |
| op-ca05824125b40df4 | call/name | `module.lesson.out_lesson_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:53` |
| op-72570e96a27085eb | call/name | `module.lesson.pre_process_lesson_name` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:23` |
| op-7c936afb965dad93 | call/name | `module.lesson.purchase_lesson_ticket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:30` |
| op-b413d2c12e534442 | call/name | `module.lesson.start_lesson` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:235` |
| op-16b33be47835769f | call/name | `module.lesson.switch_lesson_region_page` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:102` |
| op-ce7b6821446e2faa | call/name | `module.lesson.to_all_locations` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:51` |
| op-1c5cfcb71f8139d2 | call/name | `module.lesson.to_lesson_location_select` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:27` |
| op-ca34c340390e16e3 | call/name | `module.lesson.to_lesson_region` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:49` |
| op-5bb49810e4fc0fdd | call/name | `module.lesson.to_location_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:234` |
| op-c3f80661fe63a7a6 | call/name | `module.lesson.to_purchase_lesson_ticket` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:167` |
| op-e793e1adeda36dfc | call/name | `module.lesson.to_select_location` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/lesson.py:41` |
| op-895325cfd73f4b56 | call/name | `module.mail.to_mail` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mail.py:15` |
| op-0ff0837d91165a20 | call/name | `module.main_story.auto_fight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:265` |
| op-62314d88138e3bba | call/name | `module.main_story.check_current_plot_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:306` |
| op-7164d05f3e874b21 | call/name | `module.main_story.check_episode` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:300` |
| op-e727a2675877ae7d | call/name | `module.main_story.check_state_and_get_stage_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:263` |
| op-1d608d128204eb1e | call/name | `module.main_story.clear_current_plot` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:259` |
| op-11e1327bd995a178 | call/name | `module.main_story.enter_battle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:143` |
| op-6c88234d3f68a72f | call/name | `module.main_story.episode_stage` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:175` |
| op-1ac346d416965c80 | call/name | `module.main_story.get_acceleration` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:97` |
| op-7b976c818a5003ae | call/name | `module.main_story.get_auto` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:107` |
| op-2b998e5b22f796db | call/name | `module.main_story.get_stage_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:10` |
| op-ccdeeb00bbb72867 | call/name | `module.main_story.process_regions` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:13` |
| op-c16a5d5f667bffe8 | call/name | `module.main_story.push_episode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:20` |
| op-8a185afb954ac723 | call/name | `module.main_story.search_episode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:180` |
| op-ca59b2942dd030d6 | call/name | `module.main_story.set_acc` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:117` |
| op-2ad524a615cfa366 | call/name | `module.main_story.set_acc_and_auto` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:146` |
| op-53c669bc7db9ae00 | call/name | `module.main_story.set_auto` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:118` |
| op-cb1f17cbdb4d34d2 | call/name | `module.main_story.to_episode` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:299` |
| op-e5a2ddd443d519e4 | call/name | `module.main_story.to_episode_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:307` |
| op-34287fdb720211f8 | call/name | `module.main_story.to_main_story` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:12` |
| op-129720d8dd81ae3b | call/name | `module.main_story.to_select_episode` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:302` |
| op-00ad7ec0b4494a52 | call/name | `module.main_story.to_stage` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/main_story.py:298` |
| op-e76ee26249dfd42c | call/name | `module.mini_story.check_6_region_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:13` |
| op-d4f6ea49e95f068e | call/name | `module.mini_story.check_current_episode_cleared` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:17` |
| op-0fc3846a7b8e5f69 | call/name | `module.mini_story.clear_current_plot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:24` |
| op-b7aade8176d6ee4d | call/name | `module.mini_story.judge_need_check_next_page` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:12` |
| op-33af8dcc23ccfe04 | call/name | `module.mini_story.one_detect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:18` |
| op-682f2f7c77639414 | call/name | `module.mini_story.to_episode_info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:23` |
| op-bf3c7a5a4b14805e | call/name | `module.mini_story.to_mini_story` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:8` |
| op-481b731f5ae5102b | call/name | `module.mini_story.to_region` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/mini_story.py:16` |
| op-5467e940f964e968 | call/name | `module.momo_talk.change_direction` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:49` |
| op-0bf5a6138aa9b106 | call/name | `module.momo_talk.change_sort` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:48` |
| op-4b84123c0b143291 | call/name | `module.momo_talk.check_mode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:9` |
| op-1b101257ee50e4b4 | call/name | `module.momo_talk.common_solve_affection_story_method` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:41` |
| op-ecd9afeb33c6ce69 | call/name | `module.momo_talk.getConversationState` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:112` |
| op-aba23f45babb1345 | call/name | `module.momo_talk.get_unread_location` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:20` |
| op-1c93c7e67151f491 | call/name | `module.momo_talk.implement` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:37` |
| op-aca323e25d615e4b | call/name | `module.momo_talk.to_momotalk` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/momo_talk.py:7` |
| op-08e64a1416831c86 | call/name | `module.no1_cafe_invite.delay_cafe_reward_execution_time` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:14` |
| op-aae1a15d7f12a1d2 | call/name | `module.no1_cafe_invite.judge_use_invitation_ticket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/no1_cafe_invite.py:11` |
| op-6cbaba3ab69351c4 | call/name | `module.restart.check_need_restart` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/restart.py:15` |
| op-07caf86bf25c9d5c | call/name | `module.restart.start` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/restart.py:12` |
| op-66de97678b136226 | call/name | `module.rewarded_task.bounty_common_operation` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:29` |
| op-dbcf9294dca11cf1 | call/name | `module.rewarded_task.get_bounty_coin` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:43` |
| op-fde3b1b4eee620c3 | call/name | `module.rewarded_task.get_los` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:111` |
| op-a776a84d801bf6ee | call/name | `module.rewarded_task.one_detect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:139` |
| op-126454106a39e35e | call/name | `module.rewarded_task.purchase_bounty_ticket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:16` |
| op-855b36e1475efe24 | call/name | `module.rewarded_task.start_sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:132` |
| op-e18be783fb9014f8 | call/name | `module.rewarded_task.to_bounty` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:28` |
| op-874c9f9375f1fabd | call/name | `module.rewarded_task.to_choose_bounty` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:15` |
| op-88bedd15cd30cfc9 | call/name | `module.rewarded_task.to_purchase_bounty_ticket_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:193` |
| op-777bf324704df462 | call/name | `module.scrimmage.get_los` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/scrimmage.py:86` |
| op-c4427a3178914868 | call/name | `module.scrimmage.purchase_scrimmage_ticket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/scrimmage.py:18` |
| op-d8b2f5337aabae06 | call/name | `module.scrimmage.scrimmage_common_operation` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/scrimmage.py:30` |
| op-9e562cc573f231cb | call/name | `module.scrimmage.start_sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/scrimmage.py:107` |
| op-4cb598089415b44a | call/name | `module.scrimmage.to_choose_scrimmage` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/scrimmage.py:17` |
| op-d03f89019be40122 | call/name | `module.scrimmage.to_purchase_scrimmage_ticket_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/scrimmage.py:155` |
| op-8a235550ac451648 | call/name | `module.scrimmage.to_scrimmage` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/scrimmage.py:29` |
| op-c1d7a1b155d57a03 | call/name | `module.shop.common_shop.calculate_left_assets` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:51` |
| op-4e8c7887bd502c3a | call/name | `module.shop.common_shop.calculate_one_time_assets` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:24` |
| op-cc4e828033eb120f | call/name | `module.shop.common_shop.to_refresh` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/common_shop.py:61` |
| op-0b7247f2b7acf065 | call/name | `module.shop.shop_utils.ensure_choose` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:118` |
| op-c7f2d9386c3a7dcc | call/name | `module.shop.shop_utils.get_item_position` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:96` |
| op-a11abce4b223dff7 | call/name | `module.shop.shop_utils.swipe_get_y_diff` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:127` |
| op-83d123c3028d2784 | call/name | `module.shop.tactical_challenge_shop.confirm_refresh` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/tactical_challenge_shop.py:65` |
| op-9a16d0d1bc6ec100 | call/name | `module.shop.tactical_challenge_shop.get_tactical_challenge_assets` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/tactical_challenge_shop.py:21` |
| op-d0699df10225dea8 | call/name | `module.shop.tactical_challenge_shop.to_refresh` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/tactical_challenge_shop.py:62` |
| op-86015df7955f5bc3 | call/name | `module.shop.tactical_challenge_shop.to_shop_menu` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/tactical_challenge_shop.py:51` |
| op-78a8cb9231c04f18 | call/name | `module.total_assault.calc_acc` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:332` |
| op-ffbe7a63840a8692 | call/name | `module.total_assault.collect_accumulated_point_reward` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:58` |
| op-e1a12b337d435903 | call/name | `module.total_assault.collect_season_reward` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:57` |
| op-8b0e0c0601b634b2 | call/name | `module.total_assault.detect_level_y` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:70` |
| op-9583949a91db6b8e | call/name | `module.total_assault.enter_fight` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:96` |
| op-7a50c4bea87847f3 | call/name | `module.total_assault.fight_difficulty_x` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:27` |
| op-c3c85c7123d06ee6 | call/name | `module.total_assault.find_button_y` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:87` |
| op-ba5a32c31869a5d2 | call/name | `module.total_assault.finish_existing_total_assault_task` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:25` |
| op-79704cc49938e509 | call/name | `module.total_assault.get_fight_result` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:98` |
| op-6ed71472b7e3796b | call/name | `module.total_assault.get_total_assault_tickets` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:14` |
| op-88038c433b209f27 | call/name | `module.total_assault.give_up_current_fight` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:43` |
| op-8288ea540c6d40ef | call/name | `module.total_assault.judge_and_collect_reward` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:191` |
| op-094259b156ca7466 | call/name | `module.total_assault.judge_formation_usable` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:119` |
| op-c72ed9f73d0d8c37 | call/name | `module.total_assault.judge_unfinished_fight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:24` |
| op-df34d6ed11b9fe09 | call/name | `module.total_assault.one_detect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:158` |
| op-99646034c3fbb473 | call/name | `module.total_assault.start_sweep` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:55` |
| op-ac83112e46c89793 | call/name | `module.total_assault.to_formation_edit_i` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:91` |
| op-1a26184c643847a5 | call/name | `module.total_assault.to_room_info` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:90` |
| op-a83500932b403937 | call/name | `module.total_assault.to_total_assault` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:13` |
| op-1f8f19a65e3d6fdb | call/name | `module.total_assault.to_total_assault_info` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:183` |
| op-59cc79d0ecae4a72 | call/name | `module.total_assault.total_assault_highest_difficulty_button_detection` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:26` |
| op-156b117e3158a9a8 | call/name | `module.total_assault.total_assault_highest_difficulty_button_judgement` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/total_assault.py:344` |
| op-d535d6458520f66c | call/name | `monitor_from_window` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:162` |
| op-c1474bdad88a32d4 | call/name | `mumu12_control_api_backend` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/get_adb_address.py:30` |
| op-d7bd280b2a86bbfd | call/name | `ocr_str_replace_func` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/image.py:252` |
| op-29ab12dcb7e2676a | call/name | `operation` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_local_device.py:10` |
| op-701a2e24965f3a97 | call/name | `original_android_language` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:256` |
| op-8d716349201b8866 | call/name | `original_check_atx` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:261` |
| op-cee7fd31c19479d9 | call/name | `original_check_resolution` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:300` |
| op-21755d336bc5e513 | call/name | `original_connection_init` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:410` |
| op-b8554da31d7d2e88 | call/name | `original_control_init` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:443` |
| op-34b3a45b5595baeb | call/name | `original_find_student_position` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:592` |
| op-4d793e2816871da5 | call/name | `original_get_current_package` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:424` |
| op-e78cf3c91f93e289 | call/name | `original_gift_to_cafe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:569` |
| op-133fae31355a6c65 | call/name | `original_init` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:99` |
| op-55b7526dae2be5af | call/name | `original_log` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:130` |
| op-21db5b8c3b3c2417 | call/name | `original_out` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:116` |
| op-ef1b4fb7169ac460 | call/name | `original_screenshot_init` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:459` |
| op-750a21272736a569 | call/name | `original_start_emulator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:246` |
| op-0b8a6f07f25b6b59 | call/name | `original_swipe` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:310` |
| op-d1dcbbba9ba4ac90 | call/name | `original_swipe_gift_and_screenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:575` |
| op-31a7d24e46dba9b5 | call/name | `original_u2_init` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:475` |
| op-fae5f1a7487ce564 | call/name | `original_wait_uiautomator_start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:278` |
| op-e98398f37e1129d2 | call/name | `perform_business_resume` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_provider.py:44` |
| op-872bdcb223da05fa | call/name | `prepare_service_imports` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:64` |
| op-9b04804171a6e5a4 | call/name | `preprocess_name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/device_config.py:16` |
| op-42a3472085df8b72 | call/name | `previous` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:107` |
| op-02fe2d40710454ac | call/name | `previous_sys_hook` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:223` |
| op-5c402c5660784fda | call/name | `previous_thread_hook` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:236` |
| op-ba5d1b230148b8a0 | call/name | `process_native_api` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:85` |
| op-45b1d1e155d9ae99 | call/name | `progress` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:657` |
| op-fff90d4b61bcb815 | call/name | `progress_bar` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:196` |
| op-03ea1d22e2d3eed5 | call/name | `read_registry_key` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/start_simulator.py:36` |
| op-76eeae27712bea0d | call/name | `record_line_y` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/shop/shop_utils.py:185` |
| op-3a061b26ca24fb27 | call/name | `recurse_widgets` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:259` |
| op-4f036144c8d4d8dd | call/name | `recursive_merge` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:197` |
| op-e490f0d70ff68435 | call/name | `repo_sha_test_configs` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:656` |
| op-52e34f6a730d0079 | call/name | `resolve_config_dir` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:31` |
| op-2188069da4543ee6 | call/name | `return_bluestacks_type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/emulator_manager/auto_scan_simulator.py:48` |
| op-ac541844449660a3 | call/name | `scenario` | 14 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:82` |
| op-1e58e28d052ed62b | call/name | `seen_add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/config_translation_generator.py:41` |
| op-cc02c20f16f2d0a6 | call/name | `send_stream_json` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/ws_provider.py:35` |
| op-480e7790dbe074ad | call/name | `service.android_local_device._with_uiautomator_retry` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_local_device.py:28` |
| op-bfde1bed6f174b76 | call/name | `service.android_ocr_client.BaasOcrClient` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:489` |
| op-da3d75879aecab39 | call/name | `service.android_ocr_client.ServerConfig` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:120` |
| op-7b820cf8b3d9218e | call/name | `service.android_ocr_client._android_library_abi_dir` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:127` |
| op-e2b0e1bae2848a9a | call/name | `service.android_ocr_client._android_ocr_branch` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:83` |
| op-b86e85ecd8916ebc | call/name | `service.android_ocr_client._is_android_runtime` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:111` |
| op-4c4cdeb5eab49ed2 | call/name | `service.android_ocr_client._server_folder_path` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:103` |
| op-8eb82263dba37967 | call/name | `service.android_ocr_installer.OcrRepoManager` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:396` |
| op-596811ed30221b85 | call/name | `service.android_ocr_installer._android_internal_binary_path` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:206` |
| op-6a11ed27606f07ff | call/name | `service.android_ocr_installer._android_internal_runtime_root` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:121` |
| op-f0858adf86e6cf17 | call/name | `service.android_ocr_installer._android_internal_version_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:143` |
| op-16257543d573dcec | call/name | `service.android_ocr_installer._android_library_abi_dir` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:109` |
| op-3946581d10d47af8 | call/name | `service.android_ocr_installer._android_ocr_branch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:64` |
| op-7046a1e79e035534 | call/name | `service.android_ocr_installer._download_android_archive` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:223` |
| op-32e64800e3e512b5 | call/name | `service.android_ocr_installer._find_android_archive_root` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:228` |
| op-234868456479695f | call/name | `service.android_ocr_installer._get_android_remote_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:202` |
| op-ae7d1e5c9679011d | call/name | `service.android_ocr_installer._install_android_prebuild` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:393` |
| op-c29dbd6d16a5a232 | call/name | `service.android_ocr_installer._is_android_runtime` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:392` |
| op-f641a3412580c91e | call/name | `service.android_ocr_installer._read_android_installed_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:203` |
| op-fedf7bf0bd0d8ba9 | call/name | `service.android_ocr_installer._read_android_internal_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:205` |
| op-6d04f38ef0a084c1 | call/name | `service.android_ocr_installer._server_binary_path` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:204` |
| op-df47842ebc2ce8bb | call/name | `service.android_ocr_installer.noninteractive_git_env` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_installer.py:274` |
| op-699bc27b263ae7cd | call/name | `service.api.commands._require_config_id` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/commands.py:46` |
| op-90fe96f5a1d06b4d | call/name | `service.api.http._css_identifier` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:301` |
| op-f8bf03912bbd8a5b | call/name | `service.api.http._find_css_block_end` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:311` |
| op-c79af8b3d0c8654b | call/name | `service.api.http._require_android_loopback` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:101` |
| op-ed737b009214a830 | call/name | `service.api.http._require_loopback` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:80` |
| op-d0214c4965c6793c | call/name | `service.api.http._resolve_wiki_target` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:122` |
| op-0c788a74af0e6a0a | call/name | `service.api.http._rewrite_css_urls` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:243` |
| op-a6ed15ffc0671232 | call/name | `service.api.http._rewrite_wiki_css` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:146` |
| op-4c1a78ee2c199feb | call/name | `service.api.http._rewrite_wiki_html` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:143` |
| op-e46b9735f7c211f1 | call/name | `service.api.http._unwrap_css_layers` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:243` |
| op-9af725a20f19f237 | call/name | `service.api.http._wiki_proxy_url` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/http.py:213` |
| op-3c7e9af255c9e867 | call/name | `service.api.security.auth_ok_payload` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:139` |
| op-8866383478641322 | call/name | `service.api.security.begin_server_hello` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:216` |
| op-b469b6c508cf579e | call/name | `service.api.security.decode_control_auth_proof` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:159` |
| op-6d140dff87968f47 | call/name | `service.api.security.is_allowed_origin` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:100` |
| op-f5602b964346d8f7 | call/name | `service.api.security.json_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/security.py:76` |
| op-0a952d43df83aa83 | call/name | `service.api.state.LazyServiceContext` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/state.py:29` |
| op-8e7a41d6ddca8263 | call/name | `service.conf.manager.ResourceSnapshot` | 5 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/manager.py:93` |
| op-b19936eba9739b3c | call/name | `service.conf.ops._normalize_config_id` | 5 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/ops.py:7` |
| op-cd5bb6d57790df2d | call/name | `service.conf.ops._normalize_config_ids` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/ops.py:35` |
| op-279fb46360d6d4ec | call/name | `service.conf.paths.ConfigPathError` | 3 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/paths.py:24` |
| op-0bc5d78cc7814c7a | call/name | `service.conf.paths.ensure_safe_config_id` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `service/conf/paths.py:43` |
| op-643575b2374c04c1 | call/name | `service.context._env_bool` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:62` |
| op-3d06338867ff02c6 | call/name | `service.context._setup_no_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:61` |
| op-4c68b6f5259a895c | call/name | `service.example.main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service.example.py:20` |
| op-cbc687971cded061 | call/name | `service.injection._ensure_logger_extensions` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:102` |
| op-e09044cc0b638ef6 | call/name | `service.injection._env_enabled` | 13 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:71` |
| op-80c43be74b05bd11 | call/name | `service.injection._install_android_ocr_modules` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:80` |
| op-c5e390da93c9ece9 | call/name | `service.injection._install_gui_stubs` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:79` |
| op-593e90524e089750 | call/name | `service.injection._patch_baas_thread` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:618` |
| op-7fb65e7261c446b5 | call/name | `service.injection._patch_cafe_reward` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:619` |
| op-3ef11f07290aa58b | call/name | `service.injection._patch_device_modules` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:617` |
| op-fb06c51645751924 | call/name | `service.injection._patch_logger` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:615` |
| op-71966468502b21b5 | call/name | `service.injection._patch_main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:616` |
| op-ed044b99dadcd37a | call/name | `service.injection._supports_parameter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:98` |
| op-656255fe989edba5 | call/name | `service.runtime.ConfigSession` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:336` |
| op-ad5670a3557a4475 | call/name | `service.runtime._AndroidDisplayResizeGuard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:212` |
| op-985edeb99c27799a | call/name | `service.runtime._SignalHook` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:323` |
| op-e5acee77c210e71e | call/name | `service.runtime._coerce_sha_test_timeout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:631` |
| op-baa2d24db9be8748 | call/name | `service.runtime._default_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:335` |
| op-72b6cc2d825eddc1 | call/name | `service.runtime._is_android_runtime` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:111` |
| op-db7b1afaff89062a | call/name | `service.runtime.run_blocking` | 24 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:291` |
| op-6b651298318a5b0b | call/name | `service.system_logging.JsonLineFormatter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:80` |
| op-cc6ba2c4ffa91f29 | call/name | `service.system_logging._install_exception_hooks` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:84` |
| op-6f172c28c9fc8c20 | call/name | `service.system_logging._parse_log_line` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:132` |
| op-874d9538c695c305 | call/name | `service.system_logging.configure_dependency_log_levels` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:66` |
| op-2dbcfd30da491393 | call/name | `service.system_logging.system_log_path` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:61` |
| op-47b28109f26a237e | call/name | `service.transport.framing.encode_frame` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/framing.py:25` |
| op-a500eab11c1dc130 | call/name | `service.transport.pipe_server._PipeProtocol` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:122` |
| op-bb701f643556c36c | call/name | `service.update.checks.GitOperationHandler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:223` |
| op-9462a927be8c872a | call/name | `service.update.checks.VersionInfo` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:472` |
| op-8d7832a72dbffa3c | call/name | `service.update.checks._android_archive_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:668` |
| op-a50648d436a5750c | call/name | `service.update.checks._android_http_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:351` |
| op-b760821fe89a0e73 | call/name | `service.update.checks._android_update_to_latest` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:727` |
| op-04a7d6dbe268ddcb | call/name | `service.update.checks._copy_android_update_tree` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:704` |
| op-ade0d5b31af91159 | call/name | `service.update.checks._format_expired_time` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:425` |
| op-641502143ece7c3e | call/name | `service.update.checks._git_wrapper_get_latest_sha` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:307` |
| op-f95dac0c663bc26c | call/name | `service.update.checks._github_api_get_latest_sha` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:319` |
| op-50838d83515319ee | call/name | `service.update.checks._github_archive_config` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:514` |
| op-03e08de13993185d | call/name | `service.update.checks._github_archive_url_for_config` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:305` |
| op-b46b12b7656bc081 | call/name | `service.update.checks._github_repo_from_url` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:256` |
| op-2f81125e947160d1 | call/name | `service.update.checks._is_android_runtime` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:350` |
| op-cfdd647181d0a33a | call/name | `service.update.checks._is_noninteractive_auth_failure` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:114` |
| op-0e6a70bb2bfd4428 | call/name | `service.update.checks._mirrorc_api_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:349` |
| op-8341c88a4e29f446 | call/name | `service.update.checks._probe_android_archive_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:309` |
| op-06033650bc0303fa | call/name | `service.update.checks._select_remote_record` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:550` |
| op-b2d5bffa3e3eeb75 | call/name | `service.update.checks._setup_channel` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:327` |
| op-29cd08ac84bdf9a0 | call/name | `service.update.checks.get_local_version` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:527` |
| op-da594405df6e1d66 | call/name | `service.update.checks.noninteractive_git_env` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:86` |
| op-e0789c2bb7a3c76c | call/name | `service.update.checks.repo_sha_test_configs` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:337` |
| op-60d3540a13f4972d | call/name | `service.update.checks.test_all_repo_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:549` |
| op-85aa7c89b84dfa59 | call/name | `service.update.checks.test_repo_sha` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:338` |
| op-0f4572f90fd42bd0 | call/name | `service.update.checks.update_to_latest_with_progress` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:742` |
| op-3ea73bd87a42718c | call/name | `service.update.repository.GitOperationHandler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:326` |
| op-2a11b1773a46e8d1 | call/name | `service.update.repository.UpdateManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:635` |
| op-e651985a6757dc5a | call/name | `service.update.repository.noninteractive_git_env` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:160` |
| op-6f6ce1ddbb21ced2 | call/name | `service.update.setup_schema._first_bool` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:115` |
| op-8891f449f9909fcb | call/name | `service.update.setup_schema._first_string` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:89` |
| op-9e338d46ee13dc7b | call/name | `service.update.setup_schema._table` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:64` |
| op-623b1ffc670a6ff6 | call/name | `service.update.setup_schema.legacy_repo_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:194` |
| op-498b5169c4d565c7 | call/name | `service.update.setup_schema.migrate_to_current_schema` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:184` |
| op-60e991cae4145d04 | call/name | `service.update.setup_schema.setup_channel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/setup_schema.py:94` |
| op-371ee75dd4fed070 | call/name | `service.utils.diff.PatchConflictError` | 16 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:23` |
| op-ed307b49a82f1411 | call/name | `service.utils.diff._diff_dict` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:129` |
| op-1db69e98b24ca8ce | call/name | `service.utils.diff._escape_segment` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:113` |
| op-643bba521f82deba | call/name | `service.utils.diff._resolve_parent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:68` |
| op-aa5f92c1fb6e0e7b | call/name | `service.utils.diff._split_path` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:57` |
| op-2ed35f881cd602f7 | call/name | `service.utils.diff._unescape_segment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:25` |
| op-70d951a0f11b9555 | call/name | `service.utils.diff.diff_documents` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/diff.py:122` |
| op-9be8b55ad7aa353b | call/name | `session_nonce` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:29` |
| op-0025017f87f6eb71 | call/name | `set_log` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:378` |
| op-b30d610dbfd8a7da | call/name | `start_serving_pipe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:121` |
| op-aea5565215c978d4 | call/name | `success` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:372` |
| op-803229dca4ce2252 | call/name | `super_model_dump` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/types.py:14` |
| op-6085c29c5e545ee1 | call/name | `swipe_search_target_str` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/activities/activity_utils.py:200` |
| op-cca4f10d8120e7de | call/name | `test_one` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:655` |
| op-858740b7fe08266d | call/name | `tests.core.device.test_uiautomator2_client.make_initer` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/device/test_uiautomator2_client.py:36` |
| op-39de9571bcd60442 | call/name | `tests.core.ocr.test_android_runtime._Logger` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/core/ocr/test_android_runtime.py:37` |
| op-48c8869e20d3a161 | call/name | `tests.module.test_cafe_reward_match._install_cafe_injection` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/module/test_cafe_reward_match.py:25` |
| op-00374baab8e48370 | call/name | `tests.service.pipe_live_smoke.main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:61` |
| op-1e596d1fa96a224c | call/name | `tests.service.pipe_live_smoke.read_json` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:33` |
| op-bce4bb59c07618c9 | call/name | `tests.service.pipe_live_smoke.run` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/pipe_live_smoke.py:57` |
| op-0448f554bb5083e4 | call/name | `tests.service.test_android_display_resize._FakeDevice` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:41` |
| op-ae60010445ebbc80 | call/name | `tests.service.test_android_display_resize._FakeLogger` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:123` |
| op-948658d6e68c767a | call/name | `tests.service.test_auth_manager._cleanup` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:53` |
| op-e8ad99ad69f78c09 | call/name | `tests.service.test_auth_manager._workspace_tmp` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_auth_manager.py:34` |
| op-bdb2601ef95c8014 | call/name | `tests.service.test_commands._Runtime` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_commands.py:36` |
| op-aff7949bfba9ef08 | call/name | `tests.service.test_commands._cmd` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_commands.py:39` |
| op-ed39dcdda50b1031 | call/name | `tests.service.test_config_runtime_update._cleanup` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:70` |
| op-3c81f34e7f3c5fd6 | call/name | `tests.service.test_config_runtime_update._workspace_tmp` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:56` |
| op-54d4e7074f2f4c42 | call/name | `tests.service.test_config_runtime_update._write_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_config_runtime_update.py:200` |
| op-1f14a5f97a69a42e | call/name | `tests.service.test_http_contract._AuthManager` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_http_contract.py:53` |
| op-1215087bb259bdd2 | call/name | `tests.service.test_http_contract._PasswordState` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_http_contract.py:19` |
| op-c0e7cf2aa0ca9050 | call/name | `tests.service.test_http_contract._Runtime` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_http_contract.py:45` |
| op-c0b725bc0eeba2cf | call/name | `tests.service.test_http_contract._client` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_http_contract.py:53` |
| op-fd17726ac9fab5cb | call/name | `tests.service.test_pipe_transport._read_frame` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_pipe_transport.py:67` |
| op-d9acfe93c03d95db | call/name | `tests.service.test_remote_proxy._Client` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_remote_proxy.py:41` |
| op-d404e8daaa078501 | call/name | `tests.service.test_remote_proxy._Stream` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_remote_proxy.py:42` |
| op-62a9259af94e010a | call/name | `tests.service.test_scrcpy_client._Device` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_scrcpy_client.py:38` |
| op-c5c194b8c8735c2c | call/name | `tests.service.test_security_contract._AuthManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:122` |
| op-9fb28951f1151fd0 | call/name | `tests.service.test_security_contract._PasswordState` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:22` |
| op-3810df8ba5ef94b8 | call/name | `tests.service.test_security_contract._PreauthChannel` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:130` |
| op-dd3f9e01aacf79ce | call/name | `tests.service.test_security_contract._Stream` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:101` |
| op-081593cebb51cbaa | call/name | `tests.service.test_security_contract._StreamWebSocket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:100` |
| op-a03c31ab4184e06c | call/name | `tests.service.test_security_contract._WebSocket` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:124` |
| op-74c3b8a782ff19e6 | call/name | `tests.service.test_service_injection._install_fake_core` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_injection.py:28` |
| op-1d472ae4223c26c4 | call/name | `tests.service.test_service_logic._schedule_event` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:62` |
| op-bfca0b38752d088f | call/name | `tests.service.test_service_logic._write_config_pair` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_logic.py:136` |
| op-03ac8365c31d5ddf | call/name | `tests.service.test_system_logging._write_entry` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_system_logging.py:90` |
| op-364894fe58557e77 | call/name | `tests.service.test_ws_behaviors._Stream` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:31` |
| op-00c48bc55b6c0fe5 | call/name | `tests.service.test_ws_behaviors._WebSocket` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_behaviors.py:29` |
| op-a877617d08cbc7bf | call/name | `tests.service.test_ws_sync_conflict._ConfigManager` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_sync_conflict.py:26` |
| op-00a7f10187d1d8d1 | call/name | `text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:309` |
| op-a2239c07ff58fe85 | call/name | `try_sources` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:389` |
| op-e46af2a7a203fa3a | call/name | `unix_timestamp_ms` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:176` |
| op-acf7fe3e429260c1 | call/name | `update_to_latest_with_progress` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:874` |
| op-b61da13bd7cd635f | call/name | `wait_closed` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:143` |
| op-4b2fcee2724ddb14 | call/name | `window.BAASLangAltButton` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:353` |
| op-b123121ee1530340 | call/name | `window.BAASTabBar` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:346` |
| op-c5a84b0d92e3a3f9 | call/name | `window.BAASTabItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:281` |
| op-01f450152262de07 | call/name | `window.BAASTitleBar` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:507` |
| op-0732095ec2136d3a | call/name | `window.RenameDialogBox` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:257` |
| op-3cd340457669ddb8 | call/name | `window.Window` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:633` |
| op-99c25d178531ce34 | call/name | `window.check_and_update_user_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:174` |
| op-8d1c63eb1b379be5 | call/name | `window.check_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:410` |
| op-f418f7986fc86d0e | call/name | `window.check_event_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:177` |
| op-d62c983de1da7c1e | call/name | `window.check_single_event` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:143` |
| op-de4d3869a4b35d91 | call/name | `window.check_static_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:168` |
| op-84635c8358358030 | call/name | `window.check_switch_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:178` |
| op-2bfd109986c016fd | call/name | `window.delete_deprecated_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:165` |
| op-b56b3dee410a4cd3 | call/name | `window.update_config_reserve_old` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:101` |
| op-11316001a046c082 | call/self | `self.Baas_instance.handle_resolution_dynamic_change` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Screenshot.py:55` |
| op-37a2959bb2e1e9be | call/self | `self.General.runtime_path.replace` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:219` |
| op-f7cc9f18c4cf90ec | call/self | `self.HBoxLayout.addLayout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:78` |
| op-9493f9f56c584c21 | call/self | `self.MODE_DICT.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:33` |
| op-aef5624a84bbcd59 | call/self | `self.MODE_DICT.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:34` |
| op-70756cb78f3f5ba8 | call/self | `self.Main.get_thread` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:357` |
| op-3e73484f9bb1655b | call/self | `self.PROPERTY.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:21` |
| op-8ac6bf86d8a6acd4 | call/self | `self.PROPERTY.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:84` |
| op-45d56d4d262eb0d6 | call/self | `self.REPO_URL_CHECK_UPDATE_METHOD_mapping.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:566` |
| op-017d62a9074069ba | call/self | `self.REPO_URL_NAME_mapping.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:590` |
| op-07a288a8f205436d | call/self | `self.REPO_URL_NAME_mapping.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:114` |
| op-1b02b32502db8039 | call/self | `self.REPO_URL_NAME_mapping.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:253` |
| op-4c45cd7f68aa7efb | call/self | `self.VBoxLayout.addLayout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:81` |
| op-5707bc53e8171bc4 | call/self | `self.VBoxWrapperLayout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:86` |
| op-22f9499090c383b8 | call/self | `self.VK_TO_KEY_MAP.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:247` |
| op-fc63757c4716ed5c | call/self | `self._BAAS_local_version_label.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:345` |
| op-2d479ffda06f0932 | call/self | `self._BAAS_local_version_method_label.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:353` |
| op-cf33fd716481dc38 | call/self | `self._BAAS_local_version_state_label.setWordWrap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:187` |
| op-cd2b829576cb973a | call/self | `self._BAAS_remote_version_label.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:603` |
| op-2817ee6d9c57e4c2 | call/self | `self._BAAS_remote_version_method_label.setText` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:605` |
| op-04626a309b96ad72 | call/self | `self.__accept_hard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:138` |
| op-1226e0d8b81091b1 | call/self | `self.__adb_to_websocket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:536` |
| op-4e88c881c695daeb | call/self | `self.__adjust_raw` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:152` |
| op-d888b51079695d8d | call/self | `self.__async_emit_toggle_change_signal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:238` |
| op-11790f13eb062c2a | call/self | `self.__async_post_init_process` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:139` |
| op-56094421a5582cb3 | call/self | `self.__build_server_command` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:315` |
| op-6965daba45c5422a | call/self | `self.__config_translation.entries.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:35` |
| op-eb863c41a45e4dd1 | call/self | `self.__connectSignalToSlot` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:113` |
| op-4a6a974b9e931552 | call/self | `self.__deploy_server` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:202` |
| op-1e2a90b1c46bacf0 | call/self | `self.__find_expected_server_pids` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:300` |
| op-7445858dc3438290 | call/self | `self.__get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:77` |
| op-b019a2d59550a89d | call/self | `self.__get_cmdline` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:219` |
| op-5228b1a34f1af156 | call/self | `self.__initLayout` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:112` |
| op-f4cd91be31323bde | call/self | `self.__initWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:45` |
| op-1c6465f22a9cc124 | call/self | `self.__init_Signals_and_Slots` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:100` |
| op-0ca227fd0c522440 | call/self | `self.__init_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:23` |
| op-a34e930201d9d38f | call/self | `self.__init_display_config_layout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:94` |
| op-7d0b3930d9a9d4de | call/self | `self.__init_enableFavorStudent_check_box_layout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:145` |
| op-d4497aa8e82965d1 | call/self | `self.__init_favorite_student_layout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:146` |
| op-71c572828d792c21 | call/self | `self.__init_layouts` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:94` |
| op-797f2a2f7c73ae6c | call/self | `self.__init_relationship_check_box_layout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:147` |
| op-f14068c5d2939918 | call/self | `self.__init_server_connection` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:203` |
| op-9b8674b3f91dd36a | call/self | `self.__is_expected_server_pid` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:246` |
| op-c8994e71e6489b67 | call/self | `self.__out__` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:105` |
| op-307e89ececbbf316 | call/self | `self.__read_server_pid_file` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:241` |
| op-ee5bc897b3d4c594 | call/self | `self.__read_valid_server_pid_file` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:287` |
| op-dc1afb6d06e1d0de | call/self | `self.__remote_socket.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:425` |
| op-65a96b2b1b6f131f | call/self | `self.__remote_socket.recv` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:504` |
| op-730a102cc5d26905 | call/self | `self.__remote_socket.send` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:497` |
| op-694f69c883e9eb69 | call/self | `self.__send_to_listeners` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:205` |
| op-30c11dfbfec7b063 | call/self | `self.__server_process.kill` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:452` |
| op-f95e5ab3d0a4bc6c | call/self | `self.__server_process.poll` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:447` |
| op-d799d306bbe3b9c7 | call/self | `self.__server_process.terminate` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:448` |
| op-4e0d8f619356767b | call/self | `self.__server_process.wait` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:449` |
| op-747335f7559c6463 | call/self | `self.__server_stream.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:222` |
| op-8dd9db10da8848ce | call/self | `self.__server_stream.read` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:190` |
| op-1384a8b612921072 | call/self | `self.__set_config_and_display_message` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:363` |
| op-0b473ab7a2dbedac | call/self | `self.__shell` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:198` |
| op-959f16cadfdfbf96 | call/self | `self.__stream_loop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:213` |
| op-43288196b55fa877 | call/self | `self.__students.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:89` |
| op-4185dee01a48d804 | call/self | `self.__test__` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:168` |
| op-cf1b3524b66767e3 | call/self | `self.__to_text` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:163` |
| op-f0db3ff85b39b43f | call/self | `self.__transpose__` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:663` |
| op-3194e4ee1820439a | call/self | `self.__video_socket.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:234` |
| op-c10c314f160ddda6 | call/self | `self.__video_socket.recv` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:127` |
| op-2867531880c67882 | call/self | `self.__video_socket.setblocking` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:140` |
| op-0411f9369251cb14 | call/self | `self.__websocket_to_adb` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:537` |
| op-853435dcf8a3bcd5 | call/self | `self._aad` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:78` |
| op-fb24d257d553dd20 | call/self | `self._accept_resolution` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1092` |
| op-d84c5de7f9679b10 | call/self | `self._active_tasks.add` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:48` |
| op-fd209ff056fea284 | call/self | `self._active_tasks.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:94` |
| op-1d7217a38aa69c4b | call/self | `self._active_tasks.discard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:99` |
| op-7d7f08cbd8564137 | call/self | `self._add_top_rounded_corners` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:98` |
| op-01a4c7f1467a284c | call/self | `self._adjustViewSize` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:235` |
| op-029fac2b6423a839 | call/self | `self._android_dependency_libs.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:298` |
| op-934f8b67b86006cf | call/self | `self._android_display_guard.release` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:399` |
| op-0b5e5b15fc49ae36 | call/self | `self._android_server_lib.stop_server` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:337` |
| op-c5f85a26354b102b | call/self | `self._android_server_library_path` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:115` |
| op-e3de81d3f8df18e6 | call/self | `self._android_server_thread.join` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:339` |
| op-155276d5157fde3a | call/self | `self._android_server_thread.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:316` |
| op-3ee527a73130fe61 | call/self | `self._android_system_load` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:303` |
| op-046b669a48fc7a6f | call/self | `self._apply_config_to_layout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:311` |
| op-1faedf4c200b57ec | call/self | `self._archive_root_prefix` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:824` |
| op-1ef125b78f8bd11b | call/self | `self._async_lock` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:91` |
| op-f4af7a89ba367155 | call/self | `self._auth_context` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:150` |
| op-0ac7a3a3b0759a36 | call/self | `self._broadcast.publish` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:114` |
| op-0d55fb6bf4f54ca2 | call/self | `self._broadcast.set_loop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:37` |
| op-d5addd0f1914595a | call/self | `self._broadcast.subscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:141` |
| op-6bc1e4b2eeb02fd8 | call/self | `self._broadcast.unsubscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:144` |
| op-78ed64333d2375a0 | call/self | `self._buffer.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/framing.py:33` |
| op-42f465fc74641418 | call/self | `self._build_control_channel` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:154` |
| op-22736e22caa258be | call/self | `self._callback` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:85` |
| op-d380f82286be1881 | call/self | `self._cdk_test_thread.finished.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:399` |
| op-201d1cfcfc4ccce9 | call/self | `self._cdk_test_thread.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:400` |
| op-61f0ea56b9f89d89 | call/self | `self._cdk_test_thread.wait` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:548` |
| op-a06ae69c5db62ac5 | call/self | `self._change_hotkey` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:155` |
| op-6b2fb995803dca13 | call/self | `self._check_and_display_update_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:614` |
| op-17c3b415c5c24e93 | call/self | `self._check_resource` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:422` |
| op-669b08b91fe67397 | call/self | `self._check_single_horizontal_line` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/geometry/triangle.py:37` |
| op-25d9674578d5ed4e | call/self | `self._classify_config_change` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:536` |
| op-ad62ea2169d7cea3 | call/self | `self._click` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:20` |
| op-b340f7a70c531564 | call/self | `self._client_size` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:118` |
| op-099f3e9da107c9d2 | call/self | `self._collect_revocation_queues` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:247` |
| op-b06e237f86fb7572 | call/self | `self._commit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:117` |
| op-db32c6b95f1e5f80 | call/self | `self._commit_change` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:80` |
| op-93d294a882ab1953 | call/self | `self._common_shop_config_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:99` |
| op-e5c7dc368de90633 | call/self | `self._config_dir.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:39` |
| op-4fbe390a467bccfd | call/self | `self._config_path` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:718` |
| op-1e59bac8adc9cde3 | call/self | `self._config_root.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:74` |
| op-c361b2b69d7c6516 | call/self | `self._config_root.iterdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:76` |
| op-66467a1e41987d43 | call/self | `self._config_root.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:489` |
| op-4195e102a9e9a2c4 | call/self | `self._config_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:41` |
| op-03a9a11ecdbcc29a | call/self | `self._convert_screen_p_to_window_p` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:19` |
| op-faad406459b83f0c | call/self | `self._createMultiComponent` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:37` |
| op-0a335a918b756bb3 | call/self | `self._createNotMultiComponent` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:35` |
| op-4407ae5fd39c6dc5 | call/self | `self._create_card` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:158` |
| op-cc117d79a8cd27ec | call/self | `self._create_config_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:101` |
| op-4a625da5649d9104 | call/self | `self._create_connectivity_test_group` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:129` |
| op-1bb234015ccef7f7 | call/self | `self._create_image` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:54` |
| op-6a1b4081b0103cc7 | call/self | `self._create_repo_settings_group` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:128` |
| op-4840e60a7af812ef | call/self | `self._create_update_method_group` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:127` |
| op-f25b9fbc154daf1b | call/self | `self._create_version_info_group` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:126` |
| op-9974c0ce8541d655 | call/self | `self._current_update_method_label.setWordWrap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:210` |
| op-a55176b0977e96fe | call/self | `self._decoder.feed` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:46` |
| op-bd6212b0f66fdf01 | call/self | `self._detect_update_method_and_update_ui` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:326` |
| op-11f62c30357c5453 | call/self | `self._detect_update_method_thread` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:555` |
| op-56764a572cfa5c00 | call/self | `self._detect_usable_port` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:98` |
| op-0448c4272e942c9d | call/self | `self._device.forward_port` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:231` |
| op-a41b4c3381386985 | call/self | `self._device.package_info` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:129` |
| op-40b5c34df8bae02c | call/self | `self._device.shell` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:92` |
| op-e653978370fad642 | call/self | `self._device.sync.push` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:213` |
| op-934480e0a3ffc5cc | call/self | `self._dispatch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:22` |
| op-84dac3667c445595 | call/self | `self._endpoint.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:97` |
| op-047707918a02ddb3 | call/self | `self._endpoint.connection_lost` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:56` |
| op-30bcf3e225627359 | call/self | `self._endpoint.feed_frame` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:50` |
| op-779406de6cacfad3 | call/self | `self._endpoint.pause_writing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:62` |
| op-00b6606b704ffdf5 | call/self | `self._endpoint.resume_writing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:66` |
| op-a10d29f2650a56e4 | call/self | `self._endpoint.send_json` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:86` |
| op-8bb00c0bd0c34991 | call/self | `self._ensure_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:242` |
| op-5ac38b555f6608ca | call/self | `self._ensure_main` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:310` |
| op-07779060c51099f8 | call/self | `self._ensure_main_sync` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:235` |
| op-6b6b68f77f88f1a7 | call/self | `self._ensure_window` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:18` |
| op-53b909b57a57c5fa | call/self | `self._ev.run_in_executor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:267` |
| op-89acbfb0afc3b4f1 | call/self | `self._ev.run_until_complete` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:282` |
| op-15cb634f3e7c5152 | call/self | `self._event_map_inv.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:953` |
| op-420a3107ad34fe62 | call/self | `self._event_map_inv.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:954` |
| op-0e90438b18a0f551 | call/self | `self._fail` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:52` |
| op-1b123c43fc43aab6 | call/self | `self._fetch_best_remote_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:428` |
| op-fc4d101285354dd5 | call/self | `self._file_path` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:257` |
| op-89d6f242f2c70658 | call/self | `self._fs_task.cancel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:100` |
| op-2aed8fa47432c4b6 | call/self | `self._get_android_device_ocr_language` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:354` |
| op-c15f07480c6d06a6 | call/self | `self._get_android_device_resolution` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1009` |
| op-8d3e2e75611b5ef2 | call/self | `self._get_context` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/api/state.py:26` |
| op-7b0e009d29768025 | call/self | `self._get_host_ocr_language` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:359` |
| op-8388363d427913a2 | call/self | `self._get_local_version` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:762` |
| op-b8a749476ae7c272 | call/self | `self._get_mirror_update_type` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:809` |
| op-d6897bfbdf6e5351 | call/self | `self._get_monitor_rect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:100` |
| op-3f339be0ddab7c38 | call/self | `self._get_or_create_session` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:363` |
| op-543971439d284ead | call/self | `self._get_python_executable` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:963` |
| op-3516b95c6b089dea | call/self | `self._get_remote_version` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:766` |
| op-46afd0931ba42f9d | call/self | `self._get_sha_from_method` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:452` |
| op-bab2908101fc2018 | call/self | `self._get_window_rect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:99` |
| op-f3340da0adc16098 | call/self | `self._git_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:778` |
| op-fa26655cf3d819dc | call/self | `self._github_api_get_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:470` |
| op-46534ed82eca62c9 | call/self | `self._handle_button_signal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:323` |
| op-16098f72df2d8469 | call/self | `self._handle_exit_signal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:325` |
| op-a8c4c52791d5be73 | call/self | `self._handle_message` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:26` |
| op-5971f2bcaf94cb06 | call/self | `self._handle_update_signal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:324` |
| op-23efd7699c867bc8 | call/self | `self._handle_watch_batch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:497` |
| op-44ac891cd8b0367b | call/self | `self._handler_task.cancel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:58` |
| op-29b235c2c7be3d75 | call/self | `self._history_all.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:112` |
| op-34df4cbfd5182701 | call/self | `self._history_per_scope.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:130` |
| op-31a3a516c1d0295d | call/self | `self._history_per_scope.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:134` |
| op-ccc83379b89e782d | call/self | `self._hook_thread.hotkey_triggered.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:308` |
| op-da2242fa1bb36d1a | call/self | `self._hook_thread.isRunning` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:305` |
| op-268c7cec73f64815 | call/self | `self._hook_thread.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:309` |
| op-6949bdf3f8225e49 | call/self | `self._hook_thread.stop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:315` |
| op-31664338ea4ed098 | call/self | `self._hook_thread.wait` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:316` |
| op-40337b1adaa2767b | call/self | `self._hotkey_registry.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:262` |
| op-013d38c6fc5d677c | call/self | `self._hotkey_registry.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:299` |
| op-d8a5d9e767bd7695 | call/self | `self._incoming.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:46` |
| op-da415b95173cb2ad | call/self | `self._incoming.put_nowait` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:27` |
| op-81972d4ef34e21c8 | call/self | `self._init` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ipc_manager.py:40` |
| op-f18cf75c36930cfd | call/self | `self._init_actions` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:312` |
| op-480c8e4d884c272f | call/self | `self._init_all_comps` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:27` |
| op-49a84ad34996395c | call/self | `self._init_android_device` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:27` |
| op-fb14a57b1bc30b8d | call/self | `self._init_app_process` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:25` |
| op-e40bab825fc147e0 | call/self | `self._init_client` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:14` |
| op-ea4df6824f17d554 | call/self | `self._init_components` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:82` |
| op-6572700fd4e1a884 | call/self | `self._init_config` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:29` |
| op-05f217288a509d69 | call/self | `self._init_data_and_state` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:134` |
| op-8c9edd215ec736b6 | call/self | `self._init_detailed_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:135` |
| op-f368b26236869854 | call/self | `self._init_get_remote_sha_method_ComboBox` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:307` |
| op-535d79cd7bbc67c4 | call/self | `self._init_hotkey` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:127` |
| op-6d7d5d053dc6a79a | call/self | `self._init_layout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:95` |
| op-e319587b18a1a699 | call/self | `self._init_region` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:60` |
| op-fb9f98f65e175962 | call/self | `self._init_script` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:336` |
| op-70c6ecf230e2e412 | call/self | `self._init_student_com_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:115` |
| op-cf4d7581fdb21d1f | call/self | `self._init_student_sel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:118` |
| op-e3818ff797c71df3 | call/self | `self._init_table` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:38` |
| op-4f5cbf2ac2923a94 | call/self | `self._init_test_table_content` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:322` |
| op-018e2e7bcfe28e3c | call/self | `self._init_ui` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:38` |
| op-5b40d51a1cdda39a | call/self | `self._init_window` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:27` |
| op-6be7156e8830caa0 | call/self | `self._install_uiautomator_apks` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:244` |
| op-15b907594412d042 | call/self | `self._kill_existing_process` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:973` |
| op-de6ff1396a5ed8f6 | call/self | `self._list_config_ids_sync` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:458` |
| op-9cc9c2727e31a47b | call/self | `self._load_from_disk` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:466` |
| op-5936cecdd6cc4cbd | call/self | `self._load_or_create_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:170` |
| op-7dc06535e6ade352 | call/self | `self._load_or_create_key` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:50` |
| op-efc90292126a758c | call/self | `self._load_password_state` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:48` |
| op-fd10353bf9920e24 | call/self | `self._load_remembered_logins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:55` |
| op-bae2bfee0a74ebf0 | call/self | `self._load_signing_key` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:49` |
| op-e1055f16cdb12552 | call/self | `self._loop.create_task` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:46` |
| op-4395b9a813a9ad36 | call/self | `self._main.init_all_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:243` |
| op-1d6794dec0cb4d10 | call/self | `self._main_thread.init_all_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:361` |
| op-2d854d2d0ea4c292 | call/self | `self._main_thread.logger.info` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:341` |
| op-635829dc0c33d3f6 | call/self | `self._main_thread.send` | 11 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:342` |
| op-caa5d4401a4a79aa | call/self | `self._main_vBoxLayout.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:131` |
| op-d20a7d11a9105629 | call/self | `self._main_vBoxLayout.addWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:202` |
| op-d73de79cee39ceb9 | call/self | `self._make_cleanup` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:49` |
| op-1eac469254ac86ff | call/self | `self._merge_gui` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:355` |
| op-b290a695e394b9c2 | call/self | `self._merge_missing_keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:74` |
| op-206211bf0e313327 | call/self | `self._merge_setup_toml` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:359` |
| op-37e79f2733d48bb9 | call/self | `self._mirrorc_api_get_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:475` |
| op-264b659393060fea | call/self | `self._mirrorc_cdk_TextEdit.setEnabled` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:395` |
| op-4189c690ff07e88b | call/self | `self._mirrorc_cdk_TextEdit.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:221` |
| op-132c983cd4be9a1c | call/self | `self._mirrorc_cdk_TextEdit.setPlaceholderText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:220` |
| op-571d6fabda8a6ce5 | call/self | `self._mirrorc_cdk_TextEdit.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:313` |
| op-9b98556703404408 | call/self | `self._mirrorc_cdk_TextEdit.textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:316` |
| op-9b48803255a4b118 | call/self | `self._mirrorc_cdk_TextEdit.toPlainText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:389` |
| op-121eb58c9794ef26 | call/self | `self._mirrorc_cdk_state_label.setWordWrap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:223` |
| op-e76b4bd5eecbca20 | call/self | `self._mirrorc_install_baas` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:828` |
| op-f8607c5f895ba64b | call/self | `self._mirrorc_invalid_update_button.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:318` |
| op-53eb9ca4f29cf321 | call/self | `self._mirrorc_update_baas` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:822` |
| op-1d754d5afd9d2798 | call/self | `self._modifiers.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:348` |
| op-42de22d1e3b62433 | call/self | `self._modifiers.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:369` |
| op-d81d7541b7a1a519 | call/self | `self._mtimes.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:458` |
| op-8a8345146ee72b49 | call/self | `self._mtimes.pop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:446` |
| op-b44aba927420601b | call/self | `self._new_config_id` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:700` |
| op-4475d517208c1bbf | call/self | `self._new_session` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:153` |
| op-714865b1d19bd46f | call/self | `self._new_session_from_secrets` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:217` |
| op-f647822b5af45fcc | call/self | `self._normalize_android_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:276` |
| op-60de078e619291ef | call/self | `self._normalize_record` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:110` |
| op-ac73cd6532b5b01b | call/self | `self._notify_config_added` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:414` |
| op-1d925e00dd6e32dd | call/self | `self._notify_config_removed` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:417` |
| op-a82ade8ad5b031b7 | call/self | `self._open_channel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:48` |
| op-b03a62090d590eed | call/self | `self._optimize_window` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:30` |
| op-bce9fcc89900de32 | call/self | `self._parse_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:310` |
| op-e974acc11f2de77b | call/self | `self._parse_hotkey_string` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:272` |
| op-09967afa51c334f6 | call/self | `self._parse_settings` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:171` |
| op-5b5de11d52db7e2c | call/self | `self._parse_time` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:429` |
| op-4ba6439aacc03167 | call/self | `self._password_state.as_public_dict` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:85` |
| op-421e56180a2881a3 | call/self | `self._password_state_for` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:142` |
| op-c98c95f25ac4a676 | call/self | `self._paths.file_path` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:65` |
| op-c0b93442c5ca7637 | call/self | `self._periodic_update_check` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:86` |
| op-66c55c0b22636862 | call/self | `self._prepare_android_runtime_folder` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:112` |
| op-5c51c2434a84c644 | call/self | `self._project_gui` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:266` |
| op-123d19da79a0c3e5 | call/self | `self._project_setup_toml` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:270` |
| op-c216b4ddaba3a60d | call/self | `self._prune_expired_remembered_locked` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:206` |
| op-abf1804a09c3ff18 | call/self | `self._publish_queues` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:248` |
| op-9d0e14133cd02932 | call/self | `self._publish_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:348` |
| op-f280bef1e0d326d9 | call/self | `self._pump_async` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:46` |
| op-c977b8d180609983 | call/self | `self._queue_tasks.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:93` |
| op-573e83cac3639413 | call/self | `self._queue_tasks.pop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:55` |
| op-d4882b980e1fdc09 | call/self | `self._read_DeviceOption_ocr_language` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:366` |
| op-7bb0969fd6e2d476 | call/self | `self._read_config` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:23` |
| op-d62cd218bcedf0ae | call/self | `self._read_config_name` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:731` |
| op-5e6f8a3f9e62fee1 | call/self | `self._recreate_table` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:55` |
| op-8c32df9638639c61 | call/self | `self._recv` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:57` |
| op-d41b70c74f842971 | call/self | `self._redirect_stderr` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:60` |
| op-5df61a3ba4e3a535 | call/self | `self._redirect_stdout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:59` |
| op-1fbaa6747c269fbe | call/self | `self._remember_token_hash` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:200` |
| op-4e48bc0097246f4c | call/self | `self._remembered_file.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:531` |
| op-13e6f505dbc4b50f | call/self | `self._remembered_file.read_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:533` |
| op-1efef98c6f439341 | call/self | `self._remembered_file.write_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:561` |
| op-fc20edcd1d8f0873 | call/self | `self._remembered_logins.clear` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:245` |
| op-381f19313b61a65e | call/self | `self._remembered_logins.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:420` |
| op-97ecdf3e10569af2 | call/self | `self._remembered_logins.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:444` |
| op-39ff931619bd63a4 | call/self | `self._remembered_logins.pop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:427` |
| op-8a0e9b78dd847a91 | call/self | `self._remembered_logins.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:558` |
| op-dfe0303482b0852a | call/self | `self._renderer.defaultSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:822` |
| op-2cbcc19e2be16129 | call/self | `self._renderer.render` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:830` |
| op-c87fc368a310cb14 | call/self | `self._replace_library_name` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:180` |
| op-2c5b2b58ef67951e | call/self | `self._repo_url_http_ComboBox.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:253` |
| op-ba01911288d009b2 | call/self | `self._repo_url_http_ComboBox.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:304` |
| op-1c2d19364a8b3c4f | call/self | `self._repo_url_http_ComboBox.currentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:360` |
| op-cc0c3af4209751ed | call/self | `self._repo_url_http_ComboBox.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:303` |
| op-bd9fe5436789f080 | call/self | `self._require_session` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:227` |
| op-eaebfe8e2a54ab23 | call/self | `self._resolve_configured_package` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:393` |
| op-90aaaea002e545ea | call/self | `self._revert_REPO_URL_NAME_mapping.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:361` |
| op-26902c120dc39a66 | call/self | `self._run_git_cmd` | 31 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/server_installer.py:282` |
| op-5947db63853c85ca | call/self | `self._run_handler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:81` |
| op-1cd75027e9f5a43e | call/self | `self._rx.decrypt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:48` |
| op-4814057d36cfb600 | call/self | `self._safe_zip_members` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:823` |
| op-b854136004089389 | call/self | `self._save_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:235` |
| op-b7bafaa61b842d8d | call/self | `self._save_password_state` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:143` |
| op-3b45d9ed70db952a | call/self | `self._save_remembered_logins` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:208` |
| op-e381698d0bee0c06 | call/self | `self._schedule_android_backend_restart` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:898` |
| op-b26fd36561bf1458 | call/self | `self._scrcpy_control.touch` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy_client.py:63` |
| op-512c4a1442a8289e | call/self | `self._sct.grab` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/screenshot/mss.py:14` |
| op-b3f9c1890a69d21c | call/self | `self._send` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:76` |
| op-aeece815256285b8 | call/self | `self._send_frame` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/websocket_endpoint.py:32` |
| op-a820ffb57df0eab3 | call/self | `self._send_queue` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:29` |
| op-d2a8d3abaec7102c | call/self | `self._send_response` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:41` |
| op-795d2da76b4713fb | call/self | `self._send_updates` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:22` |
| op-d169ac0b92fa9895 | call/self | `self._sentinels.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:95` |
| op-1167b8e6a584372a | call/self | `self._sentinels.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:85` |
| op-371069868cea2d90 | call/self | `self._sentinels.pop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:56` |
| op-2dcf587eb7f77fc2 | call/self | `self._sentinels.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:71` |
| op-eb72c47a9ab35cb7 | call/self | `self._servers.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:144` |
| op-78d7a00ef5f9f281 | call/self | `self._sessions.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:487` |
| op-3f7b1c723fcb4d8a | call/self | `self._sessions.get` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:233` |
| op-936f510c9688fd06 | call/self | `self._sessions.keys` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:263` |
| op-6def0e4dff7d77db | call/self | `self._sessions.pop` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:341` |
| op-4ae35c876b945dca | call/self | `self._sessions.values` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:475` |
| op-a8db33f72aedf2ab | call/self | `self._set_icon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:364` |
| op-cf0edf02496a12e9 | call/self | `self._set_port` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:114` |
| op-e420a9bb19628e4b | call/self | `self._set_window_pos` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:139` |
| op-3b4db9b5a38abd94 | call/self | `self._setup_styles` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:123` |
| op-85df52257e5239ba | call/self | `self._shell` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:121` |
| op-290036d21a094bb3 | call/self | `self._signing_file.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:571` |
| op-684d9c8ae1f94aa0 | call/self | `self._signing_file.read_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:571` |
| op-9cc22731d9f53bef | call/self | `self._signing_file.write_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:573` |
| op-258a17e72c79055f | call/self | `self._signing_key.public_key` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:62` |
| op-c1415f8cc2ee88e2 | call/self | `self._signing_key.sign` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:103` |
| op-580c54c428ee1c89 | call/self | `self._snapshots.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:300` |
| op-6a9e59a2aa4c19ee | call/self | `self._snapshots.pop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:448` |
| op-357483fe18859cbc | call/self | `self._sources.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:70` |
| op-1df9b12f00cf0019 | call/self | `self._sources.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:52` |
| op-aae67d873c2f0467 | call/self | `self._sources.values` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/logging.py:135` |
| op-b1098728122a193f | call/self | `self._specialize_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:309` |
| op-1d3cc9cfa00db849 | call/self | `self._start_android_server` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/android_ocr_client.py:249` |
| op-8c2618d2b6ae9b07 | call/self | `self._state_file.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:508` |
| op-adf8f9cf234aec68 | call/self | `self._state_file.read_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:510` |
| op-5bcc6d87df6b45a6 | call/self | `self._state_file.write_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:525` |
| op-9929c37578290a7f | call/self | `self._status_bus.publish_threadsafe` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:245` |
| op-4caffe92a7aa4ac8 | call/self | `self._status_bus.set_loop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:223` |
| op-a63d09c57cb3e1c6 | call/self | `self._status_bus.subscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:273` |
| op-a1365eff1c5780f1 | call/self | `self._status_bus.unsubscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:277` |
| op-2ae230ed76004740 | call/self | `self._statuses.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:988` |
| op-3db8a4ff33de8ec2 | call/self | `self._statuses.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:979` |
| op-a3da5994fb86ad28 | call/self | `self._stop.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:138` |
| op-e2708edfd34eab23 | call/self | `self._stop.set` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:146` |
| op-edb6bd1acfb29864 | call/self | `self._stop.wait` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:132` |
| op-a18739ca4bea0bdd | call/self | `self._subscribers.add` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/broadcast.py:21` |
| op-2a92495dd9a9910f | call/self | `self._subscribers.discard` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/broadcast.py:26` |
| op-2e65c6ad945dd61a | call/self | `self._sync_screenshot_resolution` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:189` |
| op-f6275399fee466ae | call/self | `self._tactical_challenge_shop_config_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:100` |
| op-156997926deff710 | call/self | `self._target_size` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:113` |
| op-23eb972110921194 | call/self | `self._test_and_set_mirrorc_cdk_state` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:314` |
| op-091a1b4fe2be01dc | call/self | `self._test_get_remote_sha_method_push_button.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:323` |
| op-be8733de7f1bad5f | call/self | `self._test_get_remote_sha_method_push_button.setEnabled` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:490` |
| op-b5c73c4dc0783e57 | call/self | `self._test_mirrorc_cdk_button.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:315` |
| op-8d3e16f91b6295aa | call/self | `self._test_mirrorc_cdk_button.setEnabled` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:396` |
| op-02f6bd55925066aa | call/self | `self._thread.is_alive` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:136` |
| op-2451f8d3a5654470 | call/self | `self._thread.join` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:148` |
| op-4a63c01a48b0f835 | call/self | `self._thread.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:143` |
| op-c0bfa02722b37112 | call/self | `self._transport.close` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:90` |
| op-6647c4f6865bd6d7 | call/self | `self._transport.is_closing` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:67` |
| op-b15012a4e0851b0d | call/self | `self._transport.write` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:73` |
| op-25f7e63196403c27 | call/self | `self._trigger_expand` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:211` |
| op-ddb964c18b7d40ab | call/self | `self._try_activate_window` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:191` |
| op-1c3a282312a54044 | call/self | `self._try_git_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:513` |
| op-b26655b4efea9fc7 | call/self | `self._try_mirrorc_update` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:774` |
| op-7e75fd9ff8cfb3d0 | call/self | `self._tx.encrypt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/channels.py:32` |
| op-32dd301ebb7f4bc5 | call/self | `self._unique_config_name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:763` |
| op-10567bb2e17fc9bc | call/self | `self._unix_path.unlink` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_server.py:146` |
| op-b43f05b8223a2e93 | call/self | `self._update_bus.publish` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:386` |
| op-49b4f6386ad7f50d | call/self | `self._update_bus.set_loop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:54` |
| op-6543e3b7b26fe1ce | call/self | `self._update_bus.subscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:399` |
| op-12c0b47211e3c51f | call/self | `self._update_bus.unsubscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:403` |
| op-79289b85a4412268 | call/self | `self._update_check_task.cancel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:105` |
| op-b3d5118f7465ab91 | call/self | `self._update_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:278` |
| op-6550aca8cc774f79 | call/self | `self._update_display` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:351` |
| op-d355b415364e0a05 | call/self | `self._update_pixmap` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:814` |
| op-2aa9fcac371c61c4 | call/self | `self._update_status` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:402` |
| op-dacb314c7397968c | call/self | `self._update_status_label` | 17 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:234` |
| op-d282e3101567369a | call/self | `self._update_ui_after_detection` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:582` |
| op-44f4dda2b6594435 | call/self | `self._valid_task_queue.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:143` |
| op-834b879a6c72966f | call/self | `self._valid_task_queue.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:92` |
| op-7a8656be3f5f425e | call/self | `self._verify_remember_token` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:212` |
| op-7f111e9924c108f5 | call/self | `self._verify_resume_ticket` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:274` |
| op-b0524c025c3c9bf9 | call/self | `self._wait_resolution_change_finish` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1044` |
| op-e52af2c19860f11f | call/self | `self._waitingTaskDisplayQueue.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:125` |
| op-11fc659e6aac1bd8 | call/self | `self._waitingTaskDisplayQueue.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:94` |
| op-0f318bf8d7d0d075 | call/self | `self._warning_svg.setColor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:51` |
| op-08a1bad590d4b6d4 | call/self | `self._warning_svg.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:120` |
| op-a8c68a3ac7c9afa7 | call/self | `self._warning_svg.setFixedSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:115` |
| op-bd4fd20d95bd6657 | call/self | `self._warning_svg.setSizePolicy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:114` |
| op-a616aeda3a1ec4f0 | call/self | `self._window.activate` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:201` |
| op-e20b0afea05c87b3 | call/self | `self._window.activate_window` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:45` |
| op-2b3ec36c35cae22b | call/self | `self._window.get_possible_window_titles` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:48` |
| op-393989fe40b9b5b2 | call/self | `self._window.get_region` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:41` |
| op-5f6a7bbbfc20234f | call/self | `self._window.get_window` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:46` |
| op-487f975f0a664093 | call/self | `self._window.get_window_title` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:51` |
| op-420ef90aceb2b096 | call/self | `self._window.minimize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:193` |
| op-819cfd0687f538a4 | call/self | `self._window.restore` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:200` |
| op-bfd23330e345af13 | call/self | `self._window.update_region` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/pyautogui.py:52` |
| op-82fa23f570187049 | call/self | `self._with_android_backend_restart` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:861` |
| op-a60e2214ceea28bd | call/self | `self._writable.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:40` |
| op-4909e118076e7f64 | call/self | `self._writable.set` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:20` |
| op-9afba4a7e8c425ed | call/self | `self._writable.wait` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:70` |
| op-8ef686dd9354735e | call/self | `self._write_error` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:54` |
| op-06f2e174d6947dbb | call/self | `self._write_json` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:55` |
| op-1ab5eb14ad8094c0 | call/self | `self.accept.clicked.connect` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:51` |
| op-4e10bfe11bcbcd1f | call/self | `self.accept_1.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:52` |
| op-0760669968daacd6 | call/self | `self.accept_2.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:53` |
| op-470b7caa63ed36ab | call/self | `self.accept_3.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:54` |
| op-98e25d9fe5220cb4 | call/self | `self.accept_favor_student.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:136` |
| op-608e347ca6119ed8 | call/self | `self.accept_hard.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:63` |
| op-0bc75588041b638c | call/self | `self.actions.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:321` |
| op-88872ce45b099fe9 | call/self | `self.activate_window` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:31` |
| op-cd9495e145b4ae56 | call/self | `self.adb.shell` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/adb.py:13` |
| op-cb6a2dd0fc3edb20 | call/self | `self.adb_connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:48` |
| op-c8dec75fa0173424 | call/self | `self.adb_getprop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:191` |
| op-dfe439338c3ab521 | call/self | `self.adb_shell_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:232` |
| op-ef1f4a668b8efd39 | call/self | `self.addItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:197` |
| op-42f2b99dce8a9e0d | call/self | `self.addSubInterface` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:41` |
| op-57cb4f73bb64f73e | call/self | `self.addTab` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:492` |
| op-70815f77ea249ece | call/self | `self.add_accept.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:26` |
| op-fd31fc7622da6fb0 | call/self | `self.all_check_box.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:88` |
| op-b9ee623fa6cff1e2 | call/self | `self.alter_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:46` |
| op-48433bd40156fcd0 | call/self | `self.animation.setDuration` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:91` |
| op-104b9dea91adc28a | call/self | `self.animation.setEndValue` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:160` |
| op-42227386027516c5 | call/self | `self.animation.setStartValue` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:159` |
| op-cfa2cc6dd5fc0f0a | call/self | `self.animation.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:161` |
| op-730ff5c89b0bbee0 | call/self | `self.animation.stop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:158` |
| op-5847907850ccbc7f | call/self | `self.app_process_window.is_valid_window` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:66` |
| op-afb1d624faaa9795 | call/self | `self.applied_timestamps.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_ws_sync_conflict.py:17` |
| op-163d835e938c2bb5 | call/self | `self.assertLess` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_normal_task.py:45` |
| op-76366d5ef7696260 | call/self | `self.assets_status.hide` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:105` |
| op-7fcefa317ef370cc | call/self | `self.assets_status.show` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:105` |
| op-f603bbf0dcc66720 | call/self | `self.assets_status.start_patch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:103` |
| op-8699bb58d6372fb8 | call/self | `self.auto_detect_package` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:241` |
| op-4fe2c7123768663a | call/self | `self.autostartSwitch.checkedChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:50` |
| op-4f76e5862c1c262c | call/self | `self.autostartSwitch.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:34` |
| op-0f12035863ecaa44 | call/self | `self.available_packages` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:257` |
| op-c262edc3fc18a891 | call/self | `self.baas.config_set.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:567` |
| op-7a2f187d817278f9 | call/self | `self.baas.logger.info` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:568` |
| op-4d21eb8fd6421b83 | call/self | `self.baas_thread.init_all_data` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_hard_task.py:25` |
| op-56336e5bac8ab8de | call/self | `self.baas_thread.scheduler.getCurrentTaskName` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:102` |
| op-18c41feccb5a0711 | call/self | `self.baas_thread.scheduler.getWaitingTaskList` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:103` |
| op-a350d16e3c826bfe | call/self | `self.banner.setFixedHeight` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:64` |
| op-00b9be219d20870d | call/self | `self.banner.setMaximumHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:65` |
| op-ad7bf409c7be9f23 | call/self | `self.banner.setPixmap` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:68` |
| op-2239aea5beda8436 | call/self | `self.banner.setScaledContents` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:69` |
| op-340fd6d7b395c2ba | call/self | `self.banner.setVisible` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:70` |
| op-1d81cc46aad372f1 | call/self | `self.banner.size` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:67` |
| op-4da7eb57dedfdf41 | call/self | `self.bar_ref.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:764` |
| op-c1bd9c05e60d93ee | call/self | `self.base_path.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:165` |
| op-abb94dc8fbfa708a | call/self | `self.basicGroup.addSettingCards` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:93` |
| op-d8cae6fb35dd53ca | call/self | `self.basicGroup.deleteLater` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:240` |
| op-ee8c8df0035660a0 | call/self | `self.basicGroup.titleLabel.deleteLater` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:245` |
| op-fdbf7333ca173b90 | call/self | `self.basicGroup.vBoxLayout.insertSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:244` |
| op-1f872caa0958ef45 | call/self | `self.blockSignals` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:77` |
| op-f8c9ebfa8138bb80 | call/self | `self.bottomLayout.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:110` |
| op-1efbb258a9b31341 | call/self | `self.boxes.append` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:47` |
| op-5b3c8ce753344fc6 | call/self | `self.build_preauth_channel` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:292` |
| op-585901f1d4bcca22 | call/self | `self.button_clicked_signal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:249` |
| op-044ea53d526e5b23 | call/self | `self.button_clicked_signal.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:246` |
| op-733c4cb381e8bd0f | call/self | `self.button_signal.emit` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:211` |
| op-dccfd96ca2cea813 | call/self | `self.call_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:283` |
| op-20544b2b73a5381c | call/self | `self.calls.append` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:14` |
| op-be51f3145a3ab55f | call/self | `self.cancelButton.setText` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:22` |
| op-0865ae32dbb37bdd | call/self | `self.cancel_shutdown` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:995` |
| op-13200d86f59c79cd | call/self | `self.card.expandButton.hide` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:284` |
| op-df8dc1e59857adeb | call/self | `self.card.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:285` |
| op-5b89b6280049939e | call/self | `self.cfg.save_value` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:792` |
| op-0c71d5df98436e82 | call/self | `self.cfs.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:237` |
| op-327b8de20bdacc44 | call/self | `self.check_and_update_user_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:34` |
| op-bcb42820d45f282a | call/self | `self.check_atx` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:1101` |
| op-4ba5eb67b2018be0 | call/self | `self.check_atx_agent_version` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/uiautomator2_client.py:227` |
| op-8c9710f565f605b3 | call/self | `self.check_box_for_lesson_levels.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:82` |
| op-6e76fa889b21c4bc | call/self | `self.check_boxes.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:145` |
| op-f3c1f1038795db99 | call/self | `self.check_config_validation` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:58` |
| op-54a72d3d46d8f2d9 | call/self | `self.check_event_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:37` |
| op-1afdcf250c6fffc6 | call/self | `self.check_item_order.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:676` |
| op-0fbc5baacc9fe29c | call/self | `self.check_mumu_keep_alive` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:51` |
| op-5a445241b5421825 | call/self | `self.check_package_exist` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:251` |
| op-deebc7a35c2a3a38 | call/self | `self.check_process_running` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:284` |
| op-4f07f6d7c55a2671 | call/self | `self.check_resolution` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:335` |
| op-697c064508d25b12 | call/self | `self.check_scrcpy_alive` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy_client.py:47` |
| op-5441590356b8d969 | call/self | `self.check_screen_ratio` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1030` |
| op-d963efe14d188a76 | call/self | `self.check_serial` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:46` |
| op-58223f6eb171d06c | call/self | `self.check_static_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:27` |
| op-1ed08ef83858edc3 | call/self | `self.check_stderr` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:117` |
| op-8ce02ba6cd0aac15 | call/self | `self.check_stdout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:116` |
| op-dd05ac73213b37aa | call/self | `self.check_switch_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:38` |
| op-7fd2ab3936d0011c | call/self | `self.check_valid_student_names` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:132` |
| op-20998e920a401906 | call/self | `self.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:344` |
| op-f22ed104d7ac0fe1 | call/self | `self.clear_layout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:233` |
| op-0558806a6721bc83 | call/self | `self.clear_log` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:60` |
| op-d18f46adea15566d | call/self | `self.cli.click` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/scrcpy.py:10` |
| op-5015e5599beff814 | call/self | `self.cli.long_click` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/scrcpy.py:16` |
| op-7be7cec0dbdd2ba7 | call/self | `self.cli.screenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/screenshot/scrcpy.py:10` |
| op-ae1a2c6b93cd187e | call/self | `self.cli.swipe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/scrcpy.py:13` |
| op-8446093f5f2315e0 | call/self | `self.click` | 106 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/image.py:77` |
| op-64019819a085ecf6 | call/self | `self.click_thread` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:135` |
| op-7a802da1d2b9b1d2 | call/self | `self.clicked.connect` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `window.py:309` |
| op-c8a3a59fe51ff74c | call/self | `self.client.config.init_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:127` |
| op-580ebd437269802c | call/self | `self.client.config.save` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:126` |
| op-7158cb0d21910e11 | call/self | `self.client.create_shared_memory` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:140` |
| op-0072ed48e4f9f699 | call/self | `self.client.enable_thread_pool` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:131` |
| op-503827c030129800 | call/self | `self.client.init` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy_proxy.py:33` |
| op-149e94e2dcbc82e1 | call/self | `self.client.init_model` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:153` |
| op-1de76d2de4fb44c9 | call/self | `self.client.ocr` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:240` |
| op-ee8ab8a4d9355537 | call/self | `self.client.ocr_for_single_line` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:207` |
| op-9d52e9eebe87f036 | call/self | `self.client.proxy_websocket` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy_proxy.py:34` |
| op-8a638f7dcd21fa02 | call/self | `self.client.release_shared_memory` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:144` |
| op-607aea7f1e3d56f7 | call/self | `self.client.set_proxy_callbacks` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy_proxy.py:28` |
| op-563af601d895b312 | call/self | `self.client.start_server` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:99` |
| op-271c078b9db5525c | call/self | `self.client.stop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy_proxy.py:56` |
| op-67b839102a1629c4 | call/self | `self.column_2.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:107` |
| op-b329148129a320aa | call/self | `self.column_2.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:108` |
| op-915360ec597d669c | call/self | `self.column_2.setSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:91` |
| op-b6fc4d5a11eb94fc | call/self | `self.config.add_signal` | 2 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/fragments/home.py:280` |
| op-5860ac473ea4b379 | call/self | `self.config.create_item_holding_quantity.pop` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `core/config/config_set.py:130` |
| op-99033b40e2dcfb0f | call/self | `self.config.get` | 80 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/components/expand/arenaPriority.py:33` |
| op-52d75ad124172ef1 | call/self | `self.config.get_main_thread` | 5 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/components/expand/exploreConfig.py:48` |
| op-8cf3773309f627cc | call/self | `self.config.get_signal` | 4 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/components/expand/exploreConfig.py:47` |
| op-ee53ca2173a2631e | call/self | `self.config.get_window` | 3 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/components/expand/expandTemplate.py:87` |
| op-35349122195e4331 | call/self | `self.config.has` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/fragments/home.py:297` |
| op-ffd06e5b3a2efbf5 | call/self | `self.config.lesson_favorStudent.copy` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `module/lesson.py:484` |
| op-771ddad9e84dc933 | call/self | `self.config.save` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/fragments/switch.py:102` |
| op-3007a7ad427210bf | call/self | `self.config.set` | 63 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/components/expand/arenaPriority.py:82` |
| op-55aae72ba1f4df37 | call/self | `self.config.set_and_save` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/components/expand/baasUpdateConfig.py:371` |
| op-59d907035371c1a3 | call/self | `self.config.set_main_thread` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/fragments/home.py:116` |
| op-82bfc111899507ab | call/self | `self.config.set_signals` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/fragments/glob.py:78` |
| op-3838c3cc38372ae0 | call/self | `self.config.static_config.create_phase2_recommended_priority.keys` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/components/expand/createPriority.py:331` |
| op-4b4d03ebab8b3d10 | call/self | `self.config.unfinished_hard_tasks.append` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `core/Baas_thread.py:945` |
| op-d6f0e1836b6bfb25 | call/self | `self.config.unfinished_hard_tasks.pop` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `module/explore_tasks/sweep_task.py:76` |
| op-0491f1f9a9d256ac | call/self | `self.config.unfinished_normal_tasks.append` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `core/Baas_thread.py:932` |
| op-3794eb21ed1afb65 | call/self | `self.config.unfinished_normal_tasks.pop` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `module/explore_tasks/sweep_task.py:144` |
| op-00f2bb5cb888457e | call/self | `self.config.update` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/components/expand/expandTemplate.py:154` |
| op-0a0a0deacd19df91 | call/self | `self.config_buttons.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:154` |
| op-91dd085745246441 | call/self | `self.config_dir_list.append` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:413` |
| op-4dd4dac25fc7ec62 | call/self | `self.config_dir_list.remove` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:619` |
| op-0a2d6cdf092af0c5 | call/self | `self.config_file.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:175` |
| op-c8b52aad687d4a4f | call/self | `self.config_manager.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:48` |
| op-02b2771a6208c3d7 | call/self | `self.config_manager.set_loop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:74` |
| op-36c06d70576530c7 | call/self | `self.config_manager.watch_filesystem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:83` |
| op-f7ac6964b748863e | call/self | `self.config_obj.get` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:205` |
| op-987ea29305e07993 | call/self | `self.config_obj.save` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:201` |
| op-a751d1e8e94953e2 | call/self | `self.config_obj.set_and_save` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:223` |
| op-00744c4a512849f0 | call/self | `self.config_path.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:378` |
| op-eee28b25c9ec7ce1 | call/self | `self.config_root.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:26` |
| op-7d9d3fb28427859e | call/self | `self.config_set.get` | 6 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `core/Baas_thread.py:518` |
| op-39da0756192ebe04 | call/self | `self.config_set.get_signals` | 1 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `gui/fragments/glob.py:78` |
| op-bdce44a6e1b110d3 | call/self | `self.config_set.save` | 2 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `module/create.py:50` |
| op-008b629f95ebad34 | call/self | `self.config_set.set` | 17 | config.state | Runtime Configuration | `baas::script::host::ConfigHost` | PARITY-CONFIG-HOST | INVENTORIED | `core/Baas_thread.py:528` |
| op-17c81e8c8a0997a3 | call/self | `self.connect` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:244` |
| op-16cd3851b6018e05 | call/self | `self.connection.app_process_window.get_resolution` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1039` |
| op-5af11db3f638ab10 | call/self | `self.connection.click` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:42` |
| op-6006e93091034594 | call/self | `self.connection.close_current_app` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:569` |
| op-494ca4a9b3640a0d | call/self | `self.connection.get_activity_name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:329` |
| op-e6dbfd9e54009de8 | call/self | `self.connection.get_current_package` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:566` |
| op-c7739552d026df02 | call/self | `self.connection.get_package_name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:328` |
| op-dc55e33ecd98f600 | call/self | `self.connection.get_serial` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:327` |
| op-cc0fdb48d1481a5d | call/self | `self.connection.get_server` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:323` |
| op-3cff40bc32c852e6 | call/self | `self.connection.is_android_device` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:322` |
| op-090835590b1d23da | call/self | `self.connection.screenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:48` |
| op-49b0f98bc114935d | call/self | `self.connection.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy_client.py:24` |
| op-2f51ee419fc2eda4 | call/self | `self.connection.swipe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:45` |
| op-4371282e728a38ed | call/self | `self.connection_lost` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:25` |
| op-c17af30010e05494 | call/self | `self.content_label.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:77` |
| op-6e6f43f431c885c6 | call/self | `self.content_label.setStyleSheet` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:114` |
| op-ba2e474219a8f5a2 | call/self | `self.content_label.setWordWrap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:78` |
| op-8f01eecd84c91e07 | call/self | `self.content_label.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:128` |
| op-27eccbf5d6416571 | call/self | `self.context.config_manager.apply_patch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:62` |
| op-189b9d4896c83b03 | call/self | `self.context.config_manager.get_config_list` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:90` |
| op-a36bf1d8c423660b | call/self | `self.context.config_manager.get_snapshot` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:51` |
| op-00af1aebfff10717 | call/self | `self.context.config_manager.get_static_snapshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:38` |
| op-86f2efce595f56c2 | call/self | `self.context.config_manager.subscribe_updates` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:20` |
| op-81cf869a4db6a834 | call/self | `self.context.config_manager.unsubscribe_updates` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/sync.py:33` |
| op-23d8c8bd9ec391a9 | call/self | `self.context.log_manager.get_history` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:21` |
| op-bc7ac7ba0da884c8 | call/self | `self.context.log_manager.get_scopes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:22` |
| op-7ef69b3ff124a9d6 | call/self | `self.context.log_manager.subscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:17` |
| op-2c1e9fcd385741be | call/self | `self.context.log_manager.unsubscribe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:55` |
| op-8a2ab1a9c84838ce | call/self | `self.context.logger.error` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/exception.py:66` |
| op-9e8ce1b8850bc95b | call/self | `self.context.runtime.current_status` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:24` |
| op-f7aac0c509cdf424 | call/self | `self.context.runtime.require_remote_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/remote.py:17` |
| op-a9bcb640f8280c8a | call/self | `self.context.runtime.subscribe_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:18` |
| op-7b87301077cfa5c5 | call/self | `self.context.runtime.test_all_sha_stream` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:34` |
| op-fb820cf5c4de6663 | call/self | `self.context.runtime.unsubscribe_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/provider.py:56` |
| op-83641eedaff96db6 | call/self | `self.context.runtime.update_to_latest_stream` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/channels/trigger.py:38` |
| op-5f555abd5bbd8a46 | call/self | `self.control.click` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:165` |
| op-e16e4b6bd9475419 | call/self | `self.control.scroll` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `module/cafe_reward.py:131` |
| op-00fca6f8f3bbd84e | call/self | `self.control.swipe` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:733` |
| op-76a32431ea5a41cc | call/self | `self.controlCombo.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:46` |
| op-baee87c6cdc958a0 | call/self | `self.controlCombo.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:53` |
| op-525636197edfa3f1 | call/self | `self.controlCombo.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:47` |
| op-0a7dbb1602e3feeb | call/self | `self.control_instance.click` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Control.py:45` |
| op-41c7a0209996e7c1 | call/self | `self.control_instance.long_click` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Control.py:51` |
| op-29243f7e7f16dcb1 | call/self | `self.control_instance.scroll` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Control.py:54` |
| op-c301a8a51806e265 | call/self | `self.control_instance.swipe` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Control.py:48` |
| op-58b7c1c5bb3fdc22 | call/self | `self.control_socket.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:228` |
| op-13de22dc463217f3 | call/self | `self.convert_dict.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:677` |
| op-5daf569f882a8563 | call/self | `self.convert_dict.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:610` |
| op-0634f5aaa1b49cd7 | call/self | `self.create_cafe_mode_sel` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:47` |
| op-eddb4b677c283381 | call/self | `self.create_split_selection` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:38` |
| op-2865a51b9fb885dd | call/self | `self.create_table` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:653` |
| op-823f9e253101fc18 | call/self | `self.currentIndex` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:278` |
| op-22056894b538ab00 | call/self | `self.current_status` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:462` |
| op-71e321e293b2ea9a | call/self | `self.daily_config_refresh` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:458` |
| op-c34e7c87f356dfa3 | call/self | `self.data.get` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/mirrorc_update/mirrorc_updater.py:37` |
| op-1b0b68a8ab41942d | call/self | `self.deal_with_func_call_timeout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:540` |
| op-16dfb769ee625a30 | call/self | `self.deal_with_package_incorrect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:546` |
| op-945faa4c3b68af8a | call/self | `self.decode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:25` |
| op-a07608a38acb2a5a | call/self | `self.delete_all_view` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:221` |
| op-e8cbb16fc8370e77 | call/self | `self.delete_deprecated_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:33` |
| op-22e28187d4a0d68c | call/self | `self.derive_business_stream_keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:293` |
| op-be2be9cda3053744 | call/self | `self.detect_app_window` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:54` |
| op-84ebd439c124aacf | call/self | `self.detect_device` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:47` |
| op-bdbc4c266074861c | call/self | `self.detect_package` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:50` |
| op-e996ff1c7c6035bd | call/self | `self.device.create_connection` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:117` |
| op-a7ca239db70ea7fb | call/self | `self.device.forward` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:369` |
| op-5d74f89813abe0db | call/self | `self.device.forward_list` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:351` |
| op-7c51797782a3cd95 | call/self | `self.device.shell` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:184` |
| op-697c8ff8292991dd | call/self | `self.device.sync.push` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:159` |
| op-e6858f2ba0bf11ab | call/self | `self.dict` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/types.py:15` |
| op-ad298465a0d6ebec | call/self | `self.disconnect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:243` |
| op-712d2ca35b715851 | call/self | `self.disp_config.get` | 38 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:428` |
| op-2fbaa575e7805fda | call/self | `self.disp_config.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:395` |
| op-7e0dde28d987b8f3 | call/self | `self.dispatchSubView` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:542` |
| op-f5a68b67358d37a1 | call/self | `self.display` | 20 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:335` |
| op-a0dcd3ea6849ea6b | call/self | `self.displayWidget.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:24` |
| op-fa7e5162b6b8c349 | call/self | `self.displayWidget.setLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:83` |
| op-6a53b869e2c32e6e | call/self | `self.display_require_update_message` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:60` |
| op-cefd7e4fb7004117 | call/self | `self.display_update_config` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:63` |
| op-c25dcf705e2b691e | call/self | `self.document` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:26` |
| op-c3dd31964a2df5c9 | call/self | `self.dynamic_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:70` |
| op-092936e59592d25c | call/self | `self.dynamic_update_installer` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:958` |
| op-d274a7e782b0addb | call/self | `self.each_student_task_number_dict.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:43` |
| op-2d3da43875d19bf5 | call/self | `self.each_student_task_number_dict.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:39` |
| op-eb951269c040a4f8 | call/self | `self.emulatorMultiHLayout.addWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:92` |
| op-2340482bbc03f246 | call/self | `self.emulatorNotMultiAddressHLayout.addWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:65` |
| op-a92aa19aa580b58e | call/self | `self.enable_thread_pool` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:100` |
| op-c0f1a0e74c8c4446 | call/self | `self.encode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:58` |
| op-7320da951f92ff9f | call/self | `self.ensure_interval` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Screenshot.py:52` |
| op-ac2a9c3e62bdbc9c | call/self | `self.ensure_remote_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:492` |
| op-1030905d965ca626 | call/self | `self.error_log.write_text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:136` |
| op-55304fdb3f9b2c24 | call/self | `self.ev_run_async` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:282` |
| op-2e4364d1f520fa61 | call/self | `self.ev_run_sync` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:216` |
| op-98fc9fbbb04d9685 | call/self | `self.exist_item_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:644` |
| op-94d6372d638ceaaa | call/self | `self.exit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:350` |
| op-ed4e8480c94c3410 | call/self | `self.exitSignal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:279` |
| op-52288780ed7323e5 | call/self | `self.exit_baas` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:955` |
| op-8798f2f6f7a59452 | call/self | `self.exit_emulator` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:957` |
| op-13b5ef377d836ed4 | call/self | `self.exit_signal.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:983` |
| op-7b2558529a4972e3 | call/self | `self.expandLayout.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:273` |
| op-bd68effcdc65861d | call/self | `self.expandLayout.addWidget` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:100` |
| op-982cde6d3933b1b3 | call/self | `self.expandLayout.setSpacing` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:97` |
| op-6dd1ff7bd51f8f01 | call/self | `self.expand_view.show` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:281` |
| op-53ea78a858d0df83 | call/self | `self.exploreGroup.addSettingCards` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:200` |
| op-8e02dcfb832a8cfd | call/self | `self.explore_normal_task_str.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/test/test_explore_normal_task.py:33` |
| op-034df4f1e2785a9d | call/self | `self.extract_filename_and_extension` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:283` |
| op-d291b9f44217429e | call/self | `self.file_path.lower` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:238` |
| op-29ecb660befa1023 | call/self | `self.file_path.strip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:243` |
| op-c47c432c5206be46 | call/self | `self.finished.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:93` |
| op-1ceb1f07095b8033 | call/self | `self.flowLayout.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:254` |
| op-b2979c8b80579e8b | call/self | `self.flowLayout.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:252` |
| op-d44a25fe099b84f1 | call/self | `self.flowLayout.setSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:251` |
| op-43efccc7906dda42 | call/self | `self.font` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:28` |
| op-eaa5eaa33fa090b7 | call/self | `self.font_metrics.horizontalAdvance` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:60` |
| op-f945b2e8fb107713 | call/self | `self.format` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:34` |
| op-02344a9deace8ffe | call/self | `self.formatException` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:43` |
| op-310ac0d2927ffa9c | call/self | `self.formatStack` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/system_logging.py:45` |
| op-7856f04122814eee | call/self | `self.funcs.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:37` |
| op-2cf77a342a7302a6 | call/self | `self.genScheduleLog` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:461` |
| op-2fdacdb8da48d34e | call/self | `self.gen_event_formation_attr` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:36` |
| op-f53dc7467090b45b | call/self | `self.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:86` |
| op-05d57390c7d8b329 | call/self | `self.getPath` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:32` |
| op-a6e149c0746ae6a0 | call/self | `self.get_ap` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1118` |
| op-ee9d1040390e23e8 | call/self | `self.get_area_img` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:58` |
| op-c78c56f7b1250ee9 | call/self | `self.get_create_priority` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:292` |
| op-cbbc6ad796fdefc5 | call/self | `self.get_creditpoints` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1119` |
| op-26444368784325ec | call/self | `self.get_current_package` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:386` |
| op-e7f576f831eb6aa6 | call/self | `self.get_free_tcp_port` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:367` |
| op-a80a7431865b6a00 | call/self | `self.get_image_bytes` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:196` |
| op-96124a78e3c6032f | call/self | `self.get_next_tick` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:229` |
| op-25c9b87d1d7a7cf8 | call/self | `self.get_next_time` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:70` |
| op-0387b93d5158c418 | call/self | `self.get_ocr_language` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:336` |
| op-ef2f6773578d8361 | call/self | `self.get_phase2_recommended_name_list` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:268` |
| op-d8227ac64b4b0a39 | call/self | `self.get_phase2_recommended_priority` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:323` |
| op-f104e2fb57027f1c | call/self | `self.get_pyroxene` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1120` |
| op-f0d4b5578a1359e7 | call/self | `self.get_region_res` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:17` |
| op-4b4a56e3c2f9fc44 | call/self | `self.get_remote_sha_method_ComboBox.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:459` |
| op-2a423acfaa7e0dab | call/self | `self.get_remote_sha_method_ComboBox.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:465` |
| op-f9eb6e41a41995e4 | call/self | `self.get_remote_sha_method_ComboBox.setCurrentIndex` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:463` |
| op-7551d68d8b81ca26 | call/self | `self.get_remote_sha_method_display_names.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:449` |
| op-a21e0de0aa82aece | call/self | `self.get_remote_sha_method_origin_names.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:462` |
| op-515bc9c70ebc93f5 | call/self | `self.get_remote_sha_once` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:371` |
| op-f5fc63bb63613d8f | call/self | `self.get_request_data` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:192` |
| op-8827b84d483a89bd | call/self | `self.get_resolution` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:328` |
| op-835ccdb37a16391d | call/self | `self.get_scrcpy_jar_path` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:158` |
| op-732918c06e87467f | call/self | `self.get_screenshot_array` | 18 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:206` |
| op-362e3f7cc940bcc3 | call/self | `self.get_server_mode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:45` |
| op-4fac357fe06ca3f2 | call/self | `self.get_session` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:178` |
| op-19bf0d5b8e63d0f0 | call/self | `self.get_snapshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:307` |
| op-b99807ad7e866049 | call/self | `self.get_swipe_params` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy_client.py:75` |
| op-a128b751f8483555 | call/self | `self.get_unit_layout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:396` |
| op-43be609e3667687b | call/self | `self.git.clone` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:874` |
| op-a9540a1d83af39ee | call/self | `self.git.get_local_sha` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:791` |
| op-34eb6d327a287165 | call/self | `self.git.get_remote_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:804` |
| op-45633f6ba1473e75 | call/self | `self.git.git_dir.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:833` |
| op-084b77c32c4e4e27 | call/self | `self.git.is_valid_repo` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:789` |
| op-a04acb08aea68319 | call/self | `self.git.repair_repo` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:879` |
| op-0323b323d378f010 | call/self | `self.git.update` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:877` |
| op-0bdd847f11580e11 | call/self | `self.git_dir.exists` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:342` |
| op-c155f4ff58c6802d | call/self | `self.git_get_remote_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:395` |
| op-3755224e89d16f32 | call/self | `self.git_ops.ensure_remote_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:595` |
| op-0fc13b1a72a05c71 | call/self | `self.git_ops.fetch_and_reset` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:603` |
| op-62cf814b9082badc | call/self | `self.git_ops.get_local_head_sha` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:415` |
| op-ef8e041ff8d04db1 | call/self | `self.git_ops.get_remote_head_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:473` |
| op-13d6d967454453b9 | call/self | `self.git_ops.git_dir.exists` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:563` |
| op-70b16c9210bee94e | call/self | `self.git_ops.is_valid_repo` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:413` |
| op-0eefb5310b1e507a | call/self | `self.git_ops.repair_repo` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:592` |
| op-2678b0146a0e39bf | call/self | `self.github_api_get_latest_sha` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:393` |
| op-65c0a5ecedc24449 | call/self | `self.globalInterface.lazy_init` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:537` |
| op-a88d62f65e722f0b | call/self | `self.guiGroup.addSettingCards` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:201` |
| op-2b14bf0e00ab2a8e | call/self | `self.hBoxLayout.addLayout` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:75` |
| op-7f36832bd9cb0013 | call/self | `self.hBoxLayout.addSpacing` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:72` |
| op-61fe328f0b3e9b2d | call/self | `self.hBoxLayout.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/totalForceFightPriority.py:30` |
| op-a5e460c4692f7653 | call/self | `self.hBoxLayout.addWidget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/totalForceFightPriority.py:26` |
| op-e8529965b7a194f3 | call/self | `self.hBoxLayout.insertLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:343` |
| op-147d82b16f1760f1 | call/self | `self.hBoxLayout.insertWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:366` |
| op-aa680b1e1513c914 | call/self | `self.hBoxLayout.setAlignment` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:73` |
| op-7bd8e27ead9d23a0 | call/self | `self.hBoxLayout.setContentsMargins` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:40` |
| op-f1ef57d05469bc55 | call/self | `self.hBoxLayout.setStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:367` |
| op-62660a022a2078a0 | call/self | `self.hBoxLayout.setStretchFactor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:81` |
| op-54eeb2b18798dc16 | call/self | `self.handleError` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/__init__.py:37` |
| op-0c73461208f4cc9a | call/self | `self.handle_then` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:502` |
| op-b0872a2e0110e2d2 | call/self | `self.hard_task_combobox.addItem` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:44` |
| op-aeb3be5ff4451743 | call/self | `self.hard_task_combobox.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:45` |
| op-6f36c91a5178a131 | call/self | `self.hard_task_combobox.currentText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:131` |
| op-f029ab10e42d05b9 | call/self | `self.height` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:182` |
| op-2347ee6b80825959 | call/self | `self.helpButton.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:361` |
| op-287e8dbb325aee5f | call/self | `self.helpButton.setIcon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:377` |
| op-edcd6df1e2ad52ed | call/self | `self.helpButton.setToolTip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:360` |
| op-22a34d57f7998913 | call/self | `self.historyButton.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:357` |
| op-2c02e2d3b7a71935 | call/self | `self.historyButton.setIcon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:375` |
| op-88827a8baa1a90f0 | call/self | `self.historyButton.setToolTip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:356` |
| op-5916d5dcfad7a039 | call/self | `self.hk_callbacks.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:153` |
| op-8ea3263ea7664512 | call/self | `self.hk_mgr.register` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:300` |
| op-4af6ef1ad39574df | call/self | `self.hk_mgr.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:301` |
| op-20fd3046ee8971d0 | call/self | `self.horizontalHeader` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:640` |
| op-b98a1ed50dcfe6fc | call/self | `self.hotkey_push_button.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:155` |
| op-c345799fe94e093a | call/self | `self.hotkey_push_button.font` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:136` |
| op-34acc8ce487b9d45 | call/self | `self.hotkey_push_button.setFont` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:138` |
| op-d779ed5af0960ff5 | call/self | `self.hotkey_triggered.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:109` |
| op-19f67158ac591831 | call/self | `self.image_label.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:58` |
| op-a80b749b73bfc1df | call/self | `self.image_label.setFixedSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:57` |
| op-68178ad65da75053 | call/self | `self.image_label.setPixmap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:55` |
| op-854b14575787e38e | call/self | `self.incoming.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:102` |
| op-88b89cbaf69e9725 | call/self | `self.info.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:233` |
| op-c6c7be69c22faed7 | call/self | `self.infoLayout.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:60` |
| op-44f076775ed7e3e7 | call/self | `self.infoLayout.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:59` |
| op-c1443e6e79567d79 | call/self | `self.info_box.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:50` |
| op-f6e334b98b15e73c | call/self | `self.initNavigation` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:443` |
| op-860e585bcd39f71d | call/self | `self.initWindow` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:395` |
| op-07e063640ea9913e | call/self | `self.init_all_data` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:17` |
| op-6c9431aa72982d68 | call/self | `self.init_control_instance` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Control.py:19` |
| op-f917179b36de63a4 | call/self | `self.init_model` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:101` |
| op-cf3a2f09ea69b4db | call/self | `self.init_ocr` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:21` |
| op-9b93a8fbcb3963db | call/self | `self.init_screenshot_instance` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Screenshot.py:23` |
| op-ce6fe4b6c6637bd5 | call/self | `self.init_static_config` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:24` |
| op-a1920d7b32dec106 | call/self | `self.init_url` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:24` |
| op-98fb8656f5713843 | call/self | `self.inject_comp_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:119` |
| op-34cb6bd91c966602 | call/self | `self.inject_config_list.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:118` |
| op-2b880bd6da53868f | call/self | `self.input.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/totalForceFightPriority.py:17` |
| op-1ace5d87a4f6758d | call/self | `self.input.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/totalForceFightPriority.py:21` |
| op-934a008b65ddaacd | call/self | `self.input.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:54` |
| op-e54eb49c59011222 | call/self | `self.input.setText` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:25` |
| op-eeb94beff18526a3 | call/self | `self.input.setValidator` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:24` |
| op-ae78bc0276222fcc | call/self | `self.input.text` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:59` |
| op-4314200cbd4f7edf | call/self | `self.inputComponent.blockSignals` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:186` |
| op-1f3931bbb92c7dc0 | call/self | `self.inputComponent.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:187` |
| op-398c918675916ca7 | call/self | `self.inputComponent.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:182` |
| op-8d62b971d5ba1f01 | call/self | `self.inputPatRound.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:73` |
| op-64df568128f47795 | call/self | `self.inputPatRound.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:74` |
| op-5d62cae1f1c47b26 | call/self | `self.inputPatRound.textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:177` |
| op-7961c15fd71899c3 | call/self | `self.inputPatStyle.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:82` |
| op-de734a7a54646cde | call/self | `self.inputPatStyle.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:178` |
| op-826a117316954913 | call/self | `self.inputPatStyle.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:83` |
| op-6bf577c5b47e3f8f | call/self | `self.input_0.isChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:81` |
| op-6769ebe296ea8a34 | call/self | `self.input_0.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:34` |
| op-e785870a97655479 | call/self | `self.input_0.stateChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:51` |
| op-1814ab287f509c96 | call/self | `self.input_1.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:38` |
| op-0d10ca563df9c5e3 | call/self | `self.input_1.setValidator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:39` |
| op-a8c83763ffbaf30f | call/self | `self.input_1.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:86` |
| op-1e59508ca0c290d9 | call/self | `self.input_2.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:44` |
| op-8780f3b07a562e7e | call/self | `self.input_2.setValidator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:45` |
| op-976be11dad564841 | call/self | `self.input_2.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:91` |
| op-9269f9d5bcab86bf | call/self | `self.input_3.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:48` |
| op-e36cbe804ab188be | call/self | `self.input_3.currentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:96` |
| op-c039beaf37b4804e | call/self | `self.input_3.setCurrentIndex` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:49` |
| op-53953a40648e8b9e | call/self | `self.input_field.get_hotkey_string` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:431` |
| op-f5d42567216c26e1 | call/self | `self.input_field.setFocus` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:468` |
| op-8947207ec5017ba5 | call/self | `self.input_field.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:412` |
| op-f9df61ea74afe291 | call/self | `self.input_for_create_priority.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:294` |
| op-9781614421e00a24 | call/self | `self.input_for_create_priority.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:296` |
| op-8f0b28749015f9a5 | call/self | `self.input_for_create_priority.textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:297` |
| op-f6d27bbf9655ec8f | call/self | `self.input_hard.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:55` |
| op-28f3694ca026eace | call/self | `self.input_hard.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:52` |
| op-b9f0b7692b0cccba | call/self | `self.input_hard.text` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:113` |
| op-983bf0afce517ca6 | call/self | `self.insertBAASTab` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:270` |
| op-011d28d18d5f0c06 | call/self | `self.insert_swipe_points` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy_client.py:82` |
| op-e3bcf2652db78a8d | call/self | `self.installEventFilter` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:785` |
| op-df859ab4a60a021e | call/self | `self.isBytes` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:24` |
| op-13f6c3f0d9403d4c | call/self | `self.isChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:23` |
| op-1eeeea1a66ac9ae7 | call/self | `self.isChinese` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:57` |
| op-3675ba41540cc2d8 | call/self | `self.isScrollable` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:283` |
| op-9f3db40ef829b25b | call/self | `self.isString` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:29` |
| op-e836a2f6781a334a | call/self | `self.isTabShadowEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:286` |
| op-bf55f8c4d9c91050 | call/self | `self.is_apk_outdated` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:241` |
| op-20a49ac4888d06d5 | call/self | `self.is_atx_agent_outdated` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:220` |
| op-cd82703fed277fbd | call/self | `self.is_capturing` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:103` |
| op-14ab45ce6d170599 | call/self | `self.is_chinese_char` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:53` |
| op-e6d5a8cf43afb234 | call/self | `self.is_disable_period` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:119` |
| op-f5bd24bf2de8038f | call/self | `self.is_float` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:721` |
| op-3fdb08bc2c24ac66 | call/self | `self.is_mumu12_family` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:151` |
| op-5d5fee917b9b3dab | call/self | `self.is_mumu_family` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:189` |
| op-ada400dae580ff9b | call/self | `self.is_port_free` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:106` |
| op-88ba7794c0b04099 | call/self | `self.is_valid_window` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:44` |
| op-045abfef03644f28 | call/self | `self.itemLayout.insertWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:294` |
| op-20cd9cb1bece1d5a | call/self | `self.item_is_Material` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:747` |
| op-5e8da9352cd387e1 | call/self | `self.item_level_in` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:740` |
| op-5375cb3d14dca508 | call/self | `self.item_level_is` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:761` |
| op-4c4ecb172833058f | call/self | `self.item_type_is` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:761` |
| op-d283fc86a3a01f3d | call/self | `self.items.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:291` |
| op-7a5077ff52a3d086 | call/self | `self.items.insert` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:295` |
| op-cd67dd644bdf8597 | call/self | `self.items_k.index` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:49` |
| op-99a7af488b0937b8 | call/self | `self.kill_adb` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:293` |
| op-79826319afae6edc | call/self | `self.kwargs.keys` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:242` |
| op-cd843d0e8e1ab85a | call/self | `self.label.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:190` |
| op-69a2ba3e4d93a325 | call/self | `self.label.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/shopPriority.py:25` |
| op-f356f6f865d10b9c | call/self | `self.label_running.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:63` |
| op-704ae1225c6acac8 | call/self | `self.labeled_switchBtn_template` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:55` |
| op-aef939c27f05b830 | call/self | `self.langButton.setIcon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:379` |
| op-a4f8a4ac782486c1 | call/self | `self.lay1.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:72` |
| op-4eba64c08de5095b | call/self | `self.lay1.addWidget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:65` |
| op-7fb6db7739f17229 | call/self | `self.lay1.setAlignment` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:73` |
| op-925e004d5d796bd1 | call/self | `self.lay1.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:57` |
| op-977a280fb7a3d658 | call/self | `self.lay1_hard.addStretch` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:75` |
| op-3af53767c4e6918e | call/self | `self.lay1_hard.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:68` |
| op-4d734e98eefd8158 | call/self | `self.lay1_hard.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:76` |
| op-8302adf623d9572e | call/self | `self.lay1_hard.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:59` |
| op-50315faa2d3c98e0 | call/self | `self.lay2.addWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:66` |
| op-2a9650200a505b52 | call/self | `self.lay2.setAlignment` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:74` |
| op-9899012517a050d7 | call/self | `self.lay2.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:58` |
| op-2950bc3b1d284750 | call/self | `self.lay2_hard.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:69` |
| op-743b84bf700a59a8 | call/self | `self.lay2_hard.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:79` |
| op-25815ac0f1012324 | call/self | `self.lay2_hard.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/mainlinePriority.py:60` |
| op-fe7b7ae3cb16af22 | call/self | `self.lay3.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:76` |
| op-08d836b201cbd3fe | call/self | `self.lay3.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:87` |
| op-0c88a116450ee42e | call/self | `self.lay4.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:80` |
| op-0ec0af862c373c8b | call/self | `self.lay4.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:88` |
| op-9f33580941c93c32 | call/self | `self.lay5.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:84` |
| op-187e687a7117b705 | call/self | `self.lay5.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:89` |
| op-0071ccad378b875f | call/self | `self.layPatRound.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:75` |
| op-4b5be24cef737366 | call/self | `self.layPatStyle.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:84` |
| op-99e1ea9c72375b3a | call/self | `self.laySecondCafe.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:65` |
| op-5d8987a75b704edd | call/self | `self.lay_0.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:56` |
| op-922367ada9774be7 | call/self | `self.lay_1.addWidget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:59` |
| op-7c4f3c0731c32aee | call/self | `self.lay_2.addWidget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:63` |
| op-430270adc3f581c2 | call/self | `self.lay_3.addWidget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:67` |
| op-06bd5ab53cd8773d | call/self | `self.layout._mirrorc_inst.get_latest_version` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:92` |
| op-3b20049cfad11a37 | call/self | `self.layout.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:396` |
| op-36ec411b77fbbb82 | call/self | `self.layout.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:261` |
| op-767f54f7cd2c2267 | call/self | `self.layout.setSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:260` |
| op-7aaa36c3d4e5736e | call/self | `self.layout_for_line_two.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:164` |
| op-773f17e4f400cb24 | call/self | `self.layout_for_line_two.removeWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:201` |
| op-c9abe0d19023481e | call/self | `self.lesson_enableFavorStudent_check_box.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:65` |
| op-a39bcfe8dfa9ed3c | call/self | `self.lesson_enableFavorStudent_check_box.stateChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:135` |
| op-589f555a9a4afa71 | call/self | `self.lesson_favorStudent_LineEdit.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:69` |
| op-bbb5a257b7fbb9f5 | call/self | `self.lesson_favorStudent_LineEdit.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:108` |
| op-de14bdea4880dc2b | call/self | `self.lesson_time_input.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:88` |
| op-3dbb66d3c6316701 | call/self | `self.listWidget.addItems` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:111` |
| op-e71f31b9de447092 | call/self | `self.listWidget.clear` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:110` |
| op-63159ac43a929992 | call/self | `self.listWidget.setObjectName` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:135` |
| op-4e9e64110bb06e2d | call/self | `self.list_config_ids` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:92` |
| op-46ef7ee354d8f3e0 | call/self | `self.list_devices` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:114` |
| op-a57836bd307b2b5d | call/self | `self.list_packages` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:258` |
| op-12cab8134d5a57ca | call/self | `self.listeners.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:588` |
| op-321167dc06a05c9e | call/self | `self.load` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:12` |
| op-5fb99c95f651164f | call/self | `self.locale.name` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:11` |
| op-1fc7ec57bdf48939 | call/self | `self.log_collector.put` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/injection.py:110` |
| op-79cd34bc191548bf | call/self | `self.log_manager.register_queue` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:80` |
| op-4d57a542fd2acda8 | call/self | `self.log_manager.set_loop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:76` |
| op-7e28784aa60f43c8 | call/self | `self.log_manager.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:81` |
| op-037b8c0ef67c8f50 | call/self | `self.log_manager.stop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:111` |
| op-0ba306bd95dfdc2d | call/self | `self.logger.addHandler` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/utils.py:64` |
| op-b0770162d6870662 | call/self | `self.logger.critical` | 2 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/Baas_thread.py:877` |
| op-5fa753a9aaafb1f4 | call/self | `self.logger.error` | 98 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/Baas_thread.py:317` |
| op-65c2b1a1aa91b1cf | call/self | `self.logger.info` | 523 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/Baas_thread.py:152` |
| op-1df46d2e6603f09e | call/self | `self.logger.setLevel` | 1 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/utils.py:63` |
| op-4e63d8f84a6c09a1 | call/self | `self.logger.warning` | 146 | log.event | Runtime Observability | `baas::script::host::LogHost` | PARITY-LOG-EVENT | INVENTORIED | `core/Baas_thread.py:183` |
| op-6ef6de2631f952cf | call/self | `self.logger_box.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:192` |
| op-24e5b55b0547468e | call/self | `self.logger_box.setReadOnly` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:96` |
| op-515bb49ae6461475 | call/self | `self.logger_signal.emit` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/utils.py:80` |
| op-a7130d7dd4d10375 | call/self | `self.mainLayout.addLayout` | 10 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:89` |
| op-ee599a1addf1f76d | call/self | `self.mainLayout.addSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:88` |
| op-8c283393a4158aef | call/self | `self.mainLayout.count` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:225` |
| op-298884dc9a0eac84 | call/self | `self.mainLayout.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:95` |
| op-3349e446fa1f46aa | call/self | `self.mainLayout.takeAt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:226` |
| op-51518e9be95892a5 | call/self | `self.main_page_update_data` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:497` |
| op-40332612a0e981b5 | call/self | `self.main_thread_attach.button_signal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:117` |
| op-d416c0ceb3d301de | call/self | `self.main_thread_attach.exit_signal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:120` |
| op-6596cc1d50b37ed9 | call/self | `self.main_thread_attach.logger_signal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:118` |
| op-8f5791a35b53f69d | call/self | `self.main_thread_attach.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:287` |
| op-923fecae7dbc2d54 | call/self | `self.main_thread_attach.stop_play` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:285` |
| op-3c7b4b490b83ca38 | call/self | `self.main_thread_attach.update_signal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:119` |
| op-c78253e233e3286d | call/self | `self.manager.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:442` |
| op-5ed1f64a1b43ea3a | call/self | `self.mapToGlobal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:313` |
| op-f12d878b3e81994a | call/self | `self.menu.addAction` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:322` |
| op-e7051d7e990c0522 | call/self | `self.menu.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:329` |
| op-bdd50b8ff7bc05a2 | call/self | `self.menu.deleteLater` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:330` |
| op-c8770c21b09a1131 | call/self | `self.menu.exec` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:314` |
| op-b09287e87c4e51fc | call/self | `self.messages.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_service_injection.py:46` |
| op-11349254fc84fa73 | call/self | `self.micaCard.checkedChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:229` |
| op-e5b5e322258ff1d5 | call/self | `self.micaCard.setEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:118` |
| op-5683a2ea3221dbe8 | call/self | `self.mirrorc.get_latest_version` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:812` |
| op-5c12af23f5da9fc2 | call/self | `self.mirrorc.set_version` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:763` |
| op-293382e42c77010e | call/self | `self.mirrorc_api_get_latest_sha` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:397` |
| op-4776f019ff550366 | call/self | `self.move` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:506` |
| op-923045e99dc39518 | call/self | `self.multiMap.keys` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:86` |
| op-948a38e67df1e1b5 | call/self | `self.name_dict.items` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:24` |
| op-7f37ae440886be61 | call/self | `self.name_dict.keys` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:107` |
| op-a0754e60b5f8ec2d | call/self | `self.nemu_client.down` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/nemu.py:115` |
| op-d717dc24825259b1 | call/self | `self.nemu_client.screenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/screenshot/nemu.py:46` |
| op-bc93ee9f327128dd | call/self | `self.nemu_client.up` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/nemu.py:117` |
| op-40492913a228a4e0 | call/self | `self.normalize_screenshot` | 4 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:171` |
| op-fcd4f6dbba07f0d0 | call/self | `self.not_exist_item_list.extend` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:650` |
| op-d330bfcfa4e9a595 | call/self | `self.notify_signal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:414` |
| op-150547f0ee38e3ec | call/self | `self.now_exist_item_info.clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/create.py:679` |
| op-114c4d9d634b7e71 | call/self | `self.ocr` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:74` |
| op-a634392dd89441ce | call/self | `self.ocr.client.start_server` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `main.py:38` |
| op-65682e8c536f5e31 | call/self | `self.ocr.create_shared_memory` | 2 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/Baas_thread.py:1034` |
| op-57c419ba61d8f2f9 | call/self | `self.ocr.get_region_pure_chinese` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `module/mini_story.py:50` |
| op-73adbc73e1740549 | call/self | `self.ocr.get_region_pure_english` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `module/mini_story.py:44` |
| op-888b70e94a7ef217 | call/self | `self.ocr.get_region_raw_res` | 2 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `module/total_assault.py:320` |
| op-d929f4e041966472 | call/self | `self.ocr.get_region_res` | 21 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/Baas_thread.py:760` |
| op-472531099502def6 | call/self | `self.ocr.init_baas_model` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/Baas_thread.py:341` |
| op-6e03d3e07f059822 | call/self | `self.ocr.recognize_int` | 8 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `module/activities/activity_utils.py:526` |
| op-8d5e338000e50c7a | call/self | `self.ocr.recognize_number` | 2 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `module/activities/bunnyChaserOnTheShip.py:374` |
| op-2ee7ebc45f7f050e | call/self | `self.ocr.release_shared_memory` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/Baas_thread.py:1062` |
| op-ac0e08abeab126f2 | call/self | `self.ocr.test_models` | 1 | ocr.inference | Runtime OCR | `baas::script::host::OcrHost` | PARITY-OCR-HOST | INVENTORIED | `core/Baas_thread.py:342` |
| op-4e5fa0be040c8293 | call/self | `self.ocr_for_single_line` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:59` |
| op-e6f5c0c99df12c0a | call/self | `self.ocr_needed.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:421` |
| op-1bf2ef8050baac44 | call/self | `self.onToggleChangeSignal.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:265` |
| op-154ad07fbceda59c | call/self | `self.on_adb_to_ws` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:513` |
| op-868558c82f8972f3 | call/self | `self.on_status.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:67` |
| op-f71f6f19c08e7397 | call/self | `self.on_status.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:66` |
| op-2a5c6ba9436391df | call/self | `self.on_status.setObjectName` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:134` |
| op-c1c68aec53f59f4e | call/self | `self.on_status.setText` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:108` |
| op-e6ec928b9b2182e1 | call/self | `self.on_ws_to_adb` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy.py:489` |
| op-8a0aad6eb4592cb5 | call/self | `self.op_2.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:94` |
| op-9f8050eedca07148 | call/self | `self.op_3.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:99` |
| op-a3c14d824bb051e3 | call/self | `self.op_3.currentIndex` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:184` |
| op-83c14f8d3e612ecd | call/self | `self.op_3.currentIndexChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:101` |
| op-f32ee71a7280501d | call/self | `self.operate_dict` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:68` |
| op-f600f8b3986b0e4f | call/self | `self.operate_item` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:81` |
| op-e5aa23a3c2f29db2 | call/self | `self.option_layout.addStretch` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:90` |
| op-33a3efef52e07614 | call/self | `self.option_layout.addWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:89` |
| op-86e7c7c3eb3b2765 | call/self | `self.original_text.split` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:52` |
| op-b0a847aad89d596d | call/self | `self.outgoing.put` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/pipe_endpoint.py:118` |
| op-850c2f49ef42b4d4 | call/self | `self.p.sort` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/geometry/triangle.py:9` |
| op-ae5bc0a2cd162824 | call/self | `self.package2server` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:270` |
| op-d838b7dcc4c77e17 | call/self | `self.palette` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:165` |
| op-e09a6cd039a07a87 | call/self | `self.parent` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:14` |
| op-0c7173135b73657a | call/self | `self.parseToDisplay` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:246` |
| op-1d9cd14b95d30632 | call/self | `self.patch_signal.connect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:105` |
| op-d78d0677448b5f9c | call/self | `self.patch_signal.emit` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:100` |
| op-3024ba1d25b4b9fb | call/self | `self.pathLineEdit.setClearButtonEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:14` |
| op-eec01498e44cea92 | call/self | `self.pathLineEdit.setPlaceholderText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:13` |
| op-ef2ea094d593c70b | call/self | `self.pathLineEdit.textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:26` |
| op-665680e20deeb174 | call/self | `self.processWidget.setLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:89` |
| op-d7ea43d1be472dc0 | call/self | `self.publish` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/utils/broadcast.py:48` |
| op-3286c8f76a022df3 | call/self | `self.publish_version_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:668` |
| op-fb6c2940411a65f8 | call/self | `self.push_and_log_error_msg` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:541` |
| op-aa793abb61247c59 | call/self | `self.push_url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:222` |
| op-1d2229edb1fe1f98 | call/self | `self.pygit2_get_latest_sha` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:40` |
| op-394ea8d34ff10704 | call/self | `self.qLabels.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:143` |
| op-6d3738a73a4ef926 | call/self | `self.received_json.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:66` |
| op-909ba87ed8395743 | call/self | `self.rect` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:173` |
| op-0a13e22d195b18b3 | call/self | `self.recv_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/websocket_endpoint.py:21` |
| op-a072f46770bc51d9 | call/self | `self.recvall` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:69` |
| op-646e70eeef2cc8f6 | call/self | `self.refresh_common_tasks` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:915` |
| op-0dd23d24ebf2a8f0 | call/self | `self.refresh_create_time` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:914` |
| op-3a50484b120cb797 | call/self | `self.refresh_hard_tasks` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:916` |
| op-aac5140e9fd41d3f | call/self | `self.relationship_check_box.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:74` |
| op-6ef74e4e39a0ee50 | call/self | `self.relationship_check_box.stateChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:137` |
| op-ea8245400c1309aa | call/self | `self.repaint_all_children_labels` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:246` |
| op-62c241847b2837c9 | call/self | `self.repaint_color` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:314` |
| op-753dfdd0bbc070f8 | call/self | `self.repaint_labels` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:52` |
| op-1de37451976d62a4 | call/self | `self.repaint_page` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:56` |
| op-002dd8d7354a62a2 | call/self | `self.repo_path.exists` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/checks.py:85` |
| op-32650babbdbe082f | call/self | `self.repo_path.iterdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:233` |
| op-d9da0cbada85681b | call/self | `self.repo_path.mkdir` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:462` |
| op-350f9c8368b09521 | call/self | `self.reset_table` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:613` |
| op-5b398d36853c0ad9 | call/self | `self.reset_view` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:204` |
| op-2c2d74109b75fa73 | call/self | `self.resize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:501` |
| op-7c645efa204bc734 | call/self | `self.resolution_uiautomator2` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1103` |
| op-240bd56d163ba136 | call/self | `self.revert_dict.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:703` |
| op-0153dbcee2f228b4 | call/self | `self.revise_serial` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:72` |
| op-513dccfb37fff408 | call/self | `self.rewarded_task_status.__str__` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/rewarded_task.py:37` |
| op-0f7347a60e8fd089 | call/self | `self.rewrap_text` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:35` |
| op-8bc1a228994f45a8 | call/self | `self.run_app` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/installer.py:1058` |
| op-ac9b717c8fab78b3 | call/self | `self.runtime.check_for_update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:122` |
| op-930d1b37cdd771e6 | call/self | `self.runtime.ensure_ready` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:78` |
| op-fe208c624b56ef6a | call/self | `self.runtime.get_log_sources` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:115` |
| op-fa7053947118a799 | call/self | `self.runtime.get_main_log_queue` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:79` |
| op-7e8c66783a87afd9 | call/self | `self.runtime.set_loop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:75` |
| op-b5d0b2a5d12c2de8 | call/self | `self.runtime.stop_all_tasks` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/context.py:110` |
| op-ffc656d4e747a23d | call/self | `self.save` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:69` |
| op-b1ef9055839d77f3 | call/self | `self.save_config` | 6 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:460` |
| op-ca25fe59cbc9ee1e | call/self | `self.scan_once` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/manager.py:488` |
| op-a170f6f383c10e01 | call/self | `self.scheduler.get_interval` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `module/cafe_reward.py:45` |
| op-4e2e9877228b0faf | call/self | `self.scheduler.heartbeat` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/Baas_thread.py:455` |
| op-893f730738edeb41 | call/self | `self.scheduler.is_wait_long` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/Baas_thread.py:952` |
| op-16375a25d37bc1ec | call/self | `self.scheduler.systole` | 2 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/Baas_thread.py:487` |
| op-377325d503923dc3 | call/self | `self.scheduler.update_valid_task_queue` | 1 | task.scheduler-thread | Runtime Scheduling | `baas::script::host::SchedulerHost` | PARITY-SCHEDULER-HOST | INVENTORIED | `core/Baas_thread.py:499` |
| op-f3cac601ffcf21cd | call/self | `self.schedulerInterface.update_settings` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:470` |
| op-e65717076b455a65 | call/self | `self.scheduler_selector.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:42` |
| op-2d0f5ff1602ea0b3 | call/self | `self.scheduler_selector.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:48` |
| op-d10b91a7476905ca | call/self | `self.scheduler_selector.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:47` |
| op-52a54b07484db530 | call/self | `self.screenshot.screenshot` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:176` |
| op-3640deab01535a8c | call/self | `self.screenshot.set_screenshot_interval` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:885` |
| op-e536419d79c81aab | call/self | `self.screenshotCombo.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:43` |
| op-a86db76901fd42d0 | call/self | `self.screenshotCombo.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:52` |
| op-652c32508cfeef2b | call/self | `self.screenshotCombo.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:44` |
| op-34ef6c8a3fd0d058 | call/self | `self.screenshot_box.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:33` |
| op-9be4560f3ba441ea | call/self | `self.screenshot_box.setValidator` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:32` |
| op-ea5cd9b75e380f92 | call/self | `self.screenshot_box.textChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:49` |
| op-1d107d14f363c923 | call/self | `self.screenshot_instance.screenshot` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/Screenshot.py:53` |
| op-334b511c44290666 | call/self | `self.second_switch.checkedChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:179` |
| op-2f4c2c3f6440a084 | call/self | `self.second_switch.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:64` |
| op-2ba3b704ecbca7a7 | call/self | `self.sender` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:99` |
| op-ef73aad91fe6181b | call/self | `self.sent.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:83` |
| op-b66aa1229fbed97f | call/self | `self.sent_json.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_security_contract.py:63` |
| op-87195d86163a6202 | call/self | `self.serial_port` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:174` |
| op-81cb67838bdf6283 | call/self | `self.server_process.stdin.close` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:150` |
| op-7b532fb2c05f1b43 | call/self | `self.server_process.stdin.flush` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:146` |
| op-6d9ba127a3da08f4 | call/self | `self.server_process.stdin.write` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:145` |
| op-d9719f1f71e4ef6d | call/self | `self.server_process.wait` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/baas_ocr_client/Client.py:147` |
| op-93e5a77ca73b0e2c | call/self | `self.server_public_key_b64` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/auth/manager.py:107` |
| op-e7a93f373afb0e59 | call/self | `self.set` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:73` |
| op-c1052dd8acd29236 | call/self | `self.setAlignment` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:152` |
| op-fbf95601f8401578 | call/self | `self.setCellWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:690` |
| op-7a5d4c7950128c91 | call/self | `self.setCheckable` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:16` |
| op-bd7e597274858c91 | call/self | `self.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:17` |
| op-a79a561eeeee0e42 | call/self | `self.setColumnCount` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:637` |
| op-1c5d0b06624e40d8 | call/self | `self.setCurrentIndex` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:180` |
| op-4695b116fe7f8d19 | call/self | `self.setCursor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:39` |
| op-62920100df8dabec | call/self | `self.setExpand` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:283` |
| op-303d8c4e3469f74c | call/self | `self.setFixedHeight` | 9 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaPriority.py:13` |
| op-bceb55e699c56990 | call/self | `self.setFixedSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:18` |
| op-16caac112bb53edf | call/self | `self.setFixedWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:37` |
| op-947f4a631dfbf45e | call/self | `self.setFocus` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:789` |
| op-eafb40eb767fc6bf | call/self | `self.setFocusPolicy` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:784` |
| op-c2582d22bff67484 | call/self | `self.setGraphicsEffect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:138` |
| op-ba5b3313edab00a5 | call/self | `self.setHorizontalHeaderLabels` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:649` |
| op-a8ab7cbf88f9cb03 | call/self | `self.setHorizontalScrollBarPolicy` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:27` |
| op-c88941c706f7446d | call/self | `self.setLayout` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:305` |
| op-aed75d03a297a412 | call/self | `self.setLineWrapMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:25` |
| op-47c6dc8d9dd0b3f2 | call/self | `self.setMinimumHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:503` |
| op-d7831e5f592438ea | call/self | `self.setMinimumWidth` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:224` |
| op-b3fe1f649ccdc04e | call/self | `self.setModal` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:406` |
| op-d8feda9e3d23b6f4 | call/self | `self.setObjectName` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:27` |
| op-ca6cd271f22e995f | call/self | `self.setPalette` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:177` |
| op-e9c0a5f5467afb26 | call/self | `self.setPixmap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:835` |
| op-d565bda298140725 | call/self | `self.setPlaceholderText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:335` |
| op-f93545e89ca13488 | call/self | `self.setPlainText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:82` |
| op-c6fa3438f0d17a01 | call/self | `self.setReadOnly` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:333` |
| op-d3798c91c5b9dd82 | call/self | `self.setRowCount` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:665` |
| op-71bbe529f5f5400d | call/self | `self.setScaledContents` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:813` |
| op-49d3341d36e7debf | call/self | `self.setStyleSheet` | 16 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/arenaShopPriority.py:21` |
| op-04d69917eb9d949d | call/self | `self.setText` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:15` |
| op-5672efd484333869 | call/self | `self.setTextCursor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:85` |
| op-a638415af6566765 | call/self | `self.setTitleBar` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:77` |
| op-976af0a0254dfcea | call/self | `self.setToolTip` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:307` |
| op-1650a973079f777f | call/self | `self.setVerticalHeaderLabels` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:643` |
| op-4af318f23eeea0e4 | call/self | `self.setWidget` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:102` |
| op-798aa40746d27abf | call/self | `self.setWidgetResizable` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:99` |
| op-54f4930aac2ba878 | call/self | `self.setWindowIcon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:508` |
| op-85967aaf416f9f3c | call/self | `self.setWindowTitle` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:509` |
| op-319bda9a216e05bc | call/self | `self.set_default_style` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:88` |
| op-0acc3be8e6ca1340 | call/self | `self.set_next` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:67` |
| op-83bdbacb20986d5e | call/self | `self.set_screenshot_interval` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:333` |
| op-857e15b0d18f719e | call/self | `self.set_serial` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/connection.py:45` |
| op-e6b957b2af623ce2 | call/self | `self.set_shadow_effect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:46` |
| op-4b44a483d94f7b22 | call/self | `self.set_up_atx_agent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:424` |
| op-9e57add3653c1c7f | call/self | `self.settingLabel.setObjectName` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:124` |
| op-3211ec976336a116 | call/self | `self.settingLabel.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/glob.py:82` |
| op-02eb21b88c0dd128 | call/self | `self.setup_atx_agent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:247` |
| op-c6fb26629ccec096 | call/self | `self.shell` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/uiautomator2_client.py:181` |
| op-cb9f4551381d9bdf | call/self | `self.shm.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ipc_manager.py:49` |
| op-74f19b62046b9213 | call/self | `self.shm.unlink` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ipc_manager.py:50` |
| op-daba781c2f096488 | call/self | `self.show` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:42` |
| op-64688c00e6c04bad | call/self | `self.shutdown` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:962` |
| op-08e0d44e1ee11f83 | call/self | `self.signal_stop` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:480` |
| op-9f274c435d5f7cc2 | call/self | `self.signal_update.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:312` |
| op-b5333a74799c2aa4 | call/self | `self.signal_update.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:556` |
| op-696b2cfee13fa8d5 | call/self | `self.signals.get` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:92` |
| op-c6186ad5f0890389 | call/self | `self.size` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:824` |
| op-ddf63d52bb09e60d | call/self | `self.solve` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:442` |
| op-61b614cb042e64c1 | call/self | `self.spinner.start` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `deploy/installer/_installer.py:771` |
| op-b3f023b0adaa27d7 | call/self | `self.splashScreen.finish` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:444` |
| op-b1cebb8a851da0ee | call/self | `self.splashScreen.setIconSize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:397` |
| op-13ec6631909e450f | call/self | `self.stackedWidget.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:487` |
| op-6f9282d6e36fe672 | call/self | `self.stackedWidget.currentWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:545` |
| op-809f1bb5aba1764e | call/self | `self.stackedWidget.deleteLater` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:82` |
| op-3d5bacaf1503b62a | call/self | `self.stackedWidget.setCurrentWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:538` |
| op-a7a94b860bfe76bf | call/self | `self.start_background_animation` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:142` |
| op-60e59cb34017ce51 | call/self | `self.start_check_emulator_stat` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:309` |
| op-e1bbcab0fd39d213 | call/self | `self.start_emulator` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:315` |
| op-c6be4dc6c6062e86 | call/self | `self.start_scheduler` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:466` |
| op-6fac34bceef32713 | call/self | `self.start_shutdown` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:987` |
| op-437501647c20dba7 | call/self | `self.startup_card.button.click` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:162` |
| op-7d9eef723b6c8bb7 | call/self | `self.startup_card.button.setText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:261` |
| op-f5822484b9481eb3 | call/self | `self.startup_card.clicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:157` |
| op-30cb61972eb7b8ff | call/self | `self.startup_card.hBoxLayout.insertSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:87` |
| op-5735894482f052c6 | call/self | `self.startup_card.hBoxLayout.insertWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:86` |
| op-34e640116019ebca | call/self | `self.startup_card.setContent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:293` |
| op-d9f4a0ee577b190c | call/self | `self.startup_card.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:88` |
| op-7241fc969f4d9d1b | call/self | `self.status_label.setWordWrap` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:273` |
| op-b4c31d88e4ab4e42 | call/self | `self.stop` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:265` |
| op-1b2b076144737eee | call/self | `self.stop_all_tasks` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:858` |
| op-ce62b1abf7058dac | call/self | `self.stop_scheduler` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/runtime.py:464` |
| op-bacc362c023ee036 | call/self | `self.stream.decrypt` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy_proxy.py:29` |
| op-9c8a69cc055fc2cf | call/self | `self.stream.encrypt` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/remote/scrcpy_proxy.py:30` |
| op-bf958fdb5d20e015 | call/self | `self.stream_loop_thread.start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/core.py:211` |
| op-068973e776879a51 | call/self | `self.student_name.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/cafeInvite.py:39` |
| op-762f6939073bcdb9 | call/self | `self.sub_view.Layout` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:215` |
| op-d6aacba7acd90dd9 | call/self | `self.swipe` | 30 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/image.py:211` |
| op-cc5dcc7461b3088f | call/self | `self.swipe_scrcpy` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/device/scrcpy_client.py:76` |
| op-ff0356bae4dfc10e | call/self | `self.switch_assets.checkedChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:84` |
| op-c2471b201171f406 | call/self | `self.switch_assets.setChecked` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:83` |
| op-8a3811efd7653fd7 | call/self | `self.switch_assets.setOffText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:82` |
| op-1d250510b2e389e6 | call/self | `self.switch_assets.setOnText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:81` |
| op-b2add99e36a55cb6 | call/self | `self.tabBar.addBAASTab` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:624` |
| op-55536036adc930b8 | call/self | `self.tabBar.currentChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:494` |
| op-65adc9a396a85e33 | call/self | `self.tabBar.currentTab` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:540` |
| op-d29d561f3bc18c66 | call/self | `self.tabBar.removeTab` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:621` |
| op-d08c4c5b0b014b3c | call/self | `self.tabBar.setMovable` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:347` |
| op-56371bce5113b2b3 | call/self | `self.tabBar.setScrollable` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:350` |
| op-7d00072c1fb12253 | call/self | `self.tabBar.setTabMaximumWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:348` |
| op-863b14ca5d3b3539 | call/self | `self.tabBar.setTabSelectedBackgroundColor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:351` |
| op-1c7259279ca19f7c | call/self | `self.tabBar.setTabShadowEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:349` |
| op-9be402903e45981e | call/self | `self.tabBar.tabAddRequested.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:495` |
| op-9e28e68d06bad5d9 | call/self | `self.tabBar.tabCloseRequested.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:496` |
| op-92e6f5c6c1854815 | call/self | `self.tabBar.tabRegion` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:386` |
| op-b71cc1a50f34a199 | call/self | `self.tabBar.x` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:385` |
| op-53d2f2bc28d210d1 | call/self | `self.tabCloseRequested.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:291` |
| op-73e5961c14d803b3 | call/self | `self.tabMaximumWidth` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:283` |
| op-7b50dacc3b1e37b9 | call/self | `self.tabMinimumWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:283` |
| op-ac059ac208e185e3 | call/self | `self.table.hide` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:76` |
| op-9ef3e219eda287da | call/self | `self.table.reset_table` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:106` |
| op-1dbdc881f0dbddb5 | call/self | `self.table.show` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:116` |
| op-a5c0b9b3f4da105e | call/self | `self.tableLayout.addLayout` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:184` |
| op-7f0ccf6bf90376bb | call/self | `self.tableLayout.addWidget` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:158` |
| op-24cb439f94dc8cf9 | call/self | `self.tableLayout.setSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:151` |
| op-60676cf6c9c09b61 | call/self | `self.tableView.clearContents` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:171` |
| op-188cf8575df9e09b | call/self | `self.tableView.deleteLater` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:173` |
| op-6e3dfc8d53ab9e6e | call/self | `self.tableView.horizontalHeader` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:109` |
| op-46fadc8bd8240cf0 | call/self | `self.tableView.itemClicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:80` |
| op-3faaa99deff8d9d3 | call/self | `self.tableView.setCellWidget` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:117` |
| op-5eb559943ac3e3c4 | call/self | `self.tableView.setColumnCount` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:108` |
| op-3137ff40b038c212 | call/self | `self.tableView.setColumnWidth` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:112` |
| op-8c4947b58da31629 | call/self | `self.tableView.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:56` |
| op-e079195e6242e08c | call/self | `self.tableView.setHorizontalHeaderLabels` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:110` |
| op-928390ba8b13dd5f | call/self | `self.tableView.setItem` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/serverConfig.py:79` |
| op-851a662f5f8cbc98 | call/self | `self.tableView.setRowCount` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:107` |
| op-b89a05e0ab03b82e | call/self | `self.tableView.setWordWrap` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:106` |
| op-4c35b75edf72578c | call/self | `self.tableView.update` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:279` |
| op-64c6543ab62091aa | call/self | `self.table_view.deleteLater` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:79` |
| op-e02d848d16d62160 | call/self | `self.table_view.horizontalHeader` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:105` |
| op-8f17099b710b1d03 | call/self | `self.table_view.setColumnCount` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:98` |
| op-b55cc86b2b1bed02 | call/self | `self.table_view.setColumnWidth` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:102` |
| op-204bdc7671863085 | call/self | `self.table_view.setHorizontalHeaderLabels` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:109` |
| op-1549f121c0f4b98c | call/self | `self.table_view.setItem` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:127` |
| op-1cf8c0e303a92a9b | call/self | `self.table_view.setObjectName` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:118` |
| op-d610c66bb747551a | call/self | `self.table_view.setRowCount` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:125` |
| op-595a25c8c90905d6 | call/self | `self.table_view.setSelectionBehavior` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:99` |
| op-5f705a283e8e0497 | call/self | `self.table_view.setSelectionMode` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:100` |
| op-1cf062dd0cf16e57 | call/self | `self.table_view.verticalHeader` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/history.py:107` |
| op-8d284d93a33479a1 | call/self | `self.test_completed.emit` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:46` |
| op-8e41409ed40266e5 | call/self | `self.test_models` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:102` |
| op-c60adc7b84aa280c | call/self | `self.test_result_table.horizontalHeader` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:284` |
| op-157423fa4bdb9c16 | call/self | `self.test_result_table.item` | 12 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:487` |
| op-d332c85269d56cba | call/self | `self.test_result_table.rowCount` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:493` |
| op-ae9bf3dd925b2848 | call/self | `self.test_result_table.setColumnCount` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:280` |
| op-ac753c59ef6d823a | call/self | `self.test_result_table.setColumnWidth` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:287` |
| op-51f82a465189c745 | call/self | `self.test_result_table.setFixedHeight` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:456` |
| op-29f16fa75c74519b | call/self | `self.test_result_table.setHorizontalHeaderLabels` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:281` |
| op-fe774e411046f965 | call/self | `self.test_result_table.setItem` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:483` |
| op-324437d0bd225c57 | call/self | `self.test_result_table.setRowCount` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:454` |
| op-c71fbc72ab77556d | call/self | `self.text` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:173` |
| op-ee01b17a5458b03e | call/self | `self.textCursor` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:79` |
| op-5231fc654edbfbfb | call/self | `self.textEdit.setHtml` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:21` |
| op-ba9b5424ebfded09 | call/self | `self.textEdit.setReadOnly` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/readme.py:20` |
| op-f7b0966d665ae5f0 | call/self | `self.themeCard.optionChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:227` |
| op-1f6217b22c12062a | call/self | `self.themeColorCard.colorChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/settings.py:228` |
| op-2c1ffa425e671ae7 | call/self | `self.thenCombo.addItems` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:35` |
| op-3907d422c49f0c7c | call/self | `self.thenCombo.currentTextChanged.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:51` |
| op-0f16221ab9246b99 | call/self | `self.thenCombo.setCurrentText` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:41` |
| op-8d2f3c96de33240e | call/self | `self.thenSignal.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/home.py:278` |
| op-38de1993f0920d41 | call/self | `self.thread_starter` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:436` |
| op-d12d3c5797580782 | call/self | `self.threads.setdefault` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `main.py:55` |
| op-c866b2e4830fe9b2 | call/self | `self.times.append` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:142` |
| op-059bd6a121a5a8ed | call/self | `self.titleBar.height` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:93` |
| op-524d4e34c9e6a8a9 | call/self | `self.titleBar.move` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:92` |
| op-22b42b9e58493b03 | call/self | `self.titleBar.onHelpButtonClicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:497` |
| op-c2f83fe9ed18f351 | call/self | `self.titleBar.onHistoryButtonClicked.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:498` |
| op-7d9603d3b93ad358 | call/self | `self.titleBar.raise_` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:83` |
| op-54b40994f5a12206 | call/self | `self.titleBar.resize` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:93` |
| op-8a36b6b5fe842b50 | call/self | `self.titleLineLayout.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:54` |
| op-1d7389e2b7c89f9a | call/self | `self.titleLineLayout.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:53` |
| op-17ff431c60b079d1 | call/self | `self.title_label.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:67` |
| op-578c2e95dc6f4ff1 | call/self | `self.title_label.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:81` |
| op-ee2899c01001bbc9 | call/self | `self.title_label.update` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:123` |
| op-640903644d61c0e7 | call/self | `self.tmp_path.mkdir` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/update/repository.py:543` |
| op-3fe958882f15448b | call/self | `self.toString` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:65` |
| op-6f51fa34938d3bc5 | call/self | `self.to_add_input.text` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:41` |
| op-1db422e1a7110120 | call/self | `self.to_add_lay.addWidget` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:28` |
| op-22bd7d3e92089f25 | call/self | `self.to_main_page` | 45 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:496` |
| op-5c0ae242720efbb7 | call/self | `self.toggled.connect` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:20` |
| op-10e3a11014397c6d | call/self | `self.touch` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/scrcpy/control.py:223` |
| op-13f3294cc94b0a1d | call/self | `self.tr` | 539 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:10` |
| op-c4bda4d4336e4888 | call/self | `self.translate_mission_types` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `develop_tools/auto_translate.py:135` |
| op-6e7cbe2ba9e7c322 | call/self | `self.triangle1.x_bounds` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/geometry/parallelogram.py:15` |
| op-8f4db6b678262b31 | call/self | `self.triangle2.x_bounds` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/geometry/parallelogram.py:16` |
| op-7e099ed66484c19d | call/self | `self.u2` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/cafe_reward.py:128` |
| op-8ff61e1bf9efd9e3 | call/self | `self.u2._adb_device.shell` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:372` |
| op-1a91cd644c5270b2 | call/self | `self.u2._wait_for_device` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:413` |
| op-4015b888bf0708be | call/self | `self.u2.app_current` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/restart.py:8` |
| op-e317adeb512756d4 | call/self | `self.u2.app_start` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/restart.py:30` |
| op-1a194632812f5433 | call/self | `self.u2.app_stop` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/refresh_uiautomator2.py:8` |
| op-3d493f7bbfdab09d | call/self | `self.u2.click` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/control/uiautomator2.py:10` |
| op-5b04753685cb922c | call/self | `self.u2.http.get` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:1108` |
| op-b3715737057ecd9b | call/self | `self.u2.path2url` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:418` |
| op-b8f9ab1937cb8e24 | call/self | `self.u2.push` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `module/de_clothes.py:7` |
| op-9622e190a640bed0 | call/self | `self.u2.screenshot` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:170` |
| op-08b19c4db7eba081 | call/self | `self.u2.swipe` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:741` |
| op-a9e871c1e4f71b78 | call/self | `self.u2.uiautomator.running` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:891` |
| op-53a585062efeaa40 | call/self | `self.u2.uiautomator.start` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:890` |
| op-49a3680692ed853a | call/self | `self.u2_client.get_connection` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/Baas_thread.py:1100` |
| op-2ab10b010e6ff42a | call/self | `self.u2_get_screenshot` | 1 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `module/cafe_reward.py:113` |
| op-96b58e17def3d99e | call/self | `self.u2_swipe` | 5 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `module/activities/bunnyChaserOnTheShip2.py:267` |
| op-930f3c8ebc336e09 | call/self | `self.unique_language` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/ocr/ocr.py:96` |
| op-44836a7fad8595e8 | call/self | `self.update_callback` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:706` |
| op-4717eac58df537f1 | call/self | `self.update_component` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:40` |
| op-32ed8d797708467a | call/self | `self.update_config_reserve_old` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/conf/initializer.py:103` |
| op-2c49624a116d7c3c | call/self | `self.update_create_priority` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:713` |
| op-39a09b71d28177b6 | call/self | `self.update_region` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/window_capture/windows/window_info.py:34` |
| op-7c0cbd2802e7023f | call/self | `self.update_screenshot_array` | 32 | device.input-capture | Runtime Device | `baas::script::host::DeviceHost` | PARITY-DEVICE-HOST | INVENTORIED | `core/color.py:17` |
| op-838644224a4675a5 | call/self | `self.update_settings` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/switch.py:275` |
| op-0200ff49a1f66b1a | call/self | `self.update_signal.emit` | 18 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:96` |
| op-652a2c41ca599ed2 | call/self | `self.update_style` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:19` |
| op-bfb511811f2533b0 | call/self | `self.update_valid_task_queue` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/scheduler.py:87` |
| op-cf013cb40701298d | call/self | `self.vBox.addLayout` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:121` |
| op-b50eafe121535c69 | call/self | `self.vBox.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:122` |
| op-bd8382b39aa6e8dc | call/self | `self.vBox.removeWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/featureSwitch.py:172` |
| op-4dad6c7ac0d43747 | call/self | `self.vBox1.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:68` |
| op-bb3439333c6008dd | call/self | `self.vBox2.addWidget` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/fragments/process.py:75` |
| op-bfbf49b63a0337e7 | call/self | `self.vBoxLayout.addLayout` | 22 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:38` |
| op-2da6403fb0cf8851 | call/self | `self.vBoxLayout.addSpacing` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/drillConfig.py:35` |
| op-86c3a7b63ccae6ae | call/self | `self.vBoxLayout.addWidget` | 8 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:88` |
| op-7f8c0e71380f7324 | call/self | `self.vBoxLayout.children` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:38` |
| op-591999be58584db4 | call/self | `self.vBoxLayout.count` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/formationConfig.py:114` |
| op-e62a96ebd3ec3d25 | call/self | `self.vBoxLayout.itemAt` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:80` |
| op-4ca6c993f8f14b99 | call/self | `self.vBoxLayout.removeItem` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/emulatorConfig.py:111` |
| op-b6a2b2635fd4613f | call/self | `self.vBoxLayout.setAlignment` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/expandTemplate.py:61` |
| op-e770f8a74dde9890 | call/self | `self.vBoxLayout.setContentsMargins` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/eventMapConfig.py:89` |
| op-f88c716959a04b3c | call/self | `self.vBoxLayout.setSpacing` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/schedulePriority.py:79` |
| op-3faf5f1d46532fcd | call/self | `self.vBoxLayout.widget` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:58` |
| op-05d377c940aebbe8 | call/self | `self.verticalHeader` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:639` |
| op-691dccd0a2fd3b28 | call/self | `self.verticalScrollBar` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:81` |
| op-6d643a269e028431 | call/self | `self.viewLayout.addLayout` | 7 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:133` |
| op-945e2887a86cd39b | call/self | `self.viewLayout.addWidget` | 14 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:17` |
| op-387b2e0110769687 | call/self | `self.viewLayout.setAlignment` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:137` |
| op-4566168a1f3f1ef0 | call/self | `self.viewLayout.setContentsMargins` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:138` |
| op-69de91e22e5d1c9b | call/self | `self.viewLayout.setSpacing` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:44` |
| op-9aed4804992ddb9a | call/self | `self.viewport` | 5 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:48` |
| op-5a05bd376501ccdc | call/self | `self.wait_uiautomator_start` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/Baas_thread.py:425` |
| op-7c30e890cb1faf5d | call/self | `self.warningLabel.setAlignment` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/scriptConfig.py:65` |
| op-229f894dd13101b1 | call/self | `self.warnings.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `tests/service/test_android_display_resize.py:25` |
| op-0a16f60735a083ce | call/self | `self.websocket.close` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/websocket_endpoint.py:49` |
| op-8e9eee333e3602a7 | call/self | `self.websocket.receive_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/websocket_endpoint.py:25` |
| op-05f8418f95b872ec | call/self | `self.websocket.send_bytes` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `service/transport/websocket_endpoint.py:40` |
| op-550a633e4e89c6ff | call/self | `self.white_list.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:70` |
| op-5f4971ab120a95b7 | call/self | `self.white_list.pop` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/friendWhiteList.py:106` |
| op-541c19f2475fabea | call/self | `self.widget.setMinimumWidth` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:24` |
| op-ff6f6c2fb907dabf | call/self | `self.widgetLayout.addWidget` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:102` |
| op-d8e7bd472427751d | call/self | `self.widgetLayout.setContentsMargins` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:79` |
| op-233c9f3cc858b439 | call/self | `self.width` | 3 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:93` |
| op-9ed6b55dcd82b0e5 | call/self | `self.windowIcon` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:396` |
| op-40ac2a966f1b925e | call/self | `self.workers.append` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/baasUpdateConfig.py:508` |
| op-f38c6455099bfd71 | call/self | `self.yesButton.setDisabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:25` |
| op-69c4e73836c68875 | call/self | `self.yesButton.setEnabled` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:31` |
| op-16bf381f6846ccb4 | call/self | `self.yesButton.setText` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/dialog_panel.py:21` |
| op-6c3a31641f9bd190 | call/super | `super().__enter__` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:105` |
| op-34ffe1f9608afbf6 | call/super | `super().__exit__` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/device/nemu_client.py:114` |
| op-f14aff1a94998074 | call/super | `super().__init__` | 83 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `core/config/config_set.py:17` |
| op-2b42d124a04b3205 | call/super | `super().accept` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:456` |
| op-3ab8c7c125e0f1c0 | call/super | `super().canDrag` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:383` |
| op-3eda9dd6fe13a70a | call/super | `super().clear` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/hotkey_manager.py:368` |
| op-469bdae97c624639 | call/super | `super().closeEvent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `window.py:512` |
| op-9fd474d98dad08a1 | call/super | `super().enterEvent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:146` |
| op-9964e88de4b16570 | call/super | `super().eventFilter` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/customized_ui.py:790` |
| op-53fe75d9570d7c4d | call/super | `super().leaveEvent` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:154` |
| op-6bec7daa31af4c6c | call/super | `super().resizeEvent` | 2 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/expand/createPriority.py:41` |
| op-47d00c6d1417b168 | call/super | `super().toggleExpand` | 4 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/components/template_card.py:240` |
| op-e0f3a402f1c9c294 | call/super | `super().translate` | 1 | UNCLASSIFIED | UNASSIGNED | `UNCLASSIFIED` | UNCLASSIFIED | UNCLASSIFIED | `gui/util/translator.py:59` |
| op-0cae9ef3fb4c66c7 | dispatch/dispatch-exact | `dispatch:channel:dev` | 2 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/runtime.py:678` |
| op-87cdb1540b780a0f | dispatch/dispatch-exact | `dispatch:channel:stable` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/runtime.py:678` |
| op-8ae0620201f47595 | dispatch/dispatch-exact | `dispatch:command:check_for_update` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:109` |
| op-df25e4c49a51847a | dispatch/dispatch-exact | `dispatch:command:control_device` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:131` |
| op-14d89ec1c91de82b | dispatch/dispatch-exact | `dispatch:command:copy_config` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:71` |
| op-b3fe1d383bc80b50 | dispatch/dispatch-exact | `dispatch:command:detect_adb` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:94` |
| op-a9ada96ecc9c13af | dispatch/dispatch-exact | `dispatch:command:export_config` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:79` |
| op-a21ecd408b588a2c | dispatch/dispatch-exact | `dispatch:command:import_config` | 2 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:87` |
| op-5e6980533f6585b0 | dispatch/dispatch-exact | `dispatch:command:restart_backend` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:123` |
| op-d79f6d034ef0c1c5 | dispatch/dispatch-exact | `dispatch:command:solve` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:32` |
| op-b59db7a27b9d009d | dispatch/dispatch-exact | `dispatch:command:start_scheduler` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:17` |
| op-2977a49fe41056fc | dispatch/dispatch-exact | `dispatch:command:status` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:139` |
| op-30a68f98ce0b49db | dispatch/dispatch-exact | `dispatch:command:stop_all_tasks` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:127` |
| op-d699a0664752d6e8 | dispatch/dispatch-exact | `dispatch:command:stop_scheduler` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:26` |
| op-b827842895336bfe | dispatch/dispatch-exact | `dispatch:command:swipe` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `module/explore_tasks/task_utils.py:450` |
| op-6363c0668b9a8d7c | dispatch/dispatch-exact | `dispatch:command:test_all_sha` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:102` |
| op-6c26297f9c629324 | dispatch/dispatch-exact | `dispatch:command:test_all_sha_stream` | 2 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/channels/trigger.py:31` |
| op-ee136395e71b9cd3 | dispatch/dispatch-exact | `dispatch:command:update_setup_toml` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:115` |
| op-10a69582cbabdea7 | dispatch/dispatch-exact | `dispatch:command:update_to_latest` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:119` |
| op-5475d71f8dec91bc | dispatch/dispatch-exact | `dispatch:command:update_to_latest_stream` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/channels/trigger.py:31` |
| op-0315a12d6f0c4aa5 | dispatch/dispatch-exact | `dispatch:command:valid_cdk` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:98` |
| op-5e1ea2af3db1633c | dispatch/dispatch-exact | `dispatch:command:wm size` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `tests/service/test_android_display_resize.py:15` |
| op-2baa38d6ce6e236a | dispatch/dispatch-exact | `dispatch:msg_type:change_password` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/ws_control.py:65` |
| op-7b220a6acd09f790 | dispatch/dispatch-exact | `dispatch:msg_type:list` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/channels/sync.py:89` |
| op-32fc5e05c7ce753d | dispatch/dispatch-exact | `dispatch:msg_type:patch` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/channels/sync.py:59` |
| op-5eff282da8cd122a | dispatch/dispatch-exact | `dispatch:msg_type:ping` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/ws_control.py:63` |
| op-4cc3b9680f850017 | dispatch/dispatch-exact | `dispatch:msg_type:pull` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/channels/sync.py:49` |
| op-ef26c84ede172c06 | dispatch/dispatch-exact | `dispatch:operation:add` | 3 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `service/utils/diff.py:60` |
| op-04b65e9d554f1040 | dispatch/dispatch-exact | `dispatch:operation:choose_and_change` | 1 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `module/explore_tasks/task_utils.py:371` |
| op-9df2347c660b6163 | dispatch/dispatch-exact | `dispatch:operation:disable_app_keptlive` | 1 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/mumu_manager_api.py:63` |
| op-20d093e0e44c7251 | dispatch/dispatch-exact | `dispatch:operation:enable_app_keptlive` | 1 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/mumu_manager_api.py:66` |
| op-f53a7fde55fd255b | dispatch/dispatch-exact | `dispatch:operation:end-turn` | 1 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `module/explore_tasks/task_utils.py:359` |
| op-532f50a29cd00db3 | dispatch/dispatch-exact | `dispatch:operation:get_device_path` | 1 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/mumu_manager_api.py:51` |
| op-ecc173f7ec95478a | dispatch/dispatch-exact | `dispatch:operation:get_launch_status` | 2 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/ldplayer_manager_api.py:35` |
| op-11a79e912eb8bc16 | dispatch/dispatch-exact | `dispatch:operation:get_manager_path` | 2 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/ldplayer_manager_api.py:33` |
| op-ddcc50c91c485d06 | dispatch/dispatch-exact | `dispatch:operation:get_nemu_client_path` | 1 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/mumu_manager_api.py:58` |
| op-9b4cd27cf88ea561 | dispatch/dispatch-exact | `dispatch:operation:get_path` | 2 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/ldplayer_manager_api.py:27` |
| op-d1fedd56210b9fc8 | dispatch/dispatch-exact | `dispatch:operation:remove` | 3 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `service/utils/diff.py:62` |
| op-32aafb16da72a7af | dispatch/dispatch-exact | `dispatch:operation:replace` | 3 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `service/utils/diff.py:60` |
| op-583f89b7a410c88f | dispatch/dispatch-exact | `dispatch:operation:start` | 2 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/ldplayer_manager_api.py:19` |
| op-20047b031270d474 | dispatch/dispatch-exact | `dispatch:operation:stop` | 2 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `core/device/emulator_manager/ldplayer_manager_api.py:24` |
| op-12bee89d0fdcd6a2 | dispatch/dispatch-exact | `dispatch:req_type:static_request` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/channels/provider.py:36` |
| op-69ed984feba6d5e0 | dispatch/dispatch-exact | `dispatch:req_type:status_request` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/channels/provider.py:44` |
| op-5e877663a78e28a1 | dispatch/dispatch-prefix | `dispatch:command:add_config*` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:54` |
| op-8a290edf9e56bb18 | dispatch/dispatch-prefix | `dispatch:command:remove_config*` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:63` |
| op-0bd1bb9867315333 | dispatch/dispatch-prefix | `dispatch:command:start_*` | 1 | service.transport | Service Platform | `baas::service::CommandDispatcher::dispatch` | PARITY-SERVICE-COMMAND | INVENTORIED | `service/api/commands.py:45` |
| op-7132c91be2a59963 | dispatch/dispatch-prefix | `dispatch:operation:click*` | 1 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `module/explore_tasks/task_utils.py:343` |
| op-c8fdb3c16449a73a | dispatch/dispatch-prefix | `dispatch:operation:exchange*` | 1 | task.action | Runtime Scheduling | `baas::script::host::TaskActionHost::dispatch` | PARITY-TASK-ACTION | INVENTORIED | `module/explore_tasks/task_utils.py:348` |
| op-2c01b452ac0a726f | registration/registry-key | `registry:core.Baas_thread.func_dict:activity_sweep` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:60` |
| op-8b6ce180ff312200 | registration/registry-key | `registry:core.Baas_thread.func_dict:arena` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:41` |
| op-2b794b9aa1733563 | registration/registry-key | `registry:core.Baas_thread.func_dict:cafe_reward` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:36` |
| op-dce2f7c21bf4f6d2 | registration/registry-key | `registry:core.Baas_thread.func_dict:clear_special_task_power` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:53` |
| op-2010a820136d2741 | registration/registry-key | `registry:core.Baas_thread.func_dict:collect_daily_free_power` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:68` |
| op-ac160129d146b14e | registration/registry-key | `registry:core.Baas_thread.func_dict:collect_daily_power` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:56` |
| op-d81bda7658f26b63 | registration/registry-key | `registry:core.Baas_thread.func_dict:collect_reward` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:50` |
| op-4a010a5b4b0a9f81 | registration/registry-key | `registry:core.Baas_thread.func_dict:common_shop` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:35` |
| op-f2e434f928012178 | registration/registry-key | `registry:core.Baas_thread.func_dict:create` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:42` |
| op-851a55a3015e8954 | registration/registry-key | `registry:core.Baas_thread.func_dict:dailyGameActivity` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:64` |
| op-31982ed71f3b3bac | registration/registry-key | `registry:core.Baas_thread.func_dict:de_clothes` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:54` |
| op-1613368cd3616efc | registration/registry-key | `registry:core.Baas_thread.func_dict:explore_activity_challenge` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:62` |
| op-e169264c7060f0f8 | registration/registry-key | `registry:core.Baas_thread.func_dict:explore_activity_mission` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:63` |
| op-69b7fd757e53ce59 | registration/registry-key | `registry:core.Baas_thread.func_dict:explore_activity_story` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:61` |
| op-24432a15d4e86b8d | registration/registry-key | `registry:core.Baas_thread.func_dict:explore_hard_task` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:44` |
| op-9f813f1701793e9f | registration/registry-key | `registry:core.Baas_thread.func_dict:explore_normal_task` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:43` |
| op-b99e9d6fc12ee64c | registration/registry-key | `registry:core.Baas_thread.func_dict:friend` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:65` |
| op-007c8d83ee8f4d74 | registration/registry-key | `registry:core.Baas_thread.func_dict:group` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:33` |
| op-3fad9eca914d4f6e | registration/registry-key | `registry:core.Baas_thread.func_dict:group_story` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:47` |
| op-0063afdbcebdc6a8 | registration/registry-key | `registry:core.Baas_thread.func_dict:hard_task` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:52` |
| op-36b80ff46336ef0b | registration/registry-key | `registry:core.Baas_thread.func_dict:joint_firing_drill` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:66` |
| op-3d88fc99327d2f8b | registration/registry-key | `registry:core.Baas_thread.func_dict:lesson` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:39` |
| op-2dad68a421cf6137 | registration/registry-key | `registry:core.Baas_thread.func_dict:mail` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:45` |
| op-971295f5e8c7f469 | registration/registry-key | `registry:core.Baas_thread.func_dict:main_story` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:46` |
| op-4c15040269836efa | registration/registry-key | `registry:core.Baas_thread.func_dict:mini_story` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:48` |
| op-e5bccd91bda17cf4 | registration/registry-key | `registry:core.Baas_thread.func_dict:momo_talk` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:34` |
| op-134d75dd3fa588f9 | registration/registry-key | `registry:core.Baas_thread.func_dict:no1_cafe_invite` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:37` |
| op-8382200887d1e9bc | registration/registry-key | `registry:core.Baas_thread.func_dict:no2_cafe_invite` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:38` |
| op-b881534832a33537 | registration/registry-key | `registry:core.Baas_thread.func_dict:normal_task` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:51` |
| op-3be7809eba86159e | registration/registry-key | `registry:core.Baas_thread.func_dict:pass` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:67` |
| op-eb6532c1f222b603 | registration/registry-key | `registry:core.Baas_thread.func_dict:refresh_uiautomator2` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:59` |
| op-837923b28b297359 | registration/registry-key | `registry:core.Baas_thread.func_dict:restart` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:58` |
| op-143db113e939646a | registration/registry-key | `registry:core.Baas_thread.func_dict:rewarded_task` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:40` |
| op-e8895a9e095b93e4 | registration/registry-key | `registry:core.Baas_thread.func_dict:scrimmage` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:49` |
| op-e54a43811192988c | registration/registry-key | `registry:core.Baas_thread.func_dict:tactical_challenge_shop` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:55` |
| op-25fb6896e6ec1d61 | registration/registry-key | `registry:core.Baas_thread.func_dict:total_assault` | 1 | task.scheduler | Runtime Scheduling | `baas::script::host::TaskRegistry::register_task` | PARITY-TASK-REGISTRY | INVENTORIED | `core/Baas_thread.py:57` |
| op-574a2941cba7322d | registration/registry-key | `registry:service.transport.pipe_server._HANDLERS:provider` | 1 | service.transport | Service Platform | `baas::service::TransportRegistry::open_channel` | PARITY-SERVICE-CHANNEL | INVENTORIED | `service/transport/pipe_server.py:24` |
| op-abb619c31cbefa86 | registration/registry-key | `registry:service.transport.pipe_server._HANDLERS:remote` | 1 | service.transport | Service Platform | `baas::service::TransportRegistry::open_channel` | PARITY-SERVICE-CHANNEL | INVENTORIED | `service/transport/pipe_server.py:27` |
| op-eaf63f438470c83e | registration/registry-key | `registry:service.transport.pipe_server._HANDLERS:sync` | 1 | service.transport | Service Platform | `baas::service::TransportRegistry::open_channel` | PARITY-SERVICE-CHANNEL | INVENTORIED | `service/transport/pipe_server.py:25` |
| op-618bdd0d44a04f51 | registration/registry-key | `registry:service.transport.pipe_server._HANDLERS:trigger` | 1 | service.transport | Service Platform | `baas::service::TransportRegistry::open_channel` | PARITY-SERVICE-CHANNEL | INVENTORIED | `service/transport/pipe_server.py:26` |
| op-43a63d09fe926edc | route/decorator | `route:GET:/android/wiki` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:119` |
| op-17070395ffc23831 | route/decorator | `route:GET:/android/wiki/proxy` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:131` |
| op-dfc9fb330755ed06 | route/decorator | `route:GET:/health` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:58` |
| op-ae710e37c8fcb881 | route/decorator | `route:GET:/system/logs` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:73` |
| op-489d24aa41b3eb81 | route/decorator | `route:POST:/android/active-config` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:99` |
| op-cd94786cbaebc729 | route/decorator | `route:POST:/android/toggle` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:108` |
| op-bd8f03da0d73ba30 | route/decorator | `route:POST:/auth/logout` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:46` |
| op-bee7d684663a363a | route/decorator | `route:POST:/auth/remember` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:24` |
| op-69fe2890eeef4094 | route/decorator | `route:POST:/system/logs/clear` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/http.py:92` |
| op-550a9519c1b73f4d | route/decorator | `route:WEBSOCKET:/ws/control` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/ws_control.py:25` |
| op-c74c41e67eb251f8 | route/decorator | `route:WEBSOCKET:/ws/provider` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/ws_provider.py:40` |
| op-9859e2cb83792e39 | route/decorator | `route:WEBSOCKET:/ws/remote` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/ws_remote.py:18` |
| op-16398c905b4ec7a6 | route/decorator | `route:WEBSOCKET:/ws/sync` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/ws_sync.py:52` |
| op-690f79887ed087be | route/decorator | `route:WEBSOCKET:/ws/trigger` | 1 | service.transport | Service Platform | `baas::service::Router::dispatch` | PARITY-SERVICE-ROUTE | INVENTORIED | `service/api/ws_trigger.py:19` |
<!-- END GENERATED OPERATION INDEX -->
