"""
Laptop script: ESP32-CAM ka video padho, YOLOv8n se person dhundo,
aur ESP32 car ko command bhejo (F / L / R / T / S).

Install:  pip install ultralytics opencv-python requests
Chalane se pehle neeche CAM_URL aur ROBOT ke IP badlo.
Band karne ke liye video window me 'q' dabao.

Is version me video ek alag thread me padhi jati hai, taaki hamesha
LATEST frame mile (purane frames jama hoke lag na banayein).
"""
import threading
import time
import cv2
import requests
from ultralytics import YOLO

CAM_URL = "http://10.23.69.108:81/stream"   # ESP32-CAM ka IP
ROBOT   = "http://10.23.69.62"             # ESP32 (car) ka IP

TARGET_CLASS = 0            # 0 = person, 39 = bottle
CONF = 0.4                  # detection confidence
STOP_RATIO = 0.7            # box height frame ke 70% se zyada = kaafi paas, ruko
LEFT_ZONE, RIGHT_ZONE = 0.4, 0.6   # beech ka 20% = seedha
LOST_FRAMES_BEFORE_SEARCH = 4      # itne frame target na dikhe tab search shuru


class LatestFrame:
    """Background me stream padhta rehta hai, sirf sabse naya frame rakhta hai."""

    def __init__(self, url):
        self.url = url
        self.frame = None
        self.lock = threading.Lock()
        self.running = True
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        cap = None
        while self.running:
            if cap is None or not cap.isOpened():
                cap = cv2.VideoCapture(self.url)
                cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            ok, f = cap.read()
            if not ok:
                cap.release()
                cap = None
                with self.lock:
                    self.frame = None
                time.sleep(1)
                continue
            with self.lock:
                self.frame = f
        if cap is not None:
            cap.release()

    def read(self):
        with self.lock:
            return None if self.frame is None else self.frame.copy()

    def stop(self):
        self.running = False


model = YOLO("yolov8n.pt")
session = requests.Session()


def send(cmd):
    try:
        session.get(f"{ROBOT}/cmd", params={"c": cmd}, timeout=0.3)
    except requests.RequestException:
        pass   # ek-do command miss ho jaye to koi baat nahi, agla frame bhej dega


reader = LatestFrame(CAM_URL)
lost = 0
last_cmd = "S"
print("Camera se jud raha hu... 'q' dabake band karo.")

while True:
    frame = reader.read()
    if frame is None:
        send("S")
        time.sleep(0.1)
        continue

    h, w = frame.shape[:2]
    result = model(frame, imgsz=320, conf=CONF,
                   classes=[TARGET_CLASS], verbose=False)[0]
    boxes = result.boxes

    if len(boxes) == 0:
        lost += 1
        cmd = "T" if lost >= LOST_FRAMES_BEFORE_SEARCH else "S"
    else:
        lost = 0
        # sabse bada box = sabse paas wala target
        xyxy = max(boxes.xyxy.tolist(), key=lambda b: (b[2] - b[0]) * (b[3] - b[1]))
        x1, y1, x2, y2 = xyxy
        cx = (x1 + x2) / 2

        if (y2 - y1) > STOP_RATIO * h:
            cmd = "S"            # goal pahunch gaye
        elif cx < w * LEFT_ZONE:
            cmd = "L"
        elif cx > w * RIGHT_ZONE:
            cmd = "R"
        else:
            cmd = "F"

        cv2.rectangle(frame, (int(x1), int(y1)), (int(x2), int(y2)), (0, 255, 0), 2)

    send(cmd)
    if cmd != last_cmd:
        print("CMD:", cmd)
        last_cmd = cmd

    cv2.putText(frame, f"CMD: {cmd}", (8, 24),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
    cv2.imshow("Robot view", frame)
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

send("S")
reader.stop()
cv2.destroyAllWindows()
