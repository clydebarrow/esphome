from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # Only the page and command parsers are tested, which need no layers
    manifest.config_schema = None
