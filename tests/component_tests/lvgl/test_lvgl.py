"""Tests for the lvgl gui component"""


def test_lvgl_is_setup(generate_main):
    """
    When the gui is set in the yaml file if should be registered in main
    """
    # Given

    # When
    main_cpp = generate_main("tests/component_tests/lvgl/test_lvgl.yaml")
    assert "lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xA0A0A)," in main_cpp
    assert "charging_label = lv_label_create(lv_scr_act());" in main_cpp
    assert "lv_obj_set_style_y(charging_label, 30" in main_cpp
