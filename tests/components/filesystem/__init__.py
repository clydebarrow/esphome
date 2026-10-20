from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The tests run the filesystem on a RAM disk.
    manifest.dependencies = manifest.dependencies + ["storage", "ram_disk"]
