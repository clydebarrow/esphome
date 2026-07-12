"""Tests for the skyecho component code generation."""

from collections.abc import Callable


def test_skyecho_component_is_registered(generate_main: Callable[[str], str]) -> None:
    """The skyecho hub is created and registered as a polling component."""
    main_cpp = generate_main("tests/component_tests/skyecho/test_skyecho.yaml")

    assert "new(skyecho1) skyecho::SkyEcho();" in main_cpp
    assert "App.register_component_(skyecho1," in main_cpp
    # Polling component defaults to a 1s update interval
    assert "skyecho1->set_update_interval(1000);" in main_cpp


def test_skyecho_sets_flarm_uart_when_configured(
    generate_main: Callable[[str], str],
) -> None:
    """When flarm_uart is given, the UART bus is wired to the hub."""
    main_cpp = generate_main("tests/component_tests/skyecho/test_skyecho.yaml")

    assert "skyecho1->set_flarm_uart(flarm_bus);" in main_cpp


def test_skyecho_omits_flarm_uart_when_absent(
    generate_main: Callable[[str], str],
) -> None:
    """The flarm_uart option is optional and must not be wired when omitted."""
    main_cpp = generate_main("tests/component_tests/skyecho/test_skyecho_no_uart.yaml")

    assert "new(skyecho1) skyecho::SkyEcho();" in main_cpp
    assert "set_flarm_uart" not in main_cpp


def test_skyecho_switch_is_linked_to_hub(
    generate_main: Callable[[str], str],
) -> None:
    """The simulate switch is created and linked back to its parent hub."""
    main_cpp = generate_main("tests/component_tests/skyecho/test_skyecho.yaml")

    assert "new(simulate_switch) skyecho::SkyEchoSimulateSwitch();" in main_cpp
    assert "simulate_switch->set_parent(skyecho1);" in main_cpp
    assert 'App.register_switch(simulate_switch, "Simulate",' in main_cpp


def test_skyecho_text_sensor_types_pick_distinct_classes(
    generate_main: Callable[[str], str],
) -> None:
    """The typed text_sensor schema maps each type to its own C++ class."""
    main_cpp = generate_main("tests/component_tests/skyecho/test_skyecho.yaml")

    # nmea -> SkyEchoTextSensor, traffic_list -> SkyEchoTrafficListSensor
    assert "new(nmea_sensor) skyecho::SkyEchoTextSensor();" in main_cpp
    assert "new(traffic_sensor) skyecho::SkyEchoTrafficListSensor();" in main_cpp
    assert "nmea_sensor->set_parent(skyecho1);" in main_cpp
    assert "traffic_sensor->set_parent(skyecho1);" in main_cpp
    assert "App.register_text_sensor(nmea_sensor," in main_cpp
    assert "App.register_text_sensor(traffic_sensor," in main_cpp
