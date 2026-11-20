from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The tests cover the flight log, which needs neither the GPS nor the radio; the component itself does
    manifest.dependencies = []
    manifest.resources = [
        resource
        for resource in manifest.resources
        if not resource.resource.startswith("glider_tracker.")
    ]
