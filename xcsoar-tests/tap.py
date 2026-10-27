"""Tap XCSoar's screen through the "tap" action of xcsoar-tab5-tap.yaml: python tap.py HOST X,Y [X,Y ...]

A "+SECONDS" argument sets the time to the next tap (default 0.5); "+0.15" makes a double tap that opens the menu.
"""

import asyncio
import sys

from aioesphomeapi import APIClient


async def main() -> None:
    client = APIClient(sys.argv[1], 6053, None)
    await client.connect(login=True)
    _, services = await client.list_entities_services()
    service = next(s for s in services if s.name == "tap")
    gap = 0.5
    for arg in sys.argv[2:]:
        if arg.startswith("+"):
            gap = float(arg[1:])
            continue
        x, y = map(int, arg.split(","))
        result = client.execute_service(service, {"x": x, "y": y})
        if asyncio.iscoroutine(result):
            await result
        await asyncio.sleep(gap)
    await client.disconnect()


asyncio.run(main())
