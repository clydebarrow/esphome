import esphome.codegen as cg
from esphome.types import ConfigType
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    async def to_code_testing(config: ConfigType) -> None:
        cg.add_define("USE_FILE")

    manifest.to_code = to_code_testing
    # The test build configures no file entries, so a list config would never run to_code
    manifest.multi_conf = False
    manifest.dependencies = manifest.dependencies + ["filesystem", "storage"]
