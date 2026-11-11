"""Copy a file to XCSoar's data directory on the SD card through the "write_file" action of xcsoar-tab5-bench.yaml.

python put_file.py HOST LOCAL_FILE REMOTE_PATH, where REMOTE_PATH is relative to /sdcard/XCSoar, e.g. logs/x.igc.
"""

import asyncio
import base64
from pathlib import Path
import sys

from aioesphomeapi import APIClient

CHUNK = 3072


async def main() -> None:
    client = APIClient(sys.argv[1], 6053, None)
    await client.connect(login=True)
    _, services = await client.list_entities_services()
    service = next(s for s in services if s.name == "write_file")
    data = Path(sys.argv[2]).read_bytes()
    for offset in range(0, max(len(data), 1), CHUNK):
        chunk = base64.b64encode(data[offset : offset + CHUNK]).decode()
        result = client.execute_service(
            service, {"path": sys.argv[3], "data": chunk, "append": offset > 0}
        )
        if asyncio.iscoroutine(result):
            await result
        # The action runs in the main loop; give it time so the queue doesn't overflow
        await asyncio.sleep(0.05)
        if offset // CHUNK % 100 == 0:
            print(f"{offset}/{len(data)}", flush=True)
    await client.disconnect()
    print(f"sent {len(data)} bytes")


asyncio.run(main())
