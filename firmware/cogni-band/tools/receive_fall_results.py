import argparse
import json
import socket
import time

ALERT_DISPLAY_SECONDS = 3.0


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Recibe resultados de caída por UDP desde la pulsera ALBA."
    )
    parser.add_argument("--port", type=int, default=3333)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port debe estar entre 1 y 65535")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
        receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        receiver.bind(("", args.port))
        print(f"Esperando resultados UDP en 0.0.0.0:{args.port}...")
        suppress_until = 0.0
        while True:
            payload, address = receiver.recvfrom(2048)
            message = payload.decode("utf-8", errors="replace")
            try:
                result = json.loads(message)
            except json.JSONDecodeError:
                result = None

            if isinstance(result, dict) and result.get("type") == "fall_candidate":
                probability = result.get("probability")
                peak_g = result.get("peak_g")
                print()
                print("=" * 60)
                print("  CAÍDA PROBABLE DETECTADA")
                if isinstance(probability, (int, float)):
                    print(f"  Probabilidad del modelo: {probability:.3f}")
                if isinstance(peak_g, (int, float)):
                    print(f"  Pico de aceleración: {peak_g:.3f} g")
                print("=" * 60, flush=True)
                suppress_until = time.monotonic() + ALERT_DISPLAY_SECONDS
            elif time.monotonic() >= suppress_until:
                print(f"{address[0]} {message}")


if __name__ == "__main__":
    main()
