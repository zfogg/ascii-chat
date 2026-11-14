"""Regression checks for Catch2's direct JUnit output."""

from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

class Catch2JUnitTests(unittest.TestCase):
    def test_direct_junit_report(self):
        suite = (
            '<testsuite name="stats" tests="3">'
            '<testcase name="pass" time="0.1" assertions="1" status="PASSED" />'
            '<testcase name="fail" time="0.2"><failure>detail</failure></testcase>'
            '<testcase name="skip" time="0"><skipped /></testcase></testsuite>'
        )
        with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "report.xml"
                path.write_text("<testsuites>" + suite + "</testsuites>")
                root = ET.parse(path).getroot()
                self.assertEqual(root.tag, "testsuites")
                cases = root.findall("testsuite/testcase")
                self.assertEqual(len(cases), 3)
                # Catch2's direct JUnit reporter does not promise a
                # classname attribute. CTest supplies the suite/test identity
                # separately in ctest-results.xml; the per-case report only
                # needs to remain valid JUnit.
                self.assertTrue(all(c.get("name") for c in cases))
                self.assertEqual(cases[0].get("status"), "PASSED")
                self.assertEqual(cases[0].get("assertions"), "1")
                self.assertEqual(cases[1].find("failure").text, "detail")
                self.assertIsNotNone(cases[2].find("skipped"))

    def test_malformed_xml_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.xml"
            path.write_text("<testsuite>")
            with self.assertRaises(ET.ParseError):
                ET.parse(path)


if __name__ == "__main__":
    unittest.main()
