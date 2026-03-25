from fpioa_manager import fm
from machine import UART
import time

# ========= UART pin mapping (adjust to your board wiring) =========
# K210 TX -> ESP32 GPIO47 (ESP RX)
# K210 RX <- ESP32 GPIO48 (ESP TX)
K210_UART_RX_IO = 6
K210_UART_TX_IO = 8
UART_PORT = UART.UART2
UART_BAUD = 115200

fm.register(K210_UART_RX_IO, fm.fpioa.UART2_RX)
fm.register(K210_UART_TX_IO, fm.fpioa.UART2_TX)

uart = UART(UART_PORT, UART_BAUD, 8, 0, 0, timeout=200, read_buf_len=1024)

last_tx = time.ticks_ms()
mode = 0
hello_sent = False

print("[K210] uart test start")
print("[K210] RX_IO={}, TX_IO={}, BAUD={}".format(K210_UART_RX_IO, K210_UART_TX_IO, UART_BAUD))

try:
    while True:
        if not hello_sent:
            uart.write("HELLO K210\n")
            hello_sent = True
            print("[K210] TX: HELLO K210")

        now = time.ticks_ms()
        if time.ticks_diff(now, last_tx) >= 1500:
            last_tx = now
            if mode == 0:
                uart.write("RECOG 1 91.7\n")
                print("[K210] TX: RECOG 1 91.7")
            else:
                uart.write("UNKNOWN 67.2\n")
                print("[K210] TX: UNKNOWN 67.2")
            mode = 1 - mode

        if uart.any():
            data = uart.read()
            if data:
                try:
                    print("[K210] RX:", data.decode("utf-8").strip())
                except Exception:
                    print("[K210] RX(raw):", data)

        time.sleep_ms(20)
except Exception as e:
    print("[K210] stopped:", e)
finally:
    uart.deinit()
    del uart
