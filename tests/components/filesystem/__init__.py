from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The tests run the filesystem on a RAM disk. The image platform loads runtime_image, whose test
    # build defines USE_LVGL, so the LVGL headers are needed too.
    manifest.dependencies = manifest.dependencies + ["storage", "ram_disk", "lvgl"]
