from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The driver is a storage platform on an SPI bus.
    manifest.dependencies = manifest.dependencies + ["spi", "storage"]
