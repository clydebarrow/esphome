from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    # The tests cover the choice, which needs neither Wi-Fi nor the SkyEcho; the component itself does
    manifest.dependencies = []
    manifest.resources = [
        resource
        for resource in manifest.resources
        if not resource.resource.startswith("skyecho_wifi.")
    ]
