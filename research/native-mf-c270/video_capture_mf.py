"""Physical Logitech C270 input through a native Windows Media Foundation reader.

This module deliberately does not use OpenCV/DirectShow to read the C270:
OpenCV is only used later in the existing face-processing pipeline. The
selected physical camera is resolved inside AvCamMfCapture.dll by its actual
USB VID/PID and video interface, never by an OBS or DirectShow index.
"""
from __future__ import annotations
import ctypes
import platform
import threading
import time
from pathlib import Path
from typing import Callable, Optional, Tuple

import numpy as np

_DLL_NAME = "AvCamMfCapture.dll"


def _native_dll_path() -> Path:
    from modules.paths import install_root
    return Path(install_root()) / _DLL_NAME


def _load_native_reader():
    path = _native_dll_path()
    if not path.is_file():
        raise FileNotFoundError(
            f"Missing {path}. Install the C270 Media Foundation patch alongside Deep Live Cam."
        )
    dll = ctypes.WinDLL(str(path))
    dll.AvmfOpen.argtypes = [
        ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
    ]
    dll.AvmfOpen.restype = ctypes.c_int
    dll.AvmfRead.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_int,
    ]
    dll.AvmfRead.restype = ctypes.c_int
    dll.AvmfDescribe.argtypes = [
        ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint,
    ]
    dll.AvmfDescribe.restype = ctypes.c_int
    dll.AvmfClose.argtypes = [ctypes.c_void_p]
    dll.AvmfClose.restype = ctypes.c_int
    return dll


class VideoCapturer:
    """Preserves the upstream API, but reads a real USB C270 through MF.

    No fallback from C270 to a different DirectShow index. If MF fails, the
    error is reported rather than silently opening a virtual or wrong camera.
    """

    def __init__(self, device_index: int):
        self.device_index = device_index
        self.frame_callback: Optional[Callable] = None
        self._mutex = threading.RLock()
        self._native = None
        self._handle = ctypes.c_void_p()
        self._frame = None
        self._failures = 0
        self._start_profile = (640, 480, 30)
        self.actual_width = 0
        self.actual_height = 0
        self.actual_fps = 0.0
        self.is_running = False
        self.cap = None  # compatibility with upstream private consumers
        self._physical_name = ""
        if platform.system() != "Windows":
            raise RuntimeError("The native C270 reader requires Windows Media Foundation.")
        from pygrabber.dshow_graph import FilterGraph
        cameras = FilterGraph().get_input_devices()
        if device_index < 0 or device_index >= len(cameras):
            raise ValueError(
                f"Invalid selected camera index {device_index}; enumerated {len(cameras)}."
            )
        self._physical_name = str(cameras[device_index])
        if "c270" not in self._physical_name.lower():
            raise ValueError(
                f"Selected '{self._physical_name}' is not Logitech C270. "
                "This native prototype intentionally cannot open a different webcam."
            )

    def _description(self) -> str:
        if not self._native or not self._handle.value:
            return "no open MF source"
        message = ctypes.create_unicode_buffer(2048)
        if self._native.AvmfDescribe(self._handle, message, len(message)) == 0:
            return message.value
        return "unknown Media Foundation reader state"

    def start(self, width: int = 640, height: int = 480, fps: int = 30) -> bool:
        with self._mutex:
            self.release()
            # Request USB-safe VGA; the native reader selects an actual
            # supported C270 media type and outputs decoded BGR frames.
            width = max(160, min(int(width), 1280))
            height = max(120, min(int(height), 720))
            fps = max(1, min(int(fps), 30))
            self._start_profile = (width, height, fps)
            try:
                self._native = _load_native_reader()
                w, h = ctypes.c_int(), ctypes.c_int()
                result = self._native.AvmfOpen(width, height, fps,
                                               ctypes.byref(self._handle),
                                               ctypes.byref(w), ctypes.byref(h))
                if result != 0 or not self._handle.value:
                    raise RuntimeError(f"AvmfOpen code {result}: C270 MF device unavailable")
                self.actual_width, self.actual_height = w.value, h.value
                if not (160 <= w.value <= 1920 and 120 <= h.value <= 1080):
                    raise RuntimeError("MF returned invalid frame dimensions")
                description = self._description()
                print(f"[DLC.MF] open {description}", flush=True)
                self._frame = np.empty((h.value, w.value, 3), dtype=np.uint8)
                # Do not use declared FPS as actual speed. Measure real frames.
                good = 0
                begin = time.monotonic()
                while good < 5 and time.monotonic() - begin < 7.0:
                    code = self._native.AvmfRead(
                        self._handle, self._frame.ctypes.data_as(ctypes.c_void_p),
                        self._frame.nbytes, 1400,
                    )
                    if code == 0:
                        good += 1
                    elif code < 0:
                        raise RuntimeError(f"Native reader error {code}: {self._description()}")
                elapsed = max(time.monotonic() - begin, 0.001)
                if good < 2:
                    raise RuntimeError(f"Only {good} MF frames in {elapsed:.1f}s")
                self.actual_fps = max(0.1, (good - 1) / elapsed)
                self._failures = 0
                self.is_running = True
                self.cap = self._handle
                print(f"[DLC.MF] physical C270: {w.value}x{h.value}; "
                      f"frames={good}; measured={self.actual_fps:.1f}fps; "
                      "backend=Windows Media Foundation", flush=True)
                return True
            except Exception as error:
                print(f"[DLC.MF] start failed: {error}", flush=True)
                self.release()
                return False

    def read(self) -> Tuple[bool, Optional[np.ndarray]]:
        with self._mutex:
            if not self.is_running or not self._native or not self._handle.value:
                return False, None
            code = self._native.AvmfRead(
                self._handle, self._frame.ctypes.data_as(ctypes.c_void_p),
                self._frame.nbytes, 1200,
            )
            if code == 0:
                self._failures = 0
                # Copy: producer reuses the native output buffer next read.
                frame = self._frame.copy()
                if self.frame_callback:
                    self.frame_callback(frame)
                return True, frame
            self._failures += 1
            if self._failures == 1 or self._failures % 3 == 0:
                print(f"[DLC.MF] frame unavailable code={code}; "
                      f"{self._description()}", flush=True)
            if self._failures >= 3 or code < 0:
                self.is_running = False
            # Never synthesize video, recycle a stale frame as fresh,
            # or publish all-black frames under a healthy status.
            return False, None

    def recover(self) -> bool:
        # Bounded retry only; upstream UI capture worker owns this lifecycle.
        params = self._start_profile
        self.release()
        time.sleep(0.25)
        return self.start(*params)

    def release(self) -> None:
        with self._mutex:
            self.is_running = False
            self.cap = None
            if self._native and self._handle.value:
                outcome = self._native.AvmfClose(self._handle)
                if outcome:
                    print("[DLC.MF] native camera worker did not exit in 3s; "
                          "detached without killing the device thread", flush=True)
            self._native = None
            self._handle = ctypes.c_void_p()
            self._frame = None
            self._failures = 0
            self.actual_width = self.actual_height = 0
            self.actual_fps = 0.0

    def set_frame_callback(self, callback: Callable) -> None:
        self.frame_callback = callback
