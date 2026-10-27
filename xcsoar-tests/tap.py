"""Tap XCSoar's screen through the "tap" action of xcsoar-tab5-tap.yaml: python tap.py HOST X,Y [X,Y ...]"""

import asyncio
import sys

from aioesphomeapi import APIClient


async def main() -> None:
    client = APIClient(sys.argv[1], 6053, None)
    await client.connect(login=True)
    _, services = await client.list_entities_services()
    service = next(s for s in services if s.name == "tap")
    for xy in sys.argv[2:]:
        x, y = map(int, xy.split(","))
        result = client.execute_service(service, {"x": x, "y": y})
        if asyncio.iscoroutine(result):
            await result
        await asyncio.sleep(0.5)
    await client.disconnect()


asyncio.run(main())
