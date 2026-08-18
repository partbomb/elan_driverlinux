#!/usr/bin/env python3
import usb.core
import time
import numpy as np
import cv2
import sys
import os

DIR = '/etc/elan_fingerprint'

def main():
    if not os.path.exists(DIR):
        os.makedirs(DIR)

    dev = usb.core.find(idVendor=0x04f3, idProduct=0x0c4f)
    if dev is None:
        print("Сканер не найден!")
        sys.exit(1)

    if dev.is_kernel_driver_active(0):
        try: dev.detach_kernel_driver(0)
        except: pass

    dev.set_configuration()
    EP_OUT, EP_IN = 0x01, 0x82

    print("Сканер готов.")
    input(">>> ПРИЛОЖИ ПАЛЕЦ И НАЖМИ ENTER <<<")

    try:
        dev.write(EP_OUT, [0x40, 0x31])
        time.sleep(0.1)
        dev.write(EP_OUT, [0x40, 0x3f])
        dev.write(EP_OUT, [0x00, 0x09])

        data = dev.read(EP_IN, 12800, timeout=5000)
        raw_data = np.frombuffer(data, dtype='<u2').reshape((80, 80))
        img_8bit = cv2.normalize(raw_data, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)

        # Считаем, сколько снимков уже есть, и сохраняем новый
        existing_files = [f for f in os.listdir(DIR) if f.startswith('template_') and f.endswith('.png')]
        next_idx = len(existing_files) + 1
        save_path = os.path.join(DIR, f'template_{next_idx}.png')

        cv2.imwrite(save_path, img_8bit)
        print(f"Успех! Эталон сохранен как {save_path}")

    except usb.core.USBError as e:
        print(f"Ошибка чтения: {e}")

if __name__ == "__main__":
    main()
