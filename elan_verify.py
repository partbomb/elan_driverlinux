#!/usr/bin/env python3
import sys
import logging
import os

logging.basicConfig(filename='/tmp/elan_auth.log', level=logging.DEBUG,
                    format='%(asctime)s - %(message)s')

try:
    import usb.core
    import time
    import numpy as np
    import cv2
except Exception:
    sys.exit(1)

DIR = '/etc/elan_fingerprint'
sift = cv2.SIFT_create()
templates = []

def process_image(img):
    blur = cv2.GaussianBlur(img, (5, 5), 0)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
    return clahe.apply(blur)

# 1. ЗАГРУЖАЕМ ВСЕ 7 ЭТАЛОНОВ ОДИН РАЗ (Супер-ускорение)
try:
    for filename in os.listdir(DIR):
        if not filename.endswith('.png'): continue
        path = os.path.join(DIR, filename)
        img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        if img is not None:
            clean = process_image(img)
            kp, des = sift.detectAndCompute(clean, None)
            if des is not None and len(kp) >= 10:
                templates.append((kp, des, filename))
except Exception as e:
    logging.error(f"Ошибка загрузки эталонов: {e}")

def verify(captured_img):
    captured_clean = process_image(captured_img)
    kp2, des2 = sift.detectAndCompute(captured_clean, None)

    if des2 is None or len(kp2) < 10:
        return False

    bf = cv2.BFMatcher(cv2.NORM_L2)
    # Сверяем с уже готовыми данными из оперативной памяти
    for kp1, des1, filename in templates:
        matches = bf.knnMatch(des1, des2, k=2)

        good_matches = []
        for match_pair in matches:
            if len(match_pair) == 2:
                m, n = match_pair
                if m.distance < 0.7 * n.distance:
                    good_matches.append(m)

        logging.info(f"Сверка с {filename}: {len(good_matches)} точек")
        if len(good_matches) >= 9:
            return True

    return False

def main():
    try:
        dev = usb.core.find(idVendor=0x04f3, idProduct=0x0c4f)
        if dev is None: sys.exit(1)

        if dev.is_kernel_driver_active(0):
            try: dev.detach_kernel_driver(0)
            except: pass

        dev.set_configuration()
        EP_OUT, EP_IN = 0x01, 0x82

        # 2. ДАЕМ ЖЕЛЕЗУ ВРЕМЯ ПРОСНУТЬСЯ (0.3 сек)
        dev.write(EP_OUT, [0x40, 0x31])
        time.sleep(0.3)

        start_time = time.time()

        while time.time() - start_time < 3.0:
            try:
                dev.write(EP_OUT, [0x40, 0x3f])
                dev.write(EP_OUT, [0x00, 0x09])

                data = dev.read(EP_IN, 12800, timeout=1000)
                raw_data = np.frombuffer(data, dtype='<u2').reshape((80, 80))

                if np.std(raw_data) > 300:
                    captured_img = cv2.normalize(raw_data, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)
                    if verify(captured_img):
                        sys.exit(0)
            except usb.core.USBError:
                pass

            # Если палец не подошел, ждем долю секунды перед новым кадром
            time.sleep(0.1)

        sys.exit(1)
    except Exception:
        sys.exit(1)

if __name__ == "__main__":
    main()
