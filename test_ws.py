import asyncio
import json

import websockets


async def test():
    async with websockets.connect("ws://localhost:8765") as ws:
        await ws.send(json.dumps({"type": "scenario", "name": "flash_crash"}))
        print("Sent flash_crash")
        await asyncio.sleep(1)
        await ws.send(json.dumps({"type": "scenario", "name": "whale_buy"}))
        print("Sent whale_buy")
        await asyncio.sleep(1)
        await ws.send(json.dumps({"type": "scenario", "name": "vol_spike"}))
        print("Sent vol_spike")


asyncio.run(test())
