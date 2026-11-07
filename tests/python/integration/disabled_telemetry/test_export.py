"""Record exporter checks disabled by the test-mode no-export policy."""
import pytest


@pytest.mark.integration
@pytest.mark.parametrize("scenario", ["media", "export", "tls"])
def test_export_disabled_by_test_policy(scenario):
    pytest.skip("Collector-export checks are disabled in test mode; see tests/manual/disabled_otel_*.py")
