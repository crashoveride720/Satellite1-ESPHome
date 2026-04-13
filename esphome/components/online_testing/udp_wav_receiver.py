#!/usr/bin/env python3
import argparse
import socket
import wave


def main() -> int:
    parser = argparse.ArgumentParser(description="Receive UDP PCM and write a WAV file")
    parser.add_argument(
        "--host", default="0.0.0.0", help="Bind host (default: 0.0.0.0)"
    )
    parser.add_argument(
        "--port", type=int, default=5005, help="UDP port (default: 5005)"
    )
    parser.add_argument(
        "--rate", type=int, default=16000, help="Sample rate (default: 16000)"
    )
    parser.add_argument("--channels", type=int, default=1, help="Channels (default: 1)")
    parser.add_argument("--outfile", default="mic_capture.wav", help="Output WAV file")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))

    with wave.open(args.outfile, "wb") as wf:
        wf.setnchannels(args.channels)
        wf.setsampwidth(2)
        wf.setframerate(args.rate)

        print(f"Listening on {args.host}:{args.port}, writing to {args.outfile}")
        try:
            while True:
                data, _addr = sock.recvfrom(65535)
                if not data:
                    continue
                wf.writeframes(data)
        except KeyboardInterrupt:
            print("Stopping recorder")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
