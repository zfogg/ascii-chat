"""Regression checks for the Criterion-to-JUnit upload conversion."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    "converter", Path(__file__).with_name("criterion-to-junit.py")
)
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)


class JUnitConversionTests(unittest.TestCase):
    def test_single_suite_and_multiple_suites(self):
        suite = (
            '<testsuite name="stats" tests="3">'
            '<testcase name="pass" time="0.1" assertions="1" status="PASSED" />'
            '<testcase name="fail" time="0.2"><failure>detail</failure></testcase>'
            '<testcase name="skip" time="0"><skipped /></testcase></testsuite>'
        )
        for xml in (suite, "<testsuites>" + suite + "</testsuites>"):
            with self.subTest(xml=xml), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "report.xml"
                path.write_text(xml)
                converter.criterion_to_junit(path, path)
                root = ET.parse(path).getroot()
                self.assertEqual(root.tag, "testsuites")
                cases = root.findall("testsuite/testcase")
                self.assertEqual(len(cases), 3)
                self.assertTrue(all(c.get("classname") == "stats" for c in cases))
                self.assertNotIn("status", cases[0].attrib)
                self.assertNotIn("assertions", cases[0].attrib)
                self.assertEqual(cases[1].find("failure").text, "detail")
                self.assertIsNotNone(cases[2].find("skipped"))

    def test_malformed_xml_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.xml"
            path.write_text("<testsuite>")
            with self.assertRaises(ET.ParseError):
                converter.criterion_to_junit(path, path)


if __name__ == "__main__":
    unittest.main()
