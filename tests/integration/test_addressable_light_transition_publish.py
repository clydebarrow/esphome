"""Integration test for transition_state_publish_interval on addressable lights.

The addressable transition writes the LEDs directly, so the light must report
values that follow the transition's progress rather than the start state.
"""

from __future__ import annotations

import asyncio
from itertools import pairwise

from aioesphomeapi import EntityState, LightInfo, LightState
import pytest

from .state_utils import InitialStateHelper, require_entity
from .types import APIClientConnectedFactory, RunCompiledFunction


def _visible_brightness(state: LightState) -> float:
    return state.brightness if state.state else 0.0


@pytest.mark.asyncio
async def test_addressable_light_transition_publish(
    yaml_config: str,
    run_compiled: RunCompiledFunction,
    api_client_connected: APIClientConnectedFactory,
) -> None:
    """1 s turn-on and turn-off with a 200 ms interval publish a brightness ramp."""
    async with run_compiled(yaml_config), api_client_connected() as client:
        entities, _ = await client.list_entities_services()
        loop = asyncio.get_running_loop()
        values: list[float] = []
        key = 0
        target = 0.0
        done = asyncio.Event()

        def on_state(state: EntityState) -> None:
            if not isinstance(state, LightState) or state.key != key:
                return
            brightness = _visible_brightness(state)
            values.append(brightness)
            if brightness == pytest.approx(target, abs=0.01):
                done.set()

        helper = InitialStateHelper(entities)
        client.subscribe_states(helper.on_state_wrapper(on_state))
        await helper.wait_for_initial_states()

        for object_id in ("single_led", "strip"):
            key = require_entity(entities, object_id, LightInfo).key
            for turn_on in (True, False):
                values.clear()
                done.clear()
                target = 1.0 if turn_on else 0.0
                start = loop.time()
                client.light_command(key=key, state=turn_on, brightness=1.0)
                async with asyncio.timeout(5):
                    await done.wait()

                ramp = values if turn_on else [1.0 - v for v in values]
                assert len(ramp) >= 5, (object_id, turn_on, values)
                assert len([v for v in ramp if 0.1 < v < 0.9]) >= 2, (object_id, values)
                assert all(b >= a - 0.01 for a, b in pairwise(ramp)), (
                    object_id,
                    values,
                )
                assert loop.time() - start >= 0.8, (object_id, turn_on)
