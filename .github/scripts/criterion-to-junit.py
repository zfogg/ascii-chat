#!/usr/bin/env python3
"""Convert Criterion XML format to JUnit XML format for codecov compatibility."""

import sys
import xml.etree.ElementTree as ET
import glob
from pathlib import Path

def criterion_to_junit(criterion_xml_path, output_path):
    """Convert a single Criterion XML file to JUnit format."""
    tree = ET.parse(criterion_xml_path)
    root = tree.getroot()

    # Handle both top-level testsuites and testsuite
    if root.tag == 'testsuites':
        testsuites = root
    else:
        # Wrap single testsuite in testsuites
        testsuites = ET.Element('testsuites')
        testsuites.append(root)

    # Process each testsuite to add classname to testcases
    for testsuite in testsuites.findall('testsuite'):
        classname = testsuite.get('name', 'Unknown')

        for testcase in testsuite.findall('testcase'):
            # Add classname attribute (required by JUnit)
            testcase.set('classname', classname)

            # Remove Criterion-specific attributes that aren't JUnit standard
            for attr in ['assertions', 'status']:
                if attr in testcase.attrib:
                    del testcase.attrib[attr]

    # Write output using the modified tree
    tree.write(output_path, encoding='UTF-8', xml_declaration=True)

def main():
    """Convert all Criterion XML files in a directory."""
    if len(sys.argv) < 2:
        print("Usage: criterion-to-junit.py <criterion-xml-dir> [output-dir]")
        sys.exit(1)

    input_dir = Path(sys.argv[1])
    output_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else input_dir

    if not input_dir.exists():
        print(f"Error: Directory {input_dir} does not exist")
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    xml_files = list(input_dir.glob('*.xml'))
    if not xml_files:
        print(f"No XML files found in {input_dir}")
        sys.exit(0)  # Return success for empty directory (no files to convert)

    converted = 0
    failed = 0
    for xml_file in xml_files:
        try:
            output_file = output_dir / xml_file.name
            criterion_to_junit(str(xml_file), str(output_file))
            print(f"✓ Converted {xml_file.name}")
            converted += 1
        except Exception as e:
            print(f"✗ Error converting {xml_file.name}: {e}", file=sys.stderr)
            failed += 1

    print(f"\n✓ Successfully converted {converted} XML files to JUnit format")
    if failed > 0:
        print(f"⚠ Failed to convert {failed} files", file=sys.stderr)
        sys.exit(1)
    sys.exit(0)

if __name__ == '__main__':
    main()
