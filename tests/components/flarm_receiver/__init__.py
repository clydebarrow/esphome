from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # gnss_sentences.h reads gps::GPSData
    manifest.dependencies = manifest.dependencies + ["gps"]
