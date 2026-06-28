#!/usr/bin/env python3
"""Probe ESP-CAM / webcam index 0 — requiere permiso de dispositivo."""
import sys
try:
    import cv2
    cap = cv2.VideoCapture(0)
    ok, frame = cap.read()
    cap.release()
    print("capture_ok" if ok else "capture_fail")
    sys.exit(0 if ok else 1)
except Exception as e:
    print(f"error: {e}", file=sys.stderr)
    sys.exit(2)
