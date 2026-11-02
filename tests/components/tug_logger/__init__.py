from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The store reads and writes its records as JSON.
    manifest.dependencies = manifest.dependencies + ["json"]
